#include "task_led.h"
#include "stm32g474xx.h"
#include "systick.h"
#include "task_sd.h"
#include "task_logger.h"
#include "gps_neo8m.h"

#define GREEN_PIN 6 /* PC6 */
#define RED_PIN   8 /* PC8 */

#define GREEN_SLOW_FLASH_PERIOD_MS 1000UL /* toggle every 1s -> 1 flash / 2s */
#define RED_1HZ_PERIOD_MS 500UL           /* 1 flash/s */
#define RED_2HZ_PERIOD_MS 250UL           /* 2 flash/s */
#define RED_3HZ_PERIOD_MS 167UL           /* ~3 flash/s (334ms cycle) */

typedef enum { GREEN_OFF, GREEN_SOLID, GREEN_SLOW_FLASH } green_pattern_t;
typedef enum { RED_OFF, RED_SOLID, RED_1HZ, RED_2HZ, RED_3HZ } red_pattern_t;

static uint32_t last_seen_write_errors = 0;
static int sd_fault_latched = 0;
static int i2c_fault_latched = 0;
static battery_state_t battery_state = BATTERY_OK;

static green_pattern_t green_pattern = GREEN_OFF;
static uint32_t green_last_toggle_ms = 0;
static int green_on = 0;

static red_pattern_t red_pattern = RED_OFF;
static uint32_t red_last_toggle_ms = 0;
static int red_on = 0;

static void gpio_set(GPIO_TypeDef *port, uint32_t pin, int level) {
    port->BSRR = level ? (1UL << pin) : (1UL << (pin + 16));
}

/* Toggles *on whenever period_ms has elapsed since *last_toggle_ms and
 * returns the (possibly just-flipped) level - shared by every blink
 * pattern below regardless of rate. */
static int blink_toggle(uint32_t *last_toggle_ms, uint32_t period_ms, uint32_t now, int *on) {
    if ((int32_t)(now - *last_toggle_ms) >= (int32_t)period_ms) {
        *last_toggle_ms = now;
        *on = !*on;
    }
    return *on;
}

void Task_LED_Init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    GPIOC->MODER &= ~((3UL << (GREEN_PIN * 2)) | (3UL << (RED_PIN * 2)));
    GPIOC->MODER |= (1UL << (GREEN_PIN * 2)) | (1UL << (RED_PIN * 2));
    gpio_set(GPIOC, GREEN_PIN, 0);
    gpio_set(GPIOC, RED_PIN, 0);
}

void Task_LED(void) {
    uint32_t now = SysTick_GetMillis();

    /* Latch on any NEW write error since the last ack - comparing
     * against the raw (monotonic, never-reset) counter directly would
     * make acking impossible without also losing that counter's
     * diagnostic value elsewhere (status dashboard). */
    uint32_t we = Task_Logger_Write_Errors();
    if (we > last_seen_write_errors) sd_fault_latched = 1;
    last_seen_write_errors = we;

    /* --- green: logging health (see task_led.h for why battery state
     * isn't folded in here) --- */
    green_pattern_t new_green;
    if (Task_SD_Get_State() != SD_STATE_MOUNTED) {
        new_green = GREEN_OFF;
    } else if (gps_get_fix()->fix_valid) {
        new_green = GREEN_SOLID;
    } else {
        new_green = GREEN_SLOW_FLASH;
    }
    if (new_green != green_pattern) {
        green_pattern = new_green;
        green_last_toggle_ms = now;
        green_on = 0; /* start each new pattern from "off" so a switch never looks like a stray flash */
    }

    int green_level;
    if (green_pattern == GREEN_SOLID) {
        green_level = 1;
    } else if (green_pattern == GREEN_SLOW_FLASH) {
        green_level = blink_toggle(&green_last_toggle_ms, GREEN_SLOW_FLASH_PERIOD_MS, now, &green_on);
    } else {
        green_level = 0;
    }
    gpio_set(GPIOC, GREEN_PIN, green_level);

    /* --- red: worst-fault-wins priority, see task_led.h --- */
    red_pattern_t new_red;
    if (battery_state == BATTERY_CRITICAL) new_red = RED_SOLID;
    else if (sd_fault_latched) new_red = RED_1HZ;
    else if (i2c_fault_latched) new_red = RED_2HZ;
    else if (battery_state == BATTERY_LOW) new_red = RED_3HZ;
    else new_red = RED_OFF;

    if (new_red != red_pattern) {
        red_pattern = new_red;
        red_last_toggle_ms = now;
        red_on = 0;
    }

    int red_level;
    switch (red_pattern) {
        case RED_SOLID: red_level = 1; break;
        case RED_1HZ: red_level = blink_toggle(&red_last_toggle_ms, RED_1HZ_PERIOD_MS, now, &red_on); break;
        case RED_2HZ: red_level = blink_toggle(&red_last_toggle_ms, RED_2HZ_PERIOD_MS, now, &red_on); break;
        case RED_3HZ: red_level = blink_toggle(&red_last_toggle_ms, RED_3HZ_PERIOD_MS, now, &red_on); break;
        default: red_level = 0; break;
    }
    gpio_set(GPIOC, RED_PIN, red_level);
}

void Task_LED_Ack_Faults(void) {
    sd_fault_latched = 0;
    i2c_fault_latched = 0;
}

void Task_LED_Report_I2C_Fault(void) {
    i2c_fault_latched = 1;
}

void Task_LED_Set_Battery_State(battery_state_t state) {
    battery_state = state;
}

const char *Task_LED_Green_Str(void) {
    switch (green_pattern) {
        case GREEN_SOLID: return "solid (logging, GPS fix)";
        case GREEN_SLOW_FLASH: return "slow flash (logging, no GPS fix)";
        default: return "off (not logging)";
    }
}

const char *Task_LED_Red_Str(void) {
    switch (red_pattern) {
        case RED_SOLID: return "solid (battery critical)";
        case RED_1HZ: return "1 flash/s (SD write error - latched, press 'A' to ack)";
        case RED_2HZ: return "2 flash/s (I2C fault - latched, press 'A' to ack)";
        case RED_3HZ: return "3 flash/s (battery low)";
        default: return "off (no fault)";
    }
}
