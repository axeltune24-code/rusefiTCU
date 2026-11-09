#include "pch.h"

#include "knock_logic.h"

using ::testing::StrEq;
using ::testing::ThrowsMessage;

struct MockKnockController : public KnockControllerBase {
	float getKnockThreshold() const override {
		// Knock threshold of 20dBv
		return 20;
	}

	float getMaximumRetard() const override {
		// Maximum 8 degrees retarded
		return 8;
	}
};

TEST(Knock, frequencyApproximation) {
  // that's first harmonic while default is knockDetectionUseDoubleFrequency for second harmonic
  ASSERT_NEAR(7.3456, bore2frequency(78/*mm*/), EPS2D);
}

TEST(Knock, InvalidFrequencyIsIgnoredWhenDisabled) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableSoftwareKnock = false;
	engineConfiguration->knockFrequency = 7.5f;

	EXPECT_TRUE(validateConfigOnStartUpOrBurn());
}

TEST(Knock, InvalidFrequencyIsRejectedWhenEnabled) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableSoftwareKnock = true;
	engineConfiguration->knockFrequency = 7.5f;

	EXPECT_FATAL_ERROR(validateConfigOnStartUpOrBurn());
}

TEST(Knock, SensingDisabledIsAcceptedOnBurn) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableSoftwareKnock = false;

	EXPECT_TRUE(validateConfigOnStartUpOrBurn(false));
}

TEST(Knock, SensingEnabledOnStartup) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableSoftwareKnock = true;

	for (float frequency : {0.0f, 7500.0f}) {
		engineConfiguration->knockFrequency = frequency;
		EXPECT_THAT([] { validateConfigOnStartUpOrBurn(); },
			ThrowsMessage<std::logic_error>(StrEq(
				"Knock sensing is enabled but this firmware does not support software knock")));
		EXPECT_TRUE(engineConfiguration->enableSoftwareKnock);
	}
}

TEST(Knock, SensingEnabledOnBurn) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableSoftwareKnock = true;

	for (float frequency : {0.0f, 7500.0f}) {
		engineConfiguration->knockFrequency = frequency;
		EXPECT_THAT([] { validateConfigOnStartUpOrBurn(false); },
			ThrowsMessage<std::logic_error>(StrEq(
				"Knock sensing is enabled but this firmware does not support software knock")));
		EXPECT_TRUE(engineConfiguration->enableSoftwareKnock);
	}
}

TEST(Knock, Retards) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->rpmCalculator.setRpmValue(2000);

	// Aggression of 10%
	engineConfiguration->knockRetardAggression = 10;
	// disable suppress for test
	engineConfiguration->knockSuppressMinTps = 0;

	setArrayValues(engine->engineState.timingAdvance, 0.0f);

	MockKnockController dut;
	dut.onFastCallback();

	// No retard unless we knock
	ASSERT_FLOAT_EQ(dut.getKnockRetard(), 0);

	// Need to call onFastCallback once to update thresholds from the Mock
	dut.onFastCallback();

	// Send some weak knocks, should yield no response
	for (size_t i = 0; i < 10; i++) {
		dut.onKnockSenseCompleted(0, 10, 0);
	}

	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 0);

	// Send a strong knock!
	dut.onKnockSenseCompleted(0, 30, 0);

	// Should retard 10% of the distance between current timing (0) and "minimum" (-20)
	// dist = 0 - (-20) = 20. 10% of 20 = 2.
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 2);

	// Send tons of strong knocks, make sure we don't go over the configured limit
	for (size_t i = 0; i < 100; i++) {
		dut.onKnockSenseCompleted(0, 30, 0);
	}

	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 8);
}

TEST(Knock, Reapply) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->rpmCalculator.setRpmValue(2000);
	setArrayValues(engine->engineState.timingAdvance, 0.0f);

	// disable suppress for test
	engineConfiguration->knockSuppressMinTps = 0;

	MockKnockController dut;
	dut.onFastCallback();

	// Aggression of 10%
	engineConfiguration->knockRetardAggression = 10;
	// Apply 1 degree/second
	engineConfiguration->knockRetardReapplyRate = 1;

	// disable suppress for test
	engineConfiguration->knockSuppressMinTps = 0;

	// Send a strong knock!
	dut.onKnockSenseCompleted(0, 30, 0);

	// Should retard 10% of the distance between current timing (0) and "minimum" (-20)
	// dist = 0 - (-20) = 20. 10% of 20 = 2.
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 2);

	constexpr auto fastPeriodSec = FAST_CALLBACK_PERIOD_MS / 1000.0f;

	// call the fast callback, should reapply 1 degree * callback period
	dut.onFastCallback();
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 2 - 1.0f * fastPeriodSec);

	// 10 updates total
	for (size_t i = 0; i < 9; i++) {
		dut.onFastCallback();
	}
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 2 - 10 * 1.0f * fastPeriodSec);

	// Spend a long time without knock
	for (size_t i = 0; i < 1000; i++) {
		dut.onFastCallback();
	}

	// Should have no knock retard
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 0);
}

TEST(Knock, FuelTrim) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->rpmCalculator.setRpmValue(2000);
	setArrayValues(engine->engineState.timingAdvance, 0.0f);

	// Aggression of 10%
	engineConfiguration->knockFuelTrimAggression = 10;
	engineConfiguration->knockFuelTrim = 30;
	// disable suppress for test
	engineConfiguration->knockSuppressMinTps = 0;

	MockKnockController dut;
	dut.onFastCallback();

	// No trim unless we knock
	ASSERT_FLOAT_EQ(dut.getFuelTrimMultiplier(), 1.0);

	// Need to call onFastCallback once to update thresholds from the Mock
	dut.onFastCallback();

	// Send some weak knocks, should yield no response
	for (size_t i = 0; i < 10; i++) {
		dut.onKnockSenseCompleted(0, 10, 0);
	}

	EXPECT_FLOAT_EQ(dut.getFuelTrimMultiplier(), 1.0);

	// Send a strong knock!
	dut.onKnockSenseCompleted(0, 30, 0);

	// 30% * 10% = 3% = 0.03
	EXPECT_FLOAT_EQ(dut.getFuelTrimMultiplier(), 1.03f);

	// Send tons of strong knocks, make sure we don't go over the configured limit
	for (size_t i = 0; i < 100; i++) {
		dut.onKnockSenseCompleted(0, 30, 0);
	}

	EXPECT_FLOAT_EQ(dut.getFuelTrimMultiplier(), 1.3f);
}

TEST(Knock, FuelTrimReapply) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->rpmCalculator.setRpmValue(2000);
	setArrayValues(engine->engineState.timingAdvance, 0.0f);

	// disable suppress for test
	engineConfiguration->knockSuppressMinTps = 0;

	MockKnockController dut;
	dut.onFastCallback();

	// Aggression of 100%
	engineConfiguration->knockFuelTrimAggression = 10;
	// Apply 1%/second
	engineConfiguration->knockFuelTrimReapplyRate = 1;

  // fuel trim 30%
	engineConfiguration->knockFuelTrim = 30;

	// disable suppress for test
	engineConfiguration->knockSuppressMinTps = 0;

	// Send a strong knock!
	dut.onKnockSenseCompleted(0, 30, 0);

  // 100% trim
  float trim = 1.03f;

	// Should retard 100% of the distance between current timing and "maximum"
	EXPECT_FLOAT_EQ(dut.getFuelTrimMultiplier(), trim);

	constexpr auto fastPeriodSec = FAST_CALLBACK_PERIOD_MS / 1000.0f;

	// call the fast callback, should reapply 1% trim * callback period
	dut.onFastCallback();
	EXPECT_FLOAT_EQ(dut.getFuelTrimMultiplier(), trim - 0.01f * fastPeriodSec);

	// 10 updates total
	for (size_t i = 0; i < 9; i++) {
		dut.onFastCallback();
	}
	EXPECT_FLOAT_EQ(dut.getFuelTrimMultiplier(), trim - 10 * 0.01f * fastPeriodSec);

	// Spend a long time without knock
	for (size_t i = 0; i < 2000; i++) {
		dut.onFastCallback();
	}

	// Should have no knock retard
	EXPECT_FLOAT_EQ(dut.getFuelTrimMultiplier(), 1.0);
}

TEST(Knock, SuppressesRetardDuringCrankingAndLowTps) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->knockSuppressMinTps = 2;
	engineConfiguration->knockRetardAggression = 10;
	setArrayValues(engine->engineState.timingAdvance, 0.0f);

	MockKnockController dut;
	dut.onFastCallback();
	dut.onKnockSenseCompleted(0, 30, 0);
	EXPECT_FLOAT_EQ(0, dut.getKnockRetard());

	engine->rpmCalculator.setRpmValue(200);
	Sensor::setMockValue(SensorType::Tps1, 10);
	dut.onFastCallback();
	dut.onKnockSenseCompleted(0, 30, 0);
	EXPECT_FLOAT_EQ(0, dut.getKnockRetard());

	engine->rpmCalculator.setRpmValue(2000);
	Sensor::setMockValue(SensorType::Tps1, 1);
	dut.onFastCallback();
	dut.onKnockSenseCompleted(0, 30, 0);
	EXPECT_FLOAT_EQ(0, dut.getKnockRetard());

	Sensor::setMockValue(SensorType::Tps1, 10);
	dut.onFastCallback();
	dut.onKnockSenseCompleted(0, 30, 0);
	EXPECT_GT(dut.getKnockRetard(), 0);

	engine->rpmCalculator.setRpmValue(0);
	dut.onFastCallback();
	EXPECT_FLOAT_EQ(0, dut.getKnockRetard());
	EXPECT_FLOAT_EQ(1, dut.getFuelTrimMultiplier());
	dut.onKnockSenseCompleted(0, 30, 0);
	EXPECT_FLOAT_EQ(0, dut.getKnockRetard());
}

#include "../../firmware/controllers/algo/o2_heater_permission.h"
#include "../../firmware/controllers/actuators/post_start_air.h"
#include "../../firmware/controllers/actuators/pedal_fault_recovery.h"
#include "../../firmware/controllers/algo/ignition/cranking_clt.h"
#include "../../firmware/controllers/sensors/impl/fft/knock_window.h"

TEST(V25, PostStartAirRequiresCrankingAndDoesNotReboundAfterPedal) {
	PostStartAir air;
	air.update(0, false, false, true, true, false, 0.5f, 2.0f);
	EXPECT_FLOAT_EQ(1, air.fraction());
	air.reset();
	air.update(0, false, true, false, true, false, 0.5f, 2.0f);
	air.update(100000, false, false, true, true, false, 0.5f, 2.0f);
	EXPECT_FLOAT_EQ(0, air.fraction());
	air.update(800000, false, false, true, true, false, 0.5f, 2.0f);
	const float before = air.fraction();
	EXPECT_GT(before, 0);
	air.update(900000, false, false, true, true, true, 0.5f, 2.0f);
	air.update(1300000, false, false, true, true, false, 0.5f, 2.0f);
	const float after = air.fraction();
	EXPECT_GE(after, before);
	air.update(4000000, false, false, true, true, false, 0.5f, 2.0f);
	EXPECT_FLOAT_EQ(1, air.fraction());
	air.update(5000000, true, false, false, true, false, 0.5f, 2.0f);
	EXPECT_FLOAT_EQ(1, air.fraction());
	EXPECT_FALSE(PostStartAir::configured(0, 0));
}

TEST(V25, O2HeaterNeedsRealPowerAndConsumesForcedCycle) {
	O2HeaterPermission heat;
	EXPECT_FALSE(heat.update(0, true, false, true, true, true, 5.0f));
	EXPECT_FALSE(heat.update(100000, true, false, false, true, true, 12.5f));
	EXPECT_TRUE(heat.update(200000, true, false, true, true, true, 12.5f));
	EXPECT_TRUE(heat.forceActive());
	EXPECT_TRUE(heat.update(250000, true, true, true, true, true, 12.5f));
	EXPECT_FALSE(heat.forceActive());
	EXPECT_FALSE(heat.update(300000, true, false, true, true, true, 12.5f));
	// Transient starter sag is not proof of a new ignition cycle.
	EXPECT_FALSE(heat.update(450000, true, false, false, true, true, 5.5f));
	EXPECT_FALSE(heat.update(700000, true, false, true, true, true, 12.5f));
	heat.update(800000, true, false, false, true, true, 12.5f, true);
	heat.update(1400000, true, false, false, true, true, 12.5f, true);
	EXPECT_TRUE(heat.update(1500000, true, false, true, true, true, 12.5f, true));
}

TEST(V25, PpsFaultClearsOnlyAfterSecondOfValidReleasedPair) {
	ReleasedPedalRecovery pps;
	EXPECT_FALSE(pps.update(0, true, 0, 0));
	EXPECT_FALSE(pps.update(900000, true, 0, 0));
	EXPECT_FALSE(pps.update(950000, false, 0, 0));
	EXPECT_FALSE(pps.update(1000000, true, 0, 0));
	EXPECT_FALSE(pps.update(1950000, true, 0, 0));
	EXPECT_TRUE(pps.update(2000000, true, 0, 0));
	EXPECT_FALSE(pps.update(2050000, true, 2, 0));
	EXPECT_FALSE(pps.update(2100000, true, 0, 0));
}

TEST(V25, CrankingCltIsOptInAndInvalidAxisFallsBack) {
	struct SyntheticCranking {
		uint16_t crankingAdvanceBins[4] = {0, 200, 400, 1000};
		float crankingAdvance[4] = {1, 2, 3, 4};
		uint32_t crankingAdvanceCltEnabled = 0;
		int16_t crankingAdvanceCltBins[6]{};
		float crankingAdvanceCltTable[6][4]{};
	} cfg{};
	initializeCrankingClt(cfg);
	EXPECT_FALSE(crankingCltTableUsable(cfg, true, 20));
	cfg.crankingAdvanceCltEnabled = 1;
	EXPECT_TRUE(crankingCltTableUsable(cfg, true, 20));
	EXPECT_FALSE(crankingCltTableUsable(cfg, false, 20));
	cfg.crankingAdvanceCltBins[2] = -20;
	EXPECT_FALSE(crankingCltTableUsable(cfg, true, 20));
}
