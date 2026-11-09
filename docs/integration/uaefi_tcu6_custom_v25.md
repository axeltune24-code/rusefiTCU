# uaEFI TCU 6-speed + custom-v25 feature integration

Baseline: `uaefi-6speed-tcu`, firmware commit `c2e86541a793d6fa02fef44f02abf19c05b2c527`.

Reference binary supplied by the user: `rusefi_custom-v25_2026-10-27_uaefi_387715127_local.bin`
(780448 bytes, SHA-256 `ce2cb4b0805d4098386fa6040f7938f6b94f2a251922a1770e2001e6f9877adf`).

This binary is an **observational reference only**. It does not contain the human-readable
source patches or TunerStudio definitions needed to reproduce these changes exactly.
Do not flash it onto a 6-speed TCU as a substitute for a merged firmware build.

## Feature checklist

- [ ] 1. Absolute cranking ignition advance: opt-in 4 RPM x 6 CLT table,
      validate data and CLT, fall back to existing RPM curve.
- [ ] 2. Time-based start air: hold / decay, new crank episode, TPS cancel with no rebound.
- [ ] 3. Idle PID handoff near end of start-air transition, separate timing/fuel phases.
- [x] 4. Suppress knock retard and trim in STOPPED, CRANKING, SPINNING_UP and
      below knockSuppressMinTps; guard late ADC samples; keep threshold/gain updates.
      Needs CI and bench verification.
- [ ] 5. Spectrogram short-window zero padding, ADC metadata freeze, rate limiting,
      shared diagnostic buffer and knock-before-FFT.
- [ ] 6. STM32F7 RTC LSI->LSE watchdog-safe bounded switchover, calendar and bypass
      preservation, LSI fallback.
- [ ] 7. WBO heater permission from *valid* battery voltage and ignition power;
      no USB-only heating; leave WBO internal safety protections intact.
- [ ] 8. One-time Force O2 heating per confirmed ignition cycle, consumed on RUNNING.
- [ ] 9. PPS accumulated error counter recovery after one second of *valid, redundant*,
      released raw and filtered pedal with valid TPS; no other fault clearing.
- [x] 10. uaEFI/SuperuaEFI DFCO uses measured TPS, 1% installed-pedal veto,
       0.25 percentage-point TPS hysteresis on active fuel cut; other restrictions
       untouched. Needs CI and bench verification.

## Safety gates

All ten must be coded, tested and reviewed before this branch can be considered
a complete custom-v25 port. CI compilation of a BIN alone is not proof of correctness.
Never replace existing six-speed calibration with a different BIN's flash/config
layout, and never road-test without validating ETB, knock, heater, fuel-cut, RTC,
and solenoid outputs on the bench. The existing transmission and tuner UI
definitions must remain intact.
