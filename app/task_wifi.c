#include "task_wifi.h"
#include "stm32g474xx.h"
#include "systick.h"
#include "esp8266.h"

#define WIFI_UART USART3
#define WIFI_BAUD 115200

#define BRINGUP_TIMEOUT_MS 2000UL
#define BRINGUP_RETRY_DELAY_MS 3000UL

typedef enum { WIFI_BRINGUP_SEND, WIFI_BRINGUP_WAIT, WIFI_BRINGUP_DONE } wifi_state_t;

static wifi_state_t state = WIFI_BRINGUP_SEND;
static uint32_t next_action_ms = 0;

void Task_WiFi_Init(void) {
    esp8266_init(WIFI_UART, WIFI_BAUD);
    state = WIFI_BRINGUP_SEND;
    next_action_ms = 0;
}

void Task_WiFi(void) {
    esp8266_poll(WIFI_UART);

    uint32_t now = SysTick_GetMillis();

    switch (state) {
        case WIFI_BRINGUP_SEND:
            if ((int32_t)(now - next_action_ms) >= 0) {
                esp8266_send(WIFI_UART, "AT", BRINGUP_TIMEOUT_MS);
                state = WIFI_BRINGUP_WAIT;
            }
            break;

        case WIFI_BRINGUP_WAIT: {
            esp_status_t st = esp8266_get_status();
            if (st == ESP_STATUS_OK) {
                state = WIFI_BRINGUP_DONE;
            } else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                next_action_ms = now + BRINGUP_RETRY_DELAY_MS;
                state = WIFI_BRINGUP_SEND;
            }
            break;
        }

        case WIFI_BRINGUP_DONE:
            break;
    }
}

const char *Task_WiFi_Status_Str(void) {
    switch (state) {
        case WIFI_BRINGUP_SEND: return "bring-up: sending AT...";
        case WIFI_BRINGUP_WAIT: return "bring-up: waiting for response...";
        case WIFI_BRINGUP_DONE: return "bring-up OK (module responding)";
    }
    return "unknown";
}
