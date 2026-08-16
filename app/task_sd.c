#include "task_sd.h"
#include "task_logger.h"
#include "stm32g474xx.h"
#include "systick.h"
#include "ff.h"
#include "uart.h"
#include "strbuf.h"

#define CONSOLE_UART USART2

#define BUTTON_PIN 9 /* PC9 - SD eject button, external 10k pull-up to 3.3V, idle high, pressed low */
#define DEBOUNCE_MS 50UL

static FATFS fs;
static sd_state_t state = SD_STATE_NOT_MOUNTED;
static char status_str[80];
static uint32_t free_kb = 0;

static int raw_level_last = 1;   /* 1 = idle/released (matches external pull-up default) */
static uint32_t level_change_ms = 0;
static int confirmed_pressed = 0;

#define SELFTEST_PATH "SELFTST.TXT"
#define SELFTEST_SIG "EkoSonda SD self-test\n"

static void report(const char *msg) {
    uart_write_str(CONSOLE_UART, "SD: ");
    uart_write_str(CONSOLE_UART, msg);
    uart_write_str(CONSOLE_UART, "\r\n");
}

/* Mount, write a small known file, read it back and compare - this is
 * the SD-card equivalent of the I2C WHO_AM_I check: prove the whole
 * chain (SPI clocking, card init, FAT structures) actually works before
 * trusting it with real log data. Shared by the boot-time bring-up and
 * the eject button's re-mount path. */
static void try_mount(void) {
    strbuf_t sb;

    FRESULT fr = f_mount(&fs, "", 1); /* opt=1: mount now, not lazily on first access */
    if (fr != FR_OK) {
        strbuf_init(&sb, status_str, sizeof(status_str));
        strbuf_str(&sb, "mount failed (FRESULT=");
        strbuf_uint(&sb, (uint32_t)fr);
        strbuf_str(&sb, ")");
        state = SD_STATE_NOT_MOUNTED;
        report(status_str);
        return;
    }

    FIL fil;
    UINT bw = 0, br = 0;
    char readback[sizeof(SELFTEST_SIG) + 4] = {0};

    fr = f_open(&fil, SELFTEST_PATH, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        fr = f_write(&fil, SELFTEST_SIG, sizeof(SELFTEST_SIG) - 1, &bw);
        f_close(&fil);
    }
    if (fr == FR_OK && bw == sizeof(SELFTEST_SIG) - 1) {
        fr = f_open(&fil, SELFTEST_PATH, FA_READ);
        if (fr == FR_OK) {
            fr = f_read(&fil, readback, sizeof(SELFTEST_SIG) - 1, &br);
            f_close(&fil);
        }
    }

    int ok = (fr == FR_OK) && (br == sizeof(SELFTEST_SIG) - 1)
           && (0 == __builtin_memcmp(readback, SELFTEST_SIG, sizeof(SELFTEST_SIG) - 1));

    strbuf_init(&sb, status_str, sizeof(status_str));
    if (!ok) {
        strbuf_str(&sb, "self-test failed (FRESULT=");
        strbuf_uint(&sb, (uint32_t)fr);
        strbuf_str(&sb, ")");
        state = SD_STATE_NOT_MOUNTED;
        report(status_str);
        return;
    }

    DWORD free_clusters;
    FATFS *fs_ptr;
    free_kb = 0;
    if (f_getfree("", &free_clusters, &fs_ptr) == FR_OK) {
        /* free bytes = free_clusters * sectors/cluster * bytes/sector */
        free_kb = (uint32_t)((uint64_t)free_clusters * fs_ptr->csize * 512U / 1024U);
    }

    strbuf_str(&sb, "mounted OK, ");
    strbuf_uint(&sb, free_kb);
    strbuf_str(&sb, " KB free");
    state = SD_STATE_MOUNTED;
    report(status_str);
}

static void button_gpio_init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    GPIOC->MODER &= ~(3UL << (BUTTON_PIN * 2)); /* 00 = input - external pull-up already present, no internal pull needed */
}

static void handle_button_press(void) {
    if (state == SD_STATE_MOUNTED) {
        Task_Logger_Prepare_For_Eject(); /* sync + close BEFORE unmounting, so removal is safe */
        f_unmount("");
        state = SD_STATE_NOT_MOUNTED;
        free_kb = 0;
        report("ejected - safe to remove card");
    } else {
        try_mount();
        if (state == SD_STATE_MOUNTED) {
            Task_Logger_Resume(); /* fresh card (or the same one reinserted) - start a new session file */
        }
    }
}

void Task_SD_Init(void) {
    button_gpio_init();
    try_mount();
}

void Task_SD_Button(void) {
    uint32_t now = SysTick_GetMillis();
    int raw = (GPIOC->IDR & (1UL << BUTTON_PIN)) ? 1 : 0; /* 1 = released, 0 = pressed */

    if (raw != raw_level_last) {
        raw_level_last = raw;
        level_change_ms = now;
    }

    if ((now - level_change_ms) >= DEBOUNCE_MS) {
        int pressed_now = (raw == 0);
        if (pressed_now && !confirmed_pressed) {
            confirmed_pressed = 1;
            handle_button_press();
        } else if (!pressed_now) {
            confirmed_pressed = 0;
        }
    }
}

sd_state_t Task_SD_Get_State(void) {
    return state;
}

const char *Task_SD_Status_Str(void) {
    return status_str[0] ? status_str : "not run";
}

uint32_t Task_SD_Free_KB(void) {
    return free_kb;
}
