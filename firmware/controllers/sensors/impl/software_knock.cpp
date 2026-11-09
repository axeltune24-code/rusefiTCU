/**
 * @file software_knock.cpp
 * @brief Digital Signal Processing (DSP) implementation for knock detection.
 *
 * This module implements software-based knock detection using the microcontroller's ADC.
 * The process follows these steps:
 * 1. ADC Capture: High-speed sampling of the knock sensor signal into a circular or DMA buffer.
 * 2. Bandpass Filtering: A Biquad filter is applied to the raw samples to isolate the
 *    knocking frequency (calculated from cylinder bore or manually configured).
 * 3. Energy Calculation: Computes the RMS (Root Mean Square) energy level of the filtered signal.
 * 4. Reporting: The calculated dB level is sent to the KnockController for high-level logic.
 *
 * Additionally, if enabled, it can perform FFT (Fast Fourier Transform) to generate
 * spectrogram data for tuning and visualization in TunerStudio.
 */

#include "pch.h"

#if EFI_SOFTWARE_KNOCK

#include "biquad.h"
#include "thread_controller.h"
#include "knock_logic.h"
#include "software_knock.h"
#include "knock_config.h"
#include "ch.hpp"
#include "error_handling.h"
#include "adc_onchip.h"

#if KNOCK_SPECTROGRAM
#include "fft/fft.hpp"
#include "fft/knock_window.h"
#include "big_buffer.h"

#define COMPRESSED_SPECTRUM_PROTOCOL_SIZE 16 // 16 * 4 = 64 byte for transport to TS
#define START_SPECTRORGAM_FREQUENCY 4000 // magic minimum Hz for draw spectrogram, use near value +next 64 freqs from fft

static size_t spectrogramStartIndex = 0;
static BigBufferHandle spectrumBuffer;
static SpectrogramData* spectrogramData = nullptr;
static_assert(sizeof(SpectrogramData) <= BIG_BUFFER_SIZE);
#endif //KNOCK_SPECTROGRAM


static NO_CACHE adcsample_t sampleBuffer[1800];
static int8_t currentCylinderNumber = 0;
static int8_t channelNumber = 0;
static efitick_t lastKnockSampleTime = 0;
static Biquad knockFilter;

static volatile bool knockIsSampling = false;
static volatile bool knockNeedsProcess = false;
static volatile size_t sampleCount = 0;

chibios_rt::BinarySemaphore knockSem(/* taken =*/ true);

void onKnockSamplingComplete() {
	knockNeedsProcess = true;

	// Notify the processing thread that it's time to process this sample
	chSysLockFromISR();
	knockSem.signalI();
	chSysUnlockFromISR();
}

/**
 * Prepares the hardware for a new sampling window.
 * Calculates the number of samples needed based on RPM and the configured crank angle duration.
 * Triggers the ADC conversion in the background using DMA.
 */
void onStartKnockSampling(uint8_t cylinderNumber, float samplingSeconds, uint8_t channelIdx) {
	if (!engineConfiguration->enableSoftwareKnock) {
		return;
	}

	chibios_rt::CriticalSectionLocker csl;
	// If there's pending processing, leave a background conversion alone.
	if (knockNeedsProcess) {
		return;
	}
#if EFI_ADC3_SLOW
	adc3SlowPreemptI(&KNOCK_ADC);
#endif

	// Cancel if ADC isn't ready
	if (!((KNOCK_ADC.state == ADC_READY) ||
			(KNOCK_ADC.state == ADC_ERROR))) {
		return;
	}

	// Convert sampling time to number of samples
#if defined(STM32H7XX)
	float sampleRate = KNOCK_SAMPLE_RATE / (isStm32H7revisionV() ? 2 : 1);
#else
	constexpr int sampleRate = KNOCK_SAMPLE_RATE;
#endif
	sampleCount = 0xFFFFFFFE & static_cast<size_t>(clampF(100, samplingSeconds * sampleRate, efi::size(sampleBuffer)));

	// Select the appropriate conversion group - it will differ depending on which sensor this cylinder should listen on
	auto conversionGroup = getKnockConversionGroup(channelIdx);

	//current chanel number for spectrum TS plugin
	channelNumber = channelIdx;

	// Stash the current cylinder's number so we can store the result appropriately
	currentCylinderNumber = cylinderNumber;

	adcStartConversionI(&KNOCK_ADC, conversionGroup, sampleBuffer, sampleCount);
	lastKnockSampleTime = getTimeNowNt();
}

class KnockThread : public ThreadController<UTILITY_THREAD_STACK_SIZE> {
public:
	KnockThread() : ThreadController("knock", PRIO_KNOCK_PROCESS) {}
	void ThreadTask() override;
};

static KnockThread kt;

void initSoftwareKnock() {
	if (engineConfiguration->enableSoftwareKnock) {
		float frequencyHz = 0;

#if defined(STM32H7XX)
		float sampleRate = KNOCK_SAMPLE_RATE / (isStm32H7revisionV() ? 2 : 1);
		efiPrintf("ADC clock %d sampling rate %d", (int)STM32_ADC3_CLOCK / (isStm32H7revisionV() ? 2 : 1) , (int)sampleRate);
#else
		constexpr float sampleRate = KNOCK_SAMPLE_RATE;
#endif

		if (engineConfiguration->knockFrequency > 0.01) {
			frequencyHz = engineConfiguration->knockFrequency;
		} else {
			frequencyHz = 1000 * bore2frequency(engineConfiguration->cylinderBore);
			frequencyHz = engineConfiguration->knockDetectionUseDoubleFrequency ? 2 * frequencyHz : frequencyHz;
			efiPrintf("Calculated knock frequency %.3f KHz", frequencyHz / 1000.0);
		}

		// A center frequency this far below the sample rate can't be represented by the filter.
		if (frequencyHz < sampleRate / 1000.0f) {
			configError("Invalid knock band frequency: %.2f Hz", frequencyHz);
			return;
		}

		knockFilter.configureBandpass(sampleRate, frequencyHz, 3);

	#if KNOCK_SPECTROGRAM
		if (engineConfiguration->enableKnockSpectrogram) {

			// The shared diagnostic buffer is acquired on demand by the
			// processing thread. Knock detection works without FFT.
			int freqStartConst = START_SPECTRORGAM_FREQUENCY;
			int minFreqDiff = freqStartConst;
			int freqStart = 0;
			float freqStep = 0;

			for (size_t i = 0; i < FFT_SIZE/2; i++)
			{
				float freq = float(i * sampleRate) / FFT_SIZE;
				int min = static_cast<int>(absF(freq - freqStartConst));

				// next after freq start index
				if(i == spectrogramStartIndex + 1) {
					freqStep = absF(freq - freqStart);
				}

				if(min < minFreqDiff) {
					minFreqDiff = min;
					spectrogramStartIndex = i;
					freqStart = static_cast<int>(freq);
				}
			}

			engine->module<KnockController>()->m_knockFrequencyStart = (uint16_t)freqStart;
			engine->module<KnockController>()->m_knockFrequencyStep = freqStep;
		}
	#endif // KNOCK_SPECTROGRAM

		// Hardware init precedes calibration correction. An unsupported spectrogram
		// request must not prevent ordinary knock detection from starting.
		// fun fact: we do not offer any ADC channel flexibility like we have for many other kinds of inputs
		efiSetPadMode("knock ch1", KNOCK_PIN_CH1, PAL_MODE_INPUT_ANALOG);
#if KNOCK_HAS_CH2
		efiSetPadMode("knock ch2", KNOCK_PIN_CH2, PAL_MODE_INPUT_ANALOG);
#endif
		kt.start();
	}
}

#if KNOCK_SPECTROGRAM
static uint8_t toDb(const float& voltage) {
	float db = 200 * log10f(voltage*voltage) + 40; // best scaling for view
	db = clampF(0, db, 255);
	return uint8_t(db);
}
#endif

/**
 * The core DSP routine executed in a dedicated high-priority thread (KnockThread).
 * 1. Applies the Biquad bandpass filter to each ADC sample.
 * 2. Calculates the sum of squares of the filtered signal.
 * 3. (Optional) Performs FFT for spectrogram visualization.
 * 4. Calculates the RMS value in decibels (dB).
 * 5. Passes the result to the engine's KnockController.
 */
static void processLastKnockEvent() {
	if (!knockNeedsProcess) {
		return;
	}

	float sumSq = 0;
#if defined(STM32H7XX)
	// samples are 16 bit
	float ratio = engineConfiguration->adcVcc / ((1 << 16) - 1);
#else
	float ratio = engineConfiguration->adcVcc / 4095.0f;
#endif

	size_t localCount = sampleCount;
	uint8_t localCylinder = currentCylinderNumber;

	// Prepare the steady state at vcc/2 so that there isn't a step
	// when samples begin
	knockFilter.cookSteadyState(engineConfiguration->adcVcc / 2);

	// Compute the sum of squares
	for (size_t i = 0; i < localCount; i++) {
		float volts = ratio * sampleBuffer[i];

		float filtered = knockFilter.filter(volts);
//		if (i == localCount - 1 && engineConfiguration->debugMode == DBG_KNOCK) {
//			engine->outputChannels.debugFloatField1 = volts;
//			engine->outputChannels.debugFloatField2 = filtered;
//		}

		sumSq += filtered * filtered;
	}

	// Freeze ADC window metadata before accepting another sample.
	const auto lastKnockTime = lastKnockSampleTime;
#if KNOCK_SPECTROGRAM
	const auto channel = channelNumber;
#endif

#if KNOCK_SPECTROGRAM
	static Timer spectrumTimer;
	{
		chibios_rt::CriticalSectionLocker csl;
		if (engineConfiguration->enableKnockSpectrogram && !spectrumBuffer) {
			spectrumBuffer = getBigBuffer(BigBufferUser::KnockSpectrogram);
		} else if (!engineConfiguration->enableKnockSpectrogram && spectrumBuffer) {
			spectrumBuffer = BigBufferHandle();
		}
		spectrogramData = spectrumBuffer.get<SpectrogramData>();
	}
	const bool renderSpectrum = spectrogramData && engineConfiguration->enableKnockSpectrogram
		&& spectrumTimer.hasElapsedMs(50); // 20 frames per second max
	if (renderSpectrum) {
		spectrumTimer.reset();
		// FFT must never mutate the active knock bandpass filter state.
		Biquad spectrumFilter = knockFilter;
		spectrumFilter.cookSteadyState(engineConfiguration->adcVcc / 2);
		const size_t count = localCount < FFT_SIZE ? localCount : FFT_SIZE;
		KnockWindow window(count);
		for (size_t i = 0; i < FFT_SIZE; ++i) {
			float value = 0;
			if (i < count) {
				const float volts = ratio * sampleBuffer[i];
				value = engineConfiguration->enableKnockSpectrogramFilter
					? spectrumFilter.filter(volts)
					: volts - engineConfiguration->adcVcc / 2;
				value *= window.next() * engineConfiguration->knockSpectrumSensitivity;
			}
			spectrogramData->fftBuffer[i] = fft::complex_type(value, 0);
		}
	}
#endif

	{
		chibios_rt::CriticalSectionLocker csl;
		knockNeedsProcess = false;
	}
	const float meanSquares = sumSq / localCount;
	const float db = clampF(-100, 10 * log10f(meanSquares), 100);
	// Prioritize knock response over diagnostic FFT.
	engine->module<KnockController>()->onKnockSenseCompleted(localCylinder, db, lastKnockTime);

#if KNOCK_SPECTROGRAM
	if (renderSpectrum) {
		ScopePerf perf(PE::KnockAnalyzer);
		fft::ffti(spectrogramData->fftBuffer, FFT_SIZE);
		auto* spectrum = &engine->module<KnockController>()->m_knockSpectrum[0];
		for (uint8_t i = 0; i < COMPRESSED_SPECTRUM_PROTOCOL_SIZE; ++i) {
			const size_t k = spectrogramStartIndex + size_t(i) * 4;
			const uint32_t a = toDb(fft::amplitude(spectrogramData->fftBuffer[k]));
			const uint32_t b = toDb(fft::amplitude(spectrogramData->fftBuffer[k + 1]));
			const uint32_t c = toDb(fft::amplitude(spectrogramData->fftBuffer[k + 2]));
			const uint32_t d = toDb(fft::amplitude(spectrogramData->fftBuffer[k + 3]));
			const uint32_t compressed = (a << 24) | (b << 16) | (c << 8) | d;
			{
				chibios_rt::CriticalSectionLocker csl;
				spectrum[i] = compressed;
			}
		}
		const uint16_t channelCylinder = uint16_t(channel << 8 | localCylinder);
		{
			chibios_rt::CriticalSectionLocker csl;
			engine->module<KnockController>()->m_knockSpectrumChannelCyl = channelCylinder;
		}
	}
#endif

}

RUSEFI_STACK_ROOT(KnockThread, ThreadTask);
void KnockThread::ThreadTask() {
	while (1) {
		knockSem.wait();

		ScopePerf perf(PE::SoftwareKnockProcess);
		processLastKnockEvent();
	}
}


#endif // EFI_SOFTWARE_KNOCK
