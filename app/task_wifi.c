#include "task_wifi.h"
#include "stm32g474xx.h"
#include "systick.h"
#include "esp8266.h"
#include "gps_neo8m.h"
#include "strbuf.h"
#include "wifi_config.h"

#define WIFI_UART USART3
#define WIFI_BAUD 115200

#define SHORT_TIMEOUT_MS 2000UL
#define JOIN_TIMEOUT_MS 20000UL   /* CWJAP: DHCP + auth can genuinely take a while */
#define MQTT_CONN_TIMEOUT_MS 10000UL
#define PUBLISH_TIMEOUT_MS 3000UL
#define RETRY_DELAY_MS 3000UL
#define PUBLISH_INTERVAL_MS 5000UL

typedef enum {
    WIFI_BRINGUP_SEND,
    WIFI_BRINGUP_WAIT,
    WIFI_CWMODE_SEND,
    WIFI_CWMODE_WAIT,
    WIFI_CWJAP_SEND,
    WIFI_CWJAP_WAIT,
    WIFI_MQTTCFG_SEND,
    WIFI_MQTTCFG_WAIT,
    WIFI_MQTTCONN_SEND,
    WIFI_MQTTCONN_WAIT,
    WIFI_CONNECTED,
    WIFI_PUBLISH_SEND,
    WIFI_PUBLISH_WAIT
} wifi_state_t;

static wifi_state_t state = WIFI_BRINGUP_SEND;
static uint32_t next_action_ms = 0;
static uint32_t last_publish_ms = 0;
static uint32_t publish_count = 0;
static char cmd_buf[220];

void Task_WiFi_Init(void) {
    esp8266_init(WIFI_UART, WIFI_BAUD);
    state = WIFI_BRINGUP_SEND;
    next_action_ms = 0;
}

static int due(uint32_t now) {
    return (int32_t)(now - next_action_ms) >= 0;
}

static void retry_at(wifi_state_t send_state, uint32_t now) {
    next_action_ms = now + RETRY_DELAY_MS;
    state = send_state;
}

static void build_gps_json(strbuf_t *sb) {
    const gps_fix_t *fix = gps_get_fix();
    strbuf_str(sb, "{\"fix\":");
    strbuf_uint(sb, fix->fix_valid);
    strbuf_str(sb, ",\"sats\":");
    strbuf_uint(sb, fix->num_sats);
    strbuf_str(sb, ",\"lat\":");
    strbuf_fixed(sb, fix->lat_e7, 7);
    strbuf_str(sb, ",\"lon\":");
    strbuf_fixed(sb, fix->lon_e7, 7);
    strbuf_str(sb, ",\"alt_m\":");
    strbuf_fixed(sb, fix->alt_dm, 1);
    strbuf_str(sb, ",\"speed_kmh\":");
    strbuf_fixed(sb, fix->speed_kmh_x10, 1);
    strbuf_str(sb, "}");
}

void Task_WiFi(void) {
    esp8266_poll(WIFI_UART);
    uint32_t now = SysTick_GetMillis();
    esp_status_t st;
    strbuf_t sb;

    switch (state) {
        case WIFI_BRINGUP_SEND:
            if (!due(now)) break;
            esp8266_send(WIFI_UART, "AT", SHORT_TIMEOUT_MS);
            state = WIFI_BRINGUP_WAIT;
            break;

        case WIFI_BRINGUP_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK) state = WIFI_CWMODE_SEND;
            else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) retry_at(WIFI_BRINGUP_SEND, now);
            break;

        case WIFI_CWMODE_SEND:
            esp8266_send(WIFI_UART, "AT+CWMODE=1", SHORT_TIMEOUT_MS);
            state = WIFI_CWMODE_WAIT;
            break;

        case WIFI_CWMODE_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK) state = WIFI_CWJAP_SEND;
            else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) retry_at(WIFI_CWMODE_SEND, now);
            break;

        case WIFI_CWJAP_SEND:
            strbuf_init(&sb, cmd_buf, sizeof(cmd_buf));
            strbuf_str(&sb, "AT+CWJAP=\"");
            strbuf_str_escaped(&sb, WIFI_SSID);
            strbuf_str(&sb, "\",\"");
            strbuf_str_escaped(&sb, WIFI_PASSWORD);
            strbuf_str(&sb, "\"");
            esp8266_send(WIFI_UART, cmd_buf, JOIN_TIMEOUT_MS);
            state = WIFI_CWJAP_WAIT;
            break;

        case WIFI_CWJAP_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK) state = WIFI_MQTTCFG_SEND;
            else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) retry_at(WIFI_CWJAP_SEND, now);
            break;

        case WIFI_MQTTCFG_SEND:
            strbuf_init(&sb, cmd_buf, sizeof(cmd_buf));
            strbuf_str(&sb, "AT+MQTTUSERCFG=0,1,\"");
            strbuf_str_escaped(&sb, MQTT_CLIENT_ID);
            strbuf_str(&sb, "\",\"");
            strbuf_str_escaped(&sb, MQTT_USER);
            strbuf_str(&sb, "\",\"");
            strbuf_str_escaped(&sb, MQTT_PASS);
            strbuf_str(&sb, "\",0,0,\"\"");
            esp8266_send(WIFI_UART, cmd_buf, SHORT_TIMEOUT_MS);
            state = WIFI_MQTTCFG_WAIT;
            break;

        case WIFI_MQTTCFG_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK) state = WIFI_MQTTCONN_SEND;
            else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) retry_at(WIFI_MQTTCFG_SEND, now);
            break;

        case WIFI_MQTTCONN_SEND:
            strbuf_init(&sb, cmd_buf, sizeof(cmd_buf));
            strbuf_str(&sb, "AT+MQTTCONN=0,\"");
            strbuf_str_escaped(&sb, MQTT_HOST);
            strbuf_str(&sb, "\",");
            strbuf_uint(&sb, MQTT_PORT);
            strbuf_str(&sb, ",0");
            esp8266_send(WIFI_UART, cmd_buf, MQTT_CONN_TIMEOUT_MS);
            state = WIFI_MQTTCONN_WAIT;
            break;

        case WIFI_MQTTCONN_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK) {
                state = WIFI_CONNECTED;
                last_publish_ms = now;
            } else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                retry_at(WIFI_MQTTCONN_SEND, now);
            }
            break;

        case WIFI_CONNECTED:
            if ((int32_t)(now - last_publish_ms) >= (int32_t)PUBLISH_INTERVAL_MS) {
                state = WIFI_PUBLISH_SEND;
            }
            break;

        case WIFI_PUBLISH_SEND: {
            char json_buf[96];
            strbuf_t json;
            strbuf_init(&json, json_buf, sizeof(json_buf));
            build_gps_json(&json);

            strbuf_init(&sb, cmd_buf, sizeof(cmd_buf));
            strbuf_str(&sb, "AT+MQTTPUB=0,\"");
            strbuf_str_escaped(&sb, MQTT_TOPIC);
            strbuf_str(&sb, "\",\"");
            strbuf_str_escaped(&sb, json_buf);
            strbuf_str(&sb, "\",0,0");
            esp8266_send(WIFI_UART, cmd_buf, PUBLISH_TIMEOUT_MS);
            state = WIFI_PUBLISH_WAIT;
            break;
        }

        case WIFI_PUBLISH_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK) {
                publish_count++;
                last_publish_ms = now;
                state = WIFI_CONNECTED;
            } else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                /* Publish failing usually means the MQTT/TCP link dropped -
                 * reconnect rather than just retrying the publish. */
                retry_at(WIFI_MQTTCONN_SEND, now);
            }
            break;
    }
}

const char *Task_WiFi_Status_Str(void) {
    switch (state) {
        case WIFI_BRINGUP_SEND:
        case WIFI_BRINGUP_WAIT:
            return "bring-up (AT)";
        case WIFI_CWMODE_SEND:
        case WIFI_CWMODE_WAIT:
            return "setting station mode";
        case WIFI_CWJAP_SEND:
        case WIFI_CWJAP_WAIT:
            return "joining WiFi";
        case WIFI_MQTTCFG_SEND:
        case WIFI_MQTTCFG_WAIT:
            return "configuring MQTT client";
        case WIFI_MQTTCONN_SEND:
        case WIFI_MQTTCONN_WAIT:
            return "connecting to MQTT broker";
        case WIFI_CONNECTED:
            return "connected, idle";
        case WIFI_PUBLISH_SEND:
        case WIFI_PUBLISH_WAIT:
            return "publishing";
    }
    return "unknown";
}

uint32_t Task_WiFi_Publish_Count(void) {
    return publish_count;
}
