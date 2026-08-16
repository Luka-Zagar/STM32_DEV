#include "rtc.h"
#include "stm32g474xx.h"
#include "systick.h"

/* Real-time bounds (ms), not CPU cycle counts: LSE crystals commonly take
 * up to ~2s to stabilize after LSEON is set, so a cycle-count busy-loop
 * (done first, then found to be wildly too short - it returned before
 * the crystal had any real time to start ringing) can't tell "not
 * populated" apart from "still starting up". LSI and RTC-INIT are fast
 * in comparison; short bounds there are just to avoid ever hanging if
 * something is genuinely broken. */
#define LSE_TIMEOUT_MS 2000UL
#define LSI_TIMEOUT_MS 10UL
#define RTC_INIT_TIMEOUT_MS 100UL

static rtc_clock_source_t clock_source = RTC_CLK_NONE;

static uint8_t bcd_to_bin(uint8_t bcd) {
    return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t bin) {
    return (uint8_t)(((bin / 10) << 4) | (bin % 10));
}

static void rtc_unlock(void) {
    RTC->WPR = 0xCA;
    RTC->WPR = 0x53;
}

static void rtc_lock(void) {
    RTC->WPR = 0xFF;
}

/* Polls `flag_reg` for `flag_bit` up to timeout_ms of real time. Returns
 * 1 if the flag came up in time, 0 on timeout. */
static int wait_flag_ms(const volatile uint32_t *flag_reg, uint32_t flag_bit, uint32_t timeout_ms) {
    uint32_t deadline = SysTick_GetMillis() + timeout_ms;
    while (!(*flag_reg & flag_bit)) {
        if ((int32_t)(SysTick_GetMillis() - deadline) >= 0) return 0;
    }
    return 1;
}

void rtc_init(void) {
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    PWR->CR1 |= PWR_CR1_DBP; /* unlock backup domain (LSE/BDCR/RTC) writes */

    RCC->BDCR |= RCC_BDCR_LSEON;
    int lse_ready = wait_flag_ms(&RCC->BDCR, RCC_BDCR_LSERDY, LSE_TIMEOUT_MS);

    uint32_t desired_rtcsel;
    uint32_t prer;
    if (lse_ready) {
        clock_source = RTC_CLK_LSE;
        desired_rtcsel = RCC_BDCR_RTCSEL_LSE;
        prer = (127UL << 16) | 255UL; /* (127+1)*(255+1) = 32768 -> exact 1Hz from a 32.768kHz LSE */
    } else {
        RCC->CSR |= RCC_CSR_LSION;
        clock_source = wait_flag_ms(&RCC->CSR, RCC_CSR_LSIRDY, LSI_TIMEOUT_MS) ? RTC_CLK_LSI : RTC_CLK_NONE;
        desired_rtcsel = RCC_BDCR_RTCSEL_LSI;
        prer = (127UL << 16) | 249UL; /* (127+1)*(249+1) = 32000 -> nominal 1Hz from a ~32kHz LSI */
    }

    if (clock_source == RTC_CLK_NONE) return; /* no usable clock source - RTC can't run */

    /* RTCSEL latches after its first write and ignores further changes
     * until the backup domain is reset - matters across repeated
     * flashing during development (no power cycle in between), not just
     * a first-ever boot. Clear it first if it doesn't already match. */
    if ((RCC->BDCR & RCC_BDCR_RTCSEL_Msk) != desired_rtcsel) {
        RCC->BDCR |= RCC_BDCR_BDRST;
        RCC->BDCR &= ~RCC_BDCR_BDRST;
        if (clock_source == RTC_CLK_LSE) {
            RCC->BDCR |= RCC_BDCR_LSEON;
            wait_flag_ms(&RCC->BDCR, RCC_BDCR_LSERDY, LSE_TIMEOUT_MS);
        }
        RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL_Msk) | desired_rtcsel;
    }

    RCC->BDCR |= RCC_BDCR_RTCEN;

    rtc_unlock();
    RTC->ISR |= RTC_ISR_INIT;
    wait_flag_ms(&RTC->ISR, RTC_ISR_INITF, RTC_INIT_TIMEOUT_MS);
    RTC->PRER = prer;
    RTC->ISR &= ~RTC_ISR_INIT;
    rtc_lock();
}

rtc_clock_source_t rtc_get_clock_source(void) {
    return clock_source;
}

void rtc_set_datetime(const rtc_datetime_t *dt) {
    if (clock_source == RTC_CLK_NONE) return;

    rtc_unlock();
    RTC->ISR |= RTC_ISR_INIT;
    wait_flag_ms(&RTC->ISR, RTC_ISR_INITF, RTC_INIT_TIMEOUT_MS);

    RTC->TR = ((uint32_t)bin_to_bcd(dt->hour) << 16) |
              ((uint32_t)bin_to_bcd(dt->min) << 8) |
              (uint32_t)bin_to_bcd(dt->sec);

    uint8_t yy = (uint8_t)(dt->year % 100);
    RTC->DR = ((uint32_t)bin_to_bcd(yy) << 16) |
              (1UL << 13) | /* weekday not tracked, just needs a nonzero placeholder */
              ((uint32_t)bin_to_bcd(dt->month) << 8) |
              (uint32_t)bin_to_bcd(dt->day);

    RTC->ISR &= ~RTC_ISR_INIT;
    rtc_lock();
}

void rtc_get_datetime(rtc_datetime_t *dt) {
    uint32_t tr1, dr1, tr2, dr2;

    /* Read twice and compare: guards against catching TR/DR mid-update
     * on the exact edge of a second tick (classic RTC read race). */
    do {
        tr1 = RTC->TR;
        dr1 = RTC->DR;
        tr2 = RTC->TR;
        dr2 = RTC->DR;
    } while (tr1 != tr2 || dr1 != dr2);

    dt->sec = bcd_to_bin((uint8_t)(tr1 & 0x7F));
    dt->min = bcd_to_bin((uint8_t)((tr1 >> 8) & 0x7F));
    dt->hour = bcd_to_bin((uint8_t)((tr1 >> 16) & 0x3F));

    dt->day = bcd_to_bin((uint8_t)(dr1 & 0x3F));
    dt->month = bcd_to_bin((uint8_t)((dr1 >> 8) & 0x1F));
    dt->year = (uint16_t)(2000 + bcd_to_bin((uint8_t)((dr1 >> 16) & 0xFF)));
}
