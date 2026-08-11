#include "task_battery.h"
#include "i2c.h"
#include "ina3221.h"
#include "task_led.h"
#include "uart.h"
#include "strbuf.h"

#define CONSOLE_UART USART2
#define BATTERY_I2C I2C1

/* Pack is 1S5P INR18650MJ1 - 1S means pack voltage IS single-cell
 * voltage (no series scaling needed), 5P multiplies capacity/current
 * headroom by 5 (5 x 3500mAh = 17500mAh), doesn't change the voltage
 * curve. Thresholds are SOC-based (computed from the OCV table below),
 * not raw voltage, so they stay meaningful if the table is ever
 * recalibrated. */
#define BATTERY_LOW_PCT      10 /* early warning */
#define BATTERY_CRITICAL_PCT 3  /* margin before the ~3.0V/0% practical cutoff */

#define PACK_CAPACITY_MAH 17500UL /* 1S5P, 5 x 3500mAh INR18650MJ1 cells */

#define I2C_FAULT_THRESHOLD 3 /* consecutive read failures before latching - a single glitch shouldn't latch */

static battery_hw_state_t state = BATTERY_HW_NOT_FOUND;
static uint16_t vbat_mv = 0;
static int16_t ibat_ma = 0;
static uint8_t soc_pct = 0;
static int consecutive_read_failures = 0;

static void report(const char *msg) {
    uart_write_str(CONSOLE_UART, "INA3221: ");
    uart_write_str(CONSOLE_UART, msg);
    uart_write_str(CONSOLE_UART, "\r\n");
}

/* Temporary bring-up diagnostic: scans the whole 7-bit address space
 * (0x08-0x77 - the reserved ranges below/above are never valid device
 * addresses) and prints anything that ACKs, to tell "wrong address" and
 * "totally dead bus" apart. Not meant to stay wired into boot forever. */
static void scan_bus(void) {
    char msg[16];
    strbuf_t sb;
    int found_any = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_probe(BATTERY_I2C, addr)) {
            found_any = 1;
            strbuf_init(&sb, msg, sizeof(msg));
            strbuf_str(&sb, "  found addr (decimal) ");
            strbuf_uint(&sb, addr); /* decimal is fine here, just need to see *something* respond - 0x40 = 64 */
            report(msg);
        }
    }
    if (!found_any) report("bus scan: nothing acked any address 0x08-0x77 - bus is dead (power/wiring/pull-ups/clock), not just wrong address");
}

/* Rough OCV-based SOC estimate for a 1S NMC Li-ion cell (INR18650MJ1) -
 * piecewise-linear interpolation over a generic discharge curve. This is
 * NOT calibrated against MJ1's actual datasheet curve and does NOT
 * compensate for voltage sag under load (a loaded reading looks more
 * "discharged" than true rest SOC would) - good enough to drive the LED
 * indicator and a rough record_t field for now. Proper SOC/OCV
 * calibration + coulomb counting is called out as later hardening work
 * in the project brief's own build order, not something this step needs
 * to solve. */
typedef struct { uint16_t mv; uint8_t pct; } ocv_point_t;

static const ocv_point_t OCV_TABLE[] = {
    {3000, 0},  {3410, 5},  {3490, 10}, {3580, 15}, {3610, 20}, {3630, 25},
    {3650, 30}, {3680, 35}, {3700, 40}, {3730, 45}, {3760, 50}, {3790, 55},
    {3820, 60}, {3870, 65}, {3920, 70}, {3970, 75}, {4020, 80}, {4080, 85},
    {4110, 90}, {4150, 95}, {4200, 100},
};
#define OCV_TABLE_LEN (sizeof(OCV_TABLE) / sizeof(OCV_TABLE[0]))

static uint8_t estimate_soc_pct(uint16_t mv) {
    if (mv <= OCV_TABLE[0].mv) return OCV_TABLE[0].pct;
    if (mv >= OCV_TABLE[OCV_TABLE_LEN - 1].mv) return OCV_TABLE[OCV_TABLE_LEN - 1].pct;

    for (uint32_t i = 0; i + 1 < OCV_TABLE_LEN; i++) {
        uint16_t lo_mv = OCV_TABLE[i].mv, hi_mv = OCV_TABLE[i + 1].mv;
        if (mv >= lo_mv && mv <= hi_mv) {
            uint32_t lo_pct = OCV_TABLE[i].pct, hi_pct = OCV_TABLE[i + 1].pct;
            uint32_t span_mv = hi_mv - lo_mv;
            uint32_t span_pct = hi_pct - lo_pct;
            uint32_t offset_mv = (uint32_t)mv - lo_mv;
            return (uint8_t)(lo_pct + (offset_mv * span_pct) / span_mv);
        }
    }
    return 0; /* unreachable given the bounds checks above */
}

void Task_Battery_Init(void) {
    i2c_init(BATTERY_I2C, I2C_TIMING_400KHZ_170MHZ);

    if (!ina3221_probe(BATTERY_I2C)) {
        state = BATTERY_HW_NOT_FOUND;
        report("probe failed (wrong/missing manufacturer or die ID) - check wiring (SCL=PB8, SDA=PB9, addr=0x40)");
        scan_bus();
        return;
    }

    ina3221_init(BATTERY_I2C);

    if (!ina3221_read_battery(BATTERY_I2C, &vbat_mv, &ibat_ma)) {
        state = BATTERY_HW_NOT_FOUND;
        report("probe OK but read failed");
        return;
    }

    soc_pct = estimate_soc_pct(vbat_mv);

    char msg[80];
    strbuf_t sb;
    strbuf_init(&sb, msg, sizeof(msg));
    strbuf_str(&sb, "OK, vbat=");
    strbuf_uint(&sb, vbat_mv);
    strbuf_str(&sb, "mV  ibat=");
    strbuf_int(&sb, ibat_ma);
    strbuf_str(&sb, "mA  soc~=");
    strbuf_uint(&sb, soc_pct);
    strbuf_str(&sb, "%");
    state = BATTERY_HW_OK;
    report(msg);
}

void Task_Battery(void) {
    if (state != BATTERY_HW_OK) return; /* never found at boot - nothing to poll */

    if (!ina3221_read_battery(BATTERY_I2C, &vbat_mv, &ibat_ma)) {
        consecutive_read_failures++;
        if (consecutive_read_failures >= I2C_FAULT_THRESHOLD) {
            Task_LED_Report_I2C_Fault();
        }
        return;
    }
    consecutive_read_failures = 0;

    soc_pct = estimate_soc_pct(vbat_mv);

    if (soc_pct <= BATTERY_CRITICAL_PCT) {
        Task_LED_Set_Battery_State(BATTERY_CRITICAL);
    } else if (soc_pct <= BATTERY_LOW_PCT) {
        Task_LED_Set_Battery_State(BATTERY_LOW);
    } else {
        Task_LED_Set_Battery_State(BATTERY_OK);
    }
}

battery_hw_state_t Task_Battery_Get_State(void) { return state; }
uint16_t Task_Battery_Vbat_mV(void) { return vbat_mv; }
int16_t Task_Battery_Ibat_mA(void) { return ibat_ma; }
uint8_t Task_Battery_Soc_Pct(void) { return soc_pct; }

/* Coulomb-counted-ish estimate: (remaining or missing capacity, from
 * SOC x pack capacity) / current draw. Simple and integer-only, but a
 * genuinely rough estimate - it assumes constant current and a fixed
 * pack capacity, neither of which holds in practice (discharge current
 * varies with load, and charge current tapers off near full under
 * CC-CV charging, so "time to full" will read optimistic once the
 * charger starts tapering). Same spirit as the OCV/SOC table above:
 * good enough for a dashboard number, not a calibrated fuel gauge.
 * Returns 0 (caller checks Task_Battery_Ibat_mA() == 0 to distinguish
 * "genuinely 0 minutes" from "no current, can't estimate"). */
uint32_t Task_Battery_Minutes_Left(void) {
    if (ibat_ma == 0) return 0;

    uint32_t abs_ma = (uint32_t)(ibat_ma < 0 ? -ibat_ma : ibat_ma);
    uint32_t relevant_mah = (ibat_ma > 0)
        ? (PACK_CAPACITY_MAH * soc_pct) / 100        /* discharging: time to empty */
        : (PACK_CAPACITY_MAH * (100 - soc_pct)) / 100; /* charging: time to full */

    return (relevant_mah * 60UL) / abs_ma;
}
