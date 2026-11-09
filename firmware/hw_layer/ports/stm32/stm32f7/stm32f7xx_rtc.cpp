/**
 * @file stm32f7xx_rtc.cpp
 * @brief Preserve RTC calendar while recovering the STM32F7 LSE oscillator.
 * Derived from the user's tested V25 source overlay.
 */
#include "pch.h"

#if HAL_USE_RTC
#if STM32_RTCSEL == STM32_RTCSEL_LSE
// Called after ChibiOS init and watchdog setup.
static bool waitForF7Lse() {
    for (unsigned i = 0; i < 200; ++i) {
        if (RCC->BDCR & RCC_BDCR_LSERDY) return true;
#if HAL_USE_WDG
        wdgReset(&WDGD1);
#endif
        chThdSleepMilliseconds(10);
    }
    return (RCC->BDCR & RCC_BDCR_LSERDY) != 0;
}
#endif

void hal_lld_rtc_fixup(void) {
#if STM32_RTCSEL == STM32_RTCSEL_LSE
    if ((RCC->BDCR & STM32_RTCSEL_MASK) == STM32_RTCSEL) {
        return;
    }
    if (!waitForF7Lse()) {
        efiPrintf("RTC: LSE unavailable; retaining LSI (no battery timekeeping)");
        return;
    }

    RTCDateTime timespec;
    rtcGetTime(&RTCD1, &timespec);
    const uint32_t backup0 = RTC->BKP0R;
    const uint32_t backup1 = RTC->BKP1R;
    const uint32_t backup2 = RTC->BKP2R;
    const uint32_t backup3 = RTC->BKP3R;
    const uint32_t lseMode = RCC->BDCR & RCC_BDCR_LSEBYP;

    RCC->BDCR |= RCC_BDCR_BDRST;
    RCC->BDCR &= ~RCC_BDCR_BDRST;
    RCC->BDCR = lseMode | STM32_LSEDRV | RCC_BDCR_LSEON;

    if (waitForF7Lse()) {
        RCC->BDCR |= STM32_RTCSEL;
    } else {
        efiPrintf("RTC: LSE not ready after restart; falling back to LSI");
        RCC->BDCR |= RUSEFI_STM32_LSE_WAIT_MAX_RTCSEL;
    }
    RCC->BDCR |= RCC_BDCR_RTCEN;
    rtcInit();
    rtcSetTime(&RTCD1, &timespec);
    RTC->BKP0R = backup0;
    RTC->BKP1R = backup1;
    RTC->BKP2R = backup2;
    RTC->BKP3R = backup3;
#endif
}
#endif // HAL_USE_RTC
