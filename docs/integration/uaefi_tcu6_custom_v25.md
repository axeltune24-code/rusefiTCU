# uaEFI 6-Speed TCU + custom-v25: source-backed integration

## Provenance

Working TCU baseline: branch `uaefi-6speed-tcu`, commit
`c2e86541a793d6fa02fef44f02abf19c05b2c527`.

User-provided verified V25 family archive: `uaEFI_Family_V25_20261027 (2).zip`.
It contains exact source overlays, patches, INI and packaged BIN for uaEFI F407,
uaEFI Pro F767 and Super uaEFI F767.

- F407 original base: rusEFI `23b6050c4ec9b1f406e49de1487fb1f9d2717676`;
  source patch: `boards/uaEFI_F407/source/custom_source.patch`.
- Super F767 original base: rusEFI `c903a0462ce11437c2b7accbc5ff89ac81d7dbda`;
  source patch: `boards/Super_uaEFI_F767/source/source_from_official_c903a046.patch`.
- `source/overlay/` contains complete V25 versions of affected C++ sources.

**DO NOT combine the BINs or reuse each other's MSQ/INI without generated
migration.** V25 F407 uses 4 config pages (24460,256,2048,1268) and TCU6
uses 5 (17764,256,2048,1268,8000). For example `tcuSolenoidTable` is
at V25 offset 15352 but at TCU6 offset 7436.

## Implementation status

| Feature | Work in this integration branch |
|---|---|
| 1. Absolute cranking RPM x CLT 4x6 | Ported from V25 to new config fields, UI, defaults and legacy fallback in commit `174a9d30`. CI/ABI migration review still required. |
| 2. Post-start air hold/decay | Original reference: `post_start_air.h`, `idle_thread.cpp/.h`. Not ported. |
| 3. Idle PID catch near end of start air | Original reference: `post_start_air.h`, `idle_thread.cpp/.h`. Not ported. |
| 4. Knock correction inhibit | Adapted to newer API in commit `75c1c1dc`. Needs comparison to V25 source and hardware testing. |
| 5. Knock spectrogram short windows | Original: `software_knock.cpp`, `fft.hpp`, board compile flags. Not ported. |
| 6. F767 RTC LSE switch | Original: `stm32f7xx_rtc.cpp` from F767 overlay. Not ported. |
| 7. WBO heat power permission | Original: `o2_heater_permission.h`, `engine.cpp`. Not ported. |
| 8. One-shot Force O2 heat | Same original helper. Not ported. |
| 9. PPS fault-counter recovery | Original: `pedal_fault_recovery.h`, `electronic_throttle.cpp`. Not ported. |
| 10. DFCO measured TPS / pedal veto | Adapted to newer API in commit `75c1c1dc`. Needs comparison to V25 source and hardware testing. |

All original source content can be retrieved from the supplied archive; do not
attempt to infer it from compiled BIN bytes.

## Build / release requirements

1. Preserve 6-speed automatic and button shifting 1-6, solenoid commands,
   configured gear count, and TCU/Boost/ETB TunerStudio menus.
2. Port sources to the later TCU6 firmware API *with reviews*, not wholesale
   replacement of unrelated engine modules with the older V25 tree.
3. Generate new firmware config and TS INI together; verify signatures, offsets,
   and explicit calibration migration. Features should be disabled by default.
4. Run Linux/Windows unit tests and simulator tests, including new cranking,
   idle, knock, DFCO, PPS, WBO and RTC tests.
5. Test the resulting *combined* uaEFI firmware on a load-safe bench before
   installing it on a running engine or transmission.

The user's separate V25 and TCU6 variants were reportedly bench tested and
working. This does NOT establish that the merged variant has been tested.
