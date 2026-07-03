/**
 * @file  esp8266.c
 * @brief ESP8266-01S AT Command Driver.
 *        Requires ESP-AT firmware >= 2.0 for MQTT AT commands.
 *        USART3: PB10=TX, PB11=RX, 115200 baud, AF7
 */
#include "esp8266.h"
#include "stm32g474xx.h"
#include <string.h>
#include <stdio.h>

/* ── Internal State ─────────────────────────────────────────────────────── */
static ESP_State_t esp_state = ESP_STATE_RESET;
static char rx_buf[256];
static uint16_t rx_idx = 0;
static uint32_t state_timer = 0;   /* Ticks since state entered */
static bool mqtt_ready = false;

/* ── Low-level USART3 TX ────────────────────────────────────────────────── */
static void ESP_Send(const char *cmd) {
    while (*cmd) {
        while (!(USART3->ISR & USART_ISR_TXE));
        USART3->TDR = (uint8_t)(*cmd++);
    }
}

/* ── Read one char from USART3 RX (non-blocking, returns 0 if empty) ────── */
static uint8_t ESP_ReadChar(void) {
    if (USART3->ISR & (USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE))
        USART3->ICR = 0x1F;
    if (USART3->ISR & USART_ISR_RXNE)
        return (uint8_t)USART3->RDR;
    return 0;
}

/* ── Drain all pending RX chars into rx_buf until newline or full ────────── */
static bool ESP_ReadLine(void) {
    uint8_t c;
    while ((c = ESP_ReadChar()) != 0) {
        if (c == '\r') continue;
        if (c == '\n') {
            rx_buf[rx_idx] = '\0';
            rx_idx = 0;
            return (rx_buf[0] != '\0');   /* true = non-empty line ready */
        }
        if (rx_idx < sizeof(rx_buf) - 1)
            rx_buf[rx_idx++] = c;
    }
    return false;
}

/* ── Public: Hardware Init ──────────────────────────────────────────────── */
void ESP_Init(void) {
    /* Clocks: GPIOB already enabled in main.c; enable USART3 */
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART3EN;

    /* PB10: AF7 (USART3_TX) */
    GPIOB->MODER  &= ~(3UL << (10 * 2));
    GPIOB->MODER  |=  (2UL << (10 * 2));   /* Alternate function */
    GPIOB->AFR[1] &= ~(0xFUL << ((10 - 8) * 4));
    GPIOB->AFR[1] |=  (7UL   << ((10 - 8) * 4));   /* AF7 */
    GPIOB->OSPEEDR |= (3UL << (10 * 2));   /* High speed */

    /* PB11: AF7 (USART3_RX) */
    GPIOB->MODER  &= ~(3UL << (11 * 2));
    GPIOB->MODER  |=  (2UL << (11 * 2));
    GPIOB->AFR[1] &= ~(0xFUL << ((11 - 8) * 4));
    GPIOB->AFR[1] |=  (7UL   << ((11 - 8) * 4));
    GPIOB->PUPDR  |=  (1UL << (11 * 2));   /* Pull-up on RX */

    /* USART3: 115200 baud @ 170MHz
       BRR = 170000000 / 115200 = 1476 */
    USART3->BRR = 1476;
    USART3->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;

    esp_state  = ESP_STATE_RESET;
    state_timer = 0;
    mqtt_ready  = false;
}

/* ── Public: State Machine Task (call every 100ms from scheduler) ────────── */
void ESP_Task(void) {
    state_timer++;

    /* Always drain RX — responses can arrive any time */
    bool line_ready = ESP_ReadLine();

    switch (esp_state) {

    case ESP_STATE_RESET:
        ESP_Send("AT+RST\r\n");
        esp_state   = ESP_STATE_WAIT_READY;
        state_timer = 0;
        break;

    case ESP_STATE_WAIT_READY:
        if (line_ready && strstr(rx_buf, "ready")) {
            esp_state = ESP_STATE_SET_MODE;
        } else if (state_timer > 50) {   /* 5s timeout */
            esp_state = ESP_STATE_RESET;
        }
        break;

    case ESP_STATE_SET_MODE:
        ESP_Send("AT+CWMODE=1\r\n");
        esp_state   = ESP_STATE_WAIT_MODE_OK;
        state_timer = 0;
        break;

    case ESP_STATE_WAIT_MODE_OK:
        if (line_ready) {
            if (strstr(rx_buf, "OK")) {
                esp_state = ESP_STATE_CONNECT_WIFI;
                state_timer = 0;
            } else if (strstr(rx_buf, "ERROR")) {
                esp_state = ESP_STATE_ERROR;
            }
        } else if (state_timer > 50) {
            esp_state = ESP_STATE_ERROR;
        }
        break;

    case ESP_STATE_CONNECT_WIFI: {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n",
                 WIFI_SSID, WIFI_PASS);
        ESP_Send(cmd);
        esp_state   = ESP_STATE_WAIT_WIFI;
        state_timer = 0;
        break;
    }

    case ESP_STATE_WAIT_WIFI:
        if (line_ready) {
            if (strstr(rx_buf, "WIFI GOT IP") || strstr(rx_buf, "OK")) {
                esp_state = ESP_STATE_MQTT_CFG;
                state_timer = 0;
            } else if (strstr(rx_buf, "FAIL") || strstr(rx_buf, "ERROR")) {
                esp_state = ESP_STATE_ERROR;
            }
        } else if (state_timer > 200) {   /* 20s timeout */
            esp_state = ESP_STATE_ERROR;
        }
        break;

    case ESP_STATE_MQTT_CFG: {
        char cmd[256];
        /* scheme=1 (MQTT over TCP with username/password) */
        snprintf(cmd, sizeof(cmd), "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n", 
                 MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
        ESP_Send(cmd);
        esp_state   = ESP_STATE_WAIT_MQTT_CFG;
        state_timer = 0;
        break;
    }

    case ESP_STATE_WAIT_MQTT_CFG:
        if (line_ready) {
            if (strstr(rx_buf, "OK")) {
                esp_state = ESP_STATE_MQTT_CONNECT;
                state_timer = 0;
            } else if (strstr(rx_buf, "ERROR")) {
                esp_state = ESP_STATE_ERROR;
            }
        } else if (state_timer > 100) {
            esp_state = ESP_STATE_ERROR;
        }
        break;

    case ESP_STATE_MQTT_CONNECT: {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "AT+MQTTCONN=0,\"%s\",%d,0\r\n",
                 MQTT_BROKER_IP, MQTT_BROKER_PORT);
        ESP_Send(cmd);
        esp_state   = ESP_STATE_WAIT_MQTT;
        state_timer = 0;
        break;
    }

    case ESP_STATE_WAIT_MQTT:
        if (line_ready) {
            if (strstr(rx_buf, "OK") || strstr(rx_buf, "ALREADY CONNECTED")) {
                mqtt_ready = true;
                esp_state = ESP_STATE_READY;
                state_timer = 0;
            } else if (strstr(rx_buf, "ERROR")) {
                esp_state = ESP_STATE_ERROR;
            }
        } else if (state_timer > 150) {   /* 15s timeout */
            esp_state = ESP_STATE_ERROR;
        }
        break;

    case ESP_STATE_READY:
        if (line_ready && strstr(rx_buf, "MQTTDISCONNECTED")) {
            mqtt_ready = false;
            esp_state  = ESP_STATE_MQTT_CONNECT;
            state_timer = 0;
        }
        break;

    case ESP_STATE_ERROR:
        mqtt_ready = false;
        if (state_timer > 300) {
            esp_state   = ESP_STATE_RESET;
            state_timer = 0;
        }
        break;
    }
}

bool ESP_IsReady(void) { return mqtt_ready; }

void ESP_Publish_SensorData(float t, float p, float h, float g) {
    if (!mqtt_ready) return;
    char payload[128];
    
    int t_i = (int)t, t_f = (int)((t > t_i ? t - t_i : t_i - t) * 100);
    int p_i = (int)p, p_f = (int)((p > p_i ? p - p_i : p_i - p) * 100);
    int h_i = (int)h, h_f = (int)((h > h_i ? h - h_i : h_i - h) * 100);
    int g_i = (int)g, g_f = (int)((g > g_i ? g - g_i : g_i - g) * 100);

    snprintf(payload, sizeof(payload), "{\\\"t\\\":%d.%02d,\\\"p\\\":%d.%02d,\\\"h\\\":%d.%02d,\\\"g\\\":%d.%02d}",
             t_i, t_f, p_i, p_f, h_i, h_f, g_i, g_f);

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+MQTTPUB=0,\"%s\",\"%s\",1,0\r\n",
             MQTT_TOPIC_DATA, payload);
    ESP_Send(cmd);
}