#include "task_wifi.h"
#include "stm32g474xx.h"
#include "systick.h"
#include "uart.h"
#include "esp8266.h"
#include "gps_neo8m.h"
#include "mqtt.h"
#include "strbuf.h"
#include "wifi_config.h"

#define WIFI_UART USART3
#define WIFI_BAUD 115200

#define SHORT_TIMEOUT_MS 2000UL
#define JOIN_TIMEOUT_MS 20000UL        /* CWJAP: DHCP + auth can genuinely take a while */
#define TCP_CONNECT_TIMEOUT_MS 15000UL /* CIPSTART="SSL": TCP + TLS handshake round trip */
#define SEND_CONFIRM_TIMEOUT_MS 5000UL /* "SEND OK"/"SEND FAIL" after raw bytes go out */
#define CONNACK_TIMEOUT_MS 8000UL
#define RETRY_DELAY_MS 8000UL /* SSL teardown is slow; a short retry delay hits "busy p..." */
#define PUBLISH_INTERVAL_MS 5000UL
#define MQTT_KEEPALIVE_SEC 60U

/* This ESP8266's AT firmware (2016, confirmed via AT+GMR) predates the
 * native AT+MQTTxxx command set, so MQTT is hand-rolled here (devices/
 * mqtt.c) over the AT firmware's basic TCP commands (AT+CIPSTART/
 * AT+CIPSEND), which have been present since the beginning. QoS0 only:
 * no subscribe, no Will, no packet-id tracking - a one-way telemetry
 * publisher doesn't need them, and periodic publishes are frequent
 * enough (every 5s, well under the 60s keepalive) that no dedicated
 * PINGREQ is needed either - any control packet resets the broker's
 * inactivity timer per the MQTT spec. */
typedef enum {
    WIFI_BRINGUP_SEND,
    WIFI_BRINGUP_WAIT,
    WIFI_GMR_SEND,
    WIFI_GMR_WAIT,
    WIFI_CWMODE_SEND,
    WIFI_CWMODE_WAIT,
    WIFI_CWJAP_SEND,
    WIFI_CWJAP_WAIT,
    WIFI_CIPMUX_SEND,
    WIFI_CIPMUX_WAIT,
    WIFI_CIPCLOSE_SEND,
    WIFI_CIPCLOSE_WAIT,
    WIFI_CIPSTART_SEND,
    WIFI_CIPSTART_WAIT,
    WIFI_CONNECT_CIPSEND_WAIT,
    WIFI_CONNACK_WAIT,
    WIFI_CONNECTED,
    WIFI_PUBLISH_CIPSEND_SEND,
    WIFI_PUBLISH_CIPSEND_WAIT,
    WIFI_PUBLISH_SENDOK_WAIT
} wifi_state_t;

static wifi_state_t state = WIFI_BRINGUP_SEND;
static uint32_t next_action_ms = 0;
static uint32_t last_publish_ms = 0;
static uint32_t publish_count = 0;
static char cmd_buf[220];
static char gmr_response[128];
static uint8_t mqtt_packet[200];
static uint32_t mqtt_packet_len = 0;
static char connack_hex[96];

static void hex_char(char *out, uint8_t nibble) {
    *out = (char)(nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10));
}

static void dump_connack_hex(const uint8_t *data, uint32_t len) {
    uint32_t pos = 0;
    for (uint32_t i = 0; i < len && pos + 3 < sizeof(connack_hex); i++) {
        hex_char(&connack_hex[pos], (uint8_t)(data[i] >> 4));
        hex_char(&connack_hex[pos + 1], (uint8_t)(data[i] & 0x0F));
        connack_hex[pos + 2] = ' ';
        pos += 3;
    }
    connack_hex[pos] = '\0';
}

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

static void copy_str(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
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

/* Builds "AT+CIPSEND=<len>" for whatever's currently in mqtt_packet. */
static void send_cipsend_for_packet(void) {
    strbuf_t sb;
    strbuf_init(&sb, cmd_buf, sizeof(cmd_buf));
    strbuf_str(&sb, "AT+CIPSEND=");
    strbuf_uint(&sb, mqtt_packet_len);
    esp8266_send(WIFI_UART, cmd_buf, SHORT_TIMEOUT_MS);
}

void Task_WiFi(void) {
    uint32_t now = SysTick_GetMillis();

    /* +IPD capture is a separate consumer of the RX ring buffer - must
     * not run in the same tick as esp8266_poll(). */
    if (state == WIFI_CONNACK_WAIT) {
        int r = esp8266_ipd_poll(WIFI_UART);
        if (r == 1) {
            dump_connack_hex(esp8266_ipd_data(), esp8266_ipd_len());
            if (mqtt_connack_ok(esp8266_ipd_data(), esp8266_ipd_len())) {
                state = WIFI_CONNECTED;
                last_publish_ms = now;
            } else {
                retry_at(WIFI_CIPCLOSE_SEND, now);
            }
        } else if (r == -1) {
            retry_at(WIFI_CIPCLOSE_SEND, now);
        }
        return;
    }

    esp8266_poll(WIFI_UART);
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
            if (st == ESP_STATUS_OK) state = WIFI_GMR_SEND;
            else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) retry_at(WIFI_BRINGUP_SEND, now);
            break;

        case WIFI_GMR_SEND:
            /* Purely diagnostic (firmware/AT version) - proceed
             * regardless of the result, just capture it for the console. */
            esp8266_send(WIFI_UART, "AT+GMR", SHORT_TIMEOUT_MS);
            state = WIFI_GMR_WAIT;
            break;

        case WIFI_GMR_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK || st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                copy_str(gmr_response, esp8266_debug_response(), sizeof(gmr_response));
                state = WIFI_CWMODE_SEND;
            }
            break;

        case WIFI_CWMODE_SEND:
            if (!due(now)) break;
            esp8266_send(WIFI_UART, "AT+CWMODE=1", SHORT_TIMEOUT_MS);
            state = WIFI_CWMODE_WAIT;
            break;

        case WIFI_CWMODE_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK) state = WIFI_CWJAP_SEND;
            else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) retry_at(WIFI_CWMODE_SEND, now);
            break;

        case WIFI_CWJAP_SEND:
            if (!due(now)) break;
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
            if (st == ESP_STATUS_OK) state = WIFI_CIPMUX_SEND;
            else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) retry_at(WIFI_CWJAP_SEND, now);
            break;

        case WIFI_CIPMUX_SEND:
            /* One-time: single-connection mode (default, but explicit). */
            esp8266_send(WIFI_UART, "AT+CIPMUX=0", SHORT_TIMEOUT_MS);
            state = WIFI_CIPMUX_WAIT;
            break;

        case WIFI_CIPMUX_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK || st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                state = WIFI_CIPCLOSE_SEND;
            }
            break;

        /* --- (Re)connect cycle: entered fresh on every retry too --- */

        case WIFI_CIPCLOSE_SEND:
            /* Always close first so a retry never fights a stale socket
             * from a previous attempt; result is irrelevant. due() gate
             * matters here: this is where every retry_at() from the
             * connect cycle lands, so without it RETRY_DELAY_MS (the
             * backoff added to dodge "busy p...") is silently skipped
             * and CIPCLOSE/CIPSTART get hammered back-to-back. */
            if (!due(now)) break;
            esp8266_send(WIFI_UART, "AT+CIPCLOSE", SHORT_TIMEOUT_MS);
            state = WIFI_CIPCLOSE_WAIT;
            break;

        case WIFI_CIPCLOSE_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK || st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                state = WIFI_CIPSTART_SEND;
            }
            break;

        case WIFI_CIPSTART_SEND:
            /* "SSL": the broker's port needs real TLS (confirmed via a
             * CONNACK hex dump that turned out to be a TLS Alert -
             * 0x15, fatal, protocol_version) - the 2016 AT firmware
             * this module shipped with couldn't negotiate it, but it's
             * since been reflashed to a modern (2022-era, mbedTLS)
             * build, so retrying real TLS here instead of the plain-
             * listener workaround. */
            strbuf_init(&sb, cmd_buf, sizeof(cmd_buf));
            strbuf_str(&sb, "AT+CIPSTART=\"SSL\",\"");
            strbuf_str_escaped(&sb, MQTT_HOST);
            strbuf_str(&sb, "\",");
            strbuf_uint(&sb, MQTT_PORT);
            esp8266_send(WIFI_UART, cmd_buf, TCP_CONNECT_TIMEOUT_MS);
            state = WIFI_CIPSTART_WAIT;
            break;

        case WIFI_CIPSTART_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK) {
                /* Some brokers enforce a tight "must speak immediately
                 * after the TLS handshake" timeout - chain straight into
                 * sending AT+CIPSEND in this same tick rather than
                 * waiting for the next scheduler invocation (up to
                 * 20ms), to not give it an excuse to close on us. */
                mqtt_packet_len = mqtt_build_connect(mqtt_packet, sizeof(mqtt_packet),
                                                      MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                                                      MQTT_KEEPALIVE_SEC);
                send_cipsend_for_packet();
                state = WIFI_CONNECT_CIPSEND_WAIT;
            } else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                retry_at(WIFI_CIPCLOSE_SEND, now);
            }
            break;

        case WIFI_CONNECT_CIPSEND_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK) {
                /* Switch to raw +IPD scanning mode immediately, before
                 * the broker's CONNACK can arrive - waiting for "SEND
                 * OK" via the line parser first is a race: if the
                 * CONNACK shows up in the same poll, the line parser
                 * consumes and mangles those raw bytes as if they were
                 * text before the IPD reader ever gets a turn. The IPD
                 * marker scan already tolerates (skips over) whatever
                 * "SEND OK" text precedes the real "+IPD," marker, so
                 * nothing is lost by not waiting for it separately. */
                uart_write(WIFI_UART, mqtt_packet, mqtt_packet_len);
                esp8266_ipd_begin(CONNACK_TIMEOUT_MS);
                state = WIFI_CONNACK_WAIT;
            } else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                retry_at(WIFI_CIPCLOSE_SEND, now);
            }
            break;

        case WIFI_CONNACK_WAIT:
            /* Unreachable: handled at the top of this function, before
             * esp8266_poll() runs, since it needs esp8266_ipd_poll()
             * instead. Listed here only so the switch is exhaustive. */
            break;

        case WIFI_CONNECTED:
            if ((int32_t)(now - last_publish_ms) >= (int32_t)PUBLISH_INTERVAL_MS) {
                char json_buf[96];
                strbuf_t json;
                strbuf_init(&json, json_buf, sizeof(json_buf));
                build_gps_json(&json);
                mqtt_packet_len = mqtt_build_publish(mqtt_packet, sizeof(mqtt_packet),
                                                       MQTT_TOPIC, json_buf, json.len);
                state = WIFI_PUBLISH_CIPSEND_SEND;
            }
            break;

        case WIFI_PUBLISH_CIPSEND_SEND:
            send_cipsend_for_packet();
            state = WIFI_PUBLISH_CIPSEND_WAIT;
            break;

        case WIFI_PUBLISH_CIPSEND_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK) {
                uart_write(WIFI_UART, mqtt_packet, mqtt_packet_len);
                esp8266_wait_again(SEND_CONFIRM_TIMEOUT_MS);
                state = WIFI_PUBLISH_SENDOK_WAIT;
            } else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                retry_at(WIFI_CIPCLOSE_SEND, now);
            }
            break;

        case WIFI_PUBLISH_SENDOK_WAIT:
            st = esp8266_get_status();
            if (st == ESP_STATUS_OK) {
                publish_count++;
                last_publish_ms = now;
                state = WIFI_CONNECTED;
            } else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                /* Publish failing usually means the TCP link dropped -
                 * reconnect rather than just retrying the publish. */
                retry_at(WIFI_CIPCLOSE_SEND, now);
            }
            break;
    }
}

const char *Task_WiFi_Status_Str(void) {
    switch (state) {
        case WIFI_BRINGUP_SEND:
        case WIFI_BRINGUP_WAIT:
            return "bring-up (AT)";
        case WIFI_GMR_SEND:
        case WIFI_GMR_WAIT:
            return "checking firmware version (AT+GMR)";
        case WIFI_CWMODE_SEND:
        case WIFI_CWMODE_WAIT:
            return "setting station mode";
        case WIFI_CWJAP_SEND:
        case WIFI_CWJAP_WAIT:
            return "joining WiFi";
        case WIFI_CIPMUX_SEND:
        case WIFI_CIPMUX_WAIT:
            return "configuring TCP mode";
        case WIFI_CIPCLOSE_SEND:
        case WIFI_CIPCLOSE_WAIT:
            return "resetting TCP socket";
        case WIFI_CIPSTART_SEND:
        case WIFI_CIPSTART_WAIT:
            return "connecting TCP to broker";
        case WIFI_CONNECT_CIPSEND_WAIT:
            return "sending MQTT CONNECT";
        case WIFI_CONNACK_WAIT:
            return "waiting for MQTT CONNACK";
        case WIFI_CONNECTED:
            return "connected, idle";
        case WIFI_PUBLISH_CIPSEND_SEND:
        case WIFI_PUBLISH_CIPSEND_WAIT:
        case WIFI_PUBLISH_SENDOK_WAIT:
            return "publishing";
    }
    return "unknown";
}

uint32_t Task_WiFi_Publish_Count(void) {
    return publish_count;
}

const char *Task_WiFi_Firmware_Str(void) {
    return gmr_response;
}

const char *Task_WiFi_Connack_Hex(void) {
    return connack_hex;
}
