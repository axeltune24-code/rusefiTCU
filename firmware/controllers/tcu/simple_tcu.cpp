#include "pch.h"

#include "simple_tcu.h"

#if EFI_TCU
SimpleTransmissionController simpleTransmissionController;
static SimplePwm simplePcPwm("Simple TCU Line Pressure");

void SimpleTransmissionController::init() {
    for (size_t i = 0; i < efi::size(engineConfiguration->tcu_solenoid); i++) {
        enginePins.tcuSolenoids[i].initPin("Transmission Solenoid", engineConfiguration->tcu_solenoid[i], engineConfiguration->tcu_solenoid_mode[i]);
    }

	// Generic4/GM modes have their own pressure PWM. Do not start a second
	// PWM driver for the same output when they call this base initializer.
	if (getMode() == TransmissionControllerMode::SimpleTransmissionController) {
		enginePins.tcuPcSolenoid.initPin("Simple TCU Pressure Solenoid", engineConfiguration->tcu_pc_solenoid_pin, engineConfiguration->tcu_pc_solenoid_pin_mode);
		startSimplePwm(&simplePcPwm, "Simple TCU Line Pressure", &engine->scheduler,
			&enginePins.tcuPcSolenoid, engineConfiguration->tcu_pc_solenoid_freq, 0);
	}
}

void SimpleTransmissionController::update(gear_e gear) {
	if (getMode() == TransmissionControllerMode::SimpleTransmissionController) {
		updateLinePressure(gear);
	}

    for (size_t i = 0; i < efi::size(engineConfiguration->tcu_solenoid); i++) {
#if ! EFI_UNIT_TEST
			enginePins.tcuSolenoids[i].setValue(config->tcuSolenoidTable[i][static_cast<int>(gear) + 1]);
#endif
    }
    setCurrentGear(gear);

#if EFI_TUNER_STUDIO
    if (engineConfiguration->debugMode == DBG_TCU) {
        engine->outputChannels.debugIntField1 = config->tcuSolenoidTable[static_cast<int>(gear) + 1][0];
        engine->outputChannels.debugIntField2 = config->tcuSolenoidTable[static_cast<int>(gear) + 1][1];
        engine->outputChannels.debugIntField3 = config->tcuSolenoidTable[static_cast<int>(gear) + 1][2];
        engine->outputChannels.debugIntField4 = config->tcuSolenoidTable[static_cast<int>(gear) + 1][3];
        engine->outputChannels.debugIntField5 = config->tcuSolenoidTable[static_cast<int>(gear) + 1][4];
    }
#endif

		TransmissionControllerBase::update(gear);
}


void SimpleTransmissionController::updateLinePressure(gear_e gear) {
	if (gear != getCurrentGear()) {
		pressureShiftFrom = getCurrentGear();
		isShifting = true;
		measureShiftTime(gear);
	}

	const uint8_t (*pcts)[TCU_TABLE_WIDTH] = nullptr;
	switch (gear) {
	case REVERSE: pcts = &config->tcu_pcValsR; break;
	case NEUTRAL: pcts = &config->tcu_pcValsN; break;
	case GEAR_1:
		pcts = isShifting && pressureShiftFrom == GEAR_2 ? &config->tcu_pcVals21 : &config->tcu_pcVals1;
		break;
	case GEAR_2:
		pcts = isShifting && pressureShiftFrom == GEAR_1 ? &config->tcu_pcVals12 :
			(isShifting && pressureShiftFrom == GEAR_3 ? &config->tcu_pcVals32 : &config->tcu_pcVals2);
		break;
	case GEAR_3:
		pcts = isShifting && pressureShiftFrom == GEAR_2 ? &config->tcu_pcVals23 :
			(isShifting && pressureShiftFrom == GEAR_4 ? &config->tcu_pcVals43 : &config->tcu_pcVals3);
		break;
	case GEAR_4:
		pcts = isShifting && pressureShiftFrom == GEAR_3 ? &config->tcu_pcVals34 :
			(isShifting && pressureShiftFrom == GEAR_5 ? &config->tcu_pcVals54 : &config->tcu_pcVals4);
		break;
	case GEAR_5:
		pcts = isShifting && pressureShiftFrom == GEAR_4 ? &config->tcu_pcVals45 :
			(isShifting && pressureShiftFrom == GEAR_6 ? &config->tcu_pcVals65 : &config->tcu_pcVals5);
		break;
	case GEAR_6:
		pcts = isShifting && pressureShiftFrom == GEAR_5 ? &config->tcu_pcVals56 : &config->tcu_pcVals6;
		break;
	default:
		// An unsupported gear must not index a pressure table.
		return;
	}

	// Preserve the established rusEFI airflow-based pressure axis. A standalone
	// TCU needs a valid airflow estimate supplied by its ECU/CAN configuration.
	float duty = interpolate2d(engine->engineState.airflowEstimate, config->tcu_pcAirmassBins, *pcts);
	if (duty < 0) duty = 0;
	if (duty > 100) duty = 100;
	pressureControlDuty = static_cast<int8_t>(duty);
	simplePcPwm.setSimplePwmDutyCycle(duty * 0.01f);

	if (isShifting) {
		float elapsed = isShiftCompleted();
		if (elapsed != 0) {
			lastShiftTime = elapsed;
			isShifting = false;
		}
	}
}

SimpleTransmissionController* getSimpleTransmissionController() {
	return &simpleTransmissionController;
}
#endif // EFI_TCU
