#include "task_wifi.h"
#include "stm32g474xx.h"
#include "systick.h"
#include "uart.h"
#include "esp32.h"
#include "task_acquire.h"
#include "record.h"
#include "strbuf.h"
#include "wifi_config.h"

#define WIFI_UART USART3
#define WIFI_BAUD 115200

#define SHORT_TIMEOUT_MS 2000UL
#define JOIN_TIMEOUT_MS 20000UL        /* CWJAP: DHCP + auth can genuinely take a while */
#define MQTT_CONNECT_TIMEOUT_MS 15000UL /* MQTTCONN: TCP + TLS handshake round trip */
#define SEND_CONFIRM_TIMEOUT_MS 5000UL /* '>' prompt, and "+MQTTPUB:OK"/FAIL afterwards */
#define RETRY_DELAY_MS 8000UL /* TLS teardown is slow; a short retry delay hits "busy p..." */
#define PUBLISH_INTERVAL_MS 2000UL /* minimum gap between publishes, not a guaranteed cadence -
                                     * a slow publish just naturally pushes the next one out,
                                     * WIFI_CONNECTED only starts one once the last fully finished */

/* Payload buffer for the raw JSON published via AT+MQTTPUBRAW -
 * record_to_json()'s worst case (every field at its type's max-width,
 * all negative) is 416 bytes, computed by hand (see record.h) the same
 * way as task_logger.c's header_buf/row_buf - strbuf_char() silently
 * drops characters past capacity instead of erroring, so guessing a
 * size here is exactly how that CSV corruption bug happened. Margin
 * above 416, not tight against it. */
#define PUBLISH_PAYLOAD_CAP 512

/* ESP32-WROOM-32 running esp-at with its native AT+MQTTxxx command set
 * (replaces the ESP8266/ESP-01S + hand-rolled devices/mqtt.c approach -
 * that module's frozen-at-2016 AT firmware predated the native MQTT AT
 * commands entirely, so raw AT+CIPSTART/AT+CIPSEND plus a hand-built
 * MQTT CONNECT/PUBLISH packet was the only option. The ESP32 does the
 * whole MQTT protocol (framing, keepalive, QoS0) internally - we just
 * feed it connection details and payload bytes. See docs/pinout.md for
 * the wiring change and why. */
typedef enum {
    WIFI_BRINGUP_SEND,
    WIFI_BRINGUP_WAIT,
    WIFI_GMR_SEND,
    WIFI_GMR_WAIT,
    WIFI_CWMODE_SEND,
    WIFI_CWMODE_WAIT,
    WIFI_CWRECONNCFG_SEND,
    WIFI_CWRECONNCFG_WAIT,
    WIFI_CWJAP_SEND,
    WIFI_CWJAP_WAIT,
    WIFI_MQTTUSERCFG_SEND,
    WIFI_MQTTUSERCFG_WAIT,
    WIFI_MQTTCONN_SEND,
    WIFI_MQTTCONN_WAIT,
    WIFI_CONNECTED,
    WIFI_PUBLISH_MQTTPUBRAW_SEND,
    WIFI_PUBLISH_PROMPT_WAIT,
    WIFI_PUBLISH_RESULT_WAIT,
    WIFI_MQTTCLEAN_SEND, /* required before AT+MQTTCONN can re-establish a dropped link */
    WIFI_MQTTCLEAN_WAIT
} wifi_state_t;

static wifi_state_t state = WIFI_BRINGUP_SEND;
static uint32_t next_action_ms = 0;
static uint32_t last_publish_ms = 0;
static uint32_t publish_count = 0;
static char cmd_buf[220];
static char gmr_response[128];
static char publish_payload[PUBLISH_PAYLOAD_CAP];
static uint32_t publish_payload_len = 0;

/* Fallback WiFi candidates (wifi_config.h) - cycled on each join
 * failure/timeout, see WIFI_CWJAP_WAIT below. Stays on whichever one
 * last worked rather than resetting to index 0, since a successful
 * reconnect after a drop should prefer the network that's actually in
 * range right now. */
static const struct { const char *ssid; const char *pass; } wifi_networks[] = {
    { WIFI_SSID, WIFI_PASSWORD },
    { WIFI_SSID_2, WIFI_PASSWORD_2 },
};
#define WIFI_NETWORK_COUNT (sizeof(wifi_networks) / sizeof(wifi_networks[0]))
static uint32_t wifi_net_index = 0;

void Task_WiFi_Init(void) {
    esp32_init(WIFI_UART, WIFI_BAUD);
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

/* True once WiFi has been joined at least once this boot - i.e. we're
 * somewhere past the initial AT+CWJAP success. Used to gate the "did
 * WiFi drop out from under us" check below: no point checking before
 * the first join has even happened (WIFI_CWJAP_WAIT's own retry loop
 * already handles that case), and re-deriving this from state's enum
 * order would be fragile against future reordering - explicit is safer. */
static int state_is_past_initial_join(wifi_state_t s) {
    switch (s) {
        case WIFI_BRINGUP_SEND: case WIFI_BRINGUP_WAIT:
        case WIFI_GMR_SEND: case WIFI_GMR_WAIT:
        case WIFI_CWMODE_SEND: case WIFI_CWMODE_WAIT:
        case WIFI_CWRECONNCFG_SEND: case WIFI_CWRECONNCFG_WAIT:
        case WIFI_CWJAP_SEND: case WIFI_CWJAP_WAIT:
            return 0;
        default:
            return 1;
    }
}

static void copy_str(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void Task_WiFi(void) {
    uint32_t now = SysTick_GetMillis();

    /* '>' prompt capture is a separate consumer of the RX ring buffer -
     * must not run in the same tick as esp32_poll() (see esp32.h). */
    if (state == WIFI_PUBLISH_PROMPT_WAIT) {
        int r = esp32_prompt_poll(WIFI_UART);
        if (r == 1) {
            uart_write(WIFI_UART, (const uint8_t *)publish_payload, publish_payload_len);
            esp32_wait_again(SEND_CONFIRM_TIMEOUT_MS);
            state = WIFI_PUBLISH_RESULT_WAIT;
        } else if (r == -1) {
            /* No prompt within the timeout usually means the link died
             * between MQTTCONN and now - reconnect rather than retrying
             * the publish blind. */
            retry_at(WIFI_MQTTCLEAN_SEND, now);
        }
        return;
    }

    esp32_poll(WIFI_UART);

    /* WiFi itself can drop at any time (out of hotspot range, most
     * relevant on a moving bus) - independent of, and more fundamental
     * than, the MQTT-link check below. The module's own auto-reconnect
     * is deliberately disabled (WIFI_CWRECONNCFG_SEND above), so nothing
     * else will ever rejoin - without this check, WIFI_CONNECTED/
     * publish states would just keep retrying AT+MQTTCONN forever
     * against a network that no longer exists, silently, exactly the
     * "stops broadcasting and never comes back" symptom this fixes.
     * Checked before the MQTT check on purpose: if WiFi's gone, the MQTT
     * link is necessarily gone too, and a full rejoin (which re-does
     * MQTTCONN as its last step anyway) is the right response, not a
     * MQTTCLEAN/MQTTCONN cycle against a dead radio link. */
    if (state_is_past_initial_join(state) && !esp32_wifi_is_joined()) {
        retry_at(WIFI_CWJAP_SEND, now);
        return;
    }

    /* The broker can drop the link at any time (restart, network blip),
     * not just as a direct reply to something we just sent - catch that
     * here once per tick rather than duplicating the check in every
     * connected/publishing state. AT+MQTTCONN cannot re-establish a
     * dropped link on its own; AT+MQTTCLEAN=0 has to run first (see
     * WIFI_MQTTCLEAN_SEND below). */
    if ((state == WIFI_CONNECTED || state == WIFI_PUBLISH_MQTTPUBRAW_SEND ||
         state == WIFI_PUBLISH_RESULT_WAIT) &&
        !esp32_mqtt_is_connected()) {
        retry_at(WIFI_MQTTCLEAN_SEND, now);
        return;
    }

    esp_status_t st;
    strbuf_t sb;

    switch (state) {
        case WIFI_BRINGUP_SEND:
            if (!due(now)) break;
            esp32_send(WIFI_UART, "AT", SHORT_TIMEOUT_MS);
            state = WIFI_BRINGUP_WAIT;
            break;

        case WIFI_BRINGUP_WAIT:
            st = esp32_get_status();
            if (st == ESP_STATUS_OK) state = WIFI_GMR_SEND;
            else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) retry_at(WIFI_BRINGUP_SEND, now);
            break;

        case WIFI_GMR_SEND:
            /* Purely diagnostic (firmware/AT version) - proceed
             * regardless of the result, just capture it for the console. */
            esp32_send(WIFI_UART, "AT+GMR", SHORT_TIMEOUT_MS);
            state = WIFI_GMR_WAIT;
            break;

        case WIFI_GMR_WAIT:
            st = esp32_get_status();
            if (st == ESP_STATUS_OK || st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                copy_str(gmr_response, esp32_debug_response(), sizeof(gmr_response));
                state = WIFI_CWMODE_SEND;
            }
            break;

        case WIFI_CWMODE_SEND:
            if (!due(now)) break;
            esp32_send(WIFI_UART, "AT+CWMODE=1", SHORT_TIMEOUT_MS);
            state = WIFI_CWMODE_WAIT;
            break;

        case WIFI_CWMODE_WAIT:
            st = esp32_get_status();
            if (st == ESP_STATUS_OK) state = WIFI_CWRECONNCFG_SEND;
            else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) retry_at(WIFI_CWMODE_SEND, now);
            break;

        case WIFI_CWRECONNCFG_SEND:
            /* Disable the module's own auto-reconnect (interval=0) -
             * task_wifi.c drives every reconnect explicitly and
             * deterministically instead (see the WIFI DISCONNECT check
             * at the top of Task_WiFi()), same reasoning as not trusting
             * AT+MQTTCONN's reconnect flag alone for the MQTT link.
             * Two independent reconnect mechanisms racing each other
             * would be harder to reason about than just owning it here.
             * One-time; proceed regardless of the result. */
            esp32_send(WIFI_UART, "AT+CWRECONNCFG=0,0", SHORT_TIMEOUT_MS);
            state = WIFI_CWRECONNCFG_WAIT;
            break;

        case WIFI_CWRECONNCFG_WAIT:
            st = esp32_get_status();
            if (st == ESP_STATUS_OK || st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                state = WIFI_CWJAP_SEND;
            }
            break;

        case WIFI_CWJAP_SEND:
            if (!due(now)) break;
            strbuf_init(&sb, cmd_buf, sizeof(cmd_buf));
            strbuf_str(&sb, "AT+CWJAP=\"");
            strbuf_str_escaped(&sb, wifi_networks[wifi_net_index].ssid);
            strbuf_str(&sb, "\",\"");
            strbuf_str_escaped(&sb, wifi_networks[wifi_net_index].pass);
            strbuf_str(&sb, "\"");
            esp32_send(WIFI_UART, cmd_buf, JOIN_TIMEOUT_MS);
            state = WIFI_CWJAP_WAIT;
            break;

        case WIFI_CWJAP_WAIT:
            st = esp32_get_status();
            if (st == ESP_STATUS_OK) {
                state = WIFI_MQTTUSERCFG_SEND;
            } else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                wifi_net_index = (wifi_net_index + 1) % WIFI_NETWORK_COUNT;
                retry_at(WIFI_CWJAP_SEND, now);
            }
            break;

        /* --- One-time MQTT client config (scheme=2: TLS, server cert not
         * verified - deliberate for now, see wifi_config.h) --- */

        case WIFI_MQTTUSERCFG_SEND:
            strbuf_init(&sb, cmd_buf, sizeof(cmd_buf));
            strbuf_str(&sb, "AT+MQTTUSERCFG=0,2,\"");
            strbuf_str_escaped(&sb, MQTT_CLIENT_ID);
            strbuf_str(&sb, "\",\"");
            strbuf_str_escaped(&sb, MQTT_USER);
            strbuf_str(&sb, "\",\"");
            strbuf_str_escaped(&sb, MQTT_PASS);
            strbuf_str(&sb, "\",0,0,\"\"");
            esp32_send(WIFI_UART, cmd_buf, SHORT_TIMEOUT_MS);
            state = WIFI_MQTTUSERCFG_WAIT;
            break;

        case WIFI_MQTTUSERCFG_WAIT:
            /* One-time client config; proceed regardless of the result
             * (mirrors the old CIPMUX step) - a real failure here shows
             * up as MQTTCONN never reaching +MQTTCONNECTED, which does
             * retry indefinitely below. */
            st = esp32_get_status();
            if (st == ESP_STATUS_OK || st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                state = WIFI_MQTTCONN_SEND;
            }
            break;

        /* --- (Re)connect cycle: entered fresh on every retry too --- */

        case WIFI_MQTTCONN_SEND:
            if (!due(now)) break;
            if (esp32_mqtt_is_connected()) {
                /* Already connected - e.g. the async +MQTTCONNECTED
                 * notification for a prior attempt landed just after we
                 * gave up on that attempt's own OK/ERROR/TIMEOUT and
                 * came back here. Re-sending AT+MQTTCONN over an
                 * already-open link just gets rejected by the module,
                 * which would otherwise retry forever without ever
                 * reaching WIFI_CONNECTED even though the link is fine. */
                state = WIFI_CONNECTED;
                last_publish_ms = now;
                break;
            }
            strbuf_init(&sb, cmd_buf, sizeof(cmd_buf));
            strbuf_str(&sb, "AT+MQTTCONN=0,\"");
            strbuf_str_escaped(&sb, MQTT_HOST);
            strbuf_str(&sb, "\",");
            strbuf_uint(&sb, MQTT_PORT);
            strbuf_str(&sb, ",1"); /* reconnect=1: let the module auto-retry a dropped TCP link */
            esp32_send(WIFI_UART, cmd_buf, MQTT_CONNECT_TIMEOUT_MS);
            state = WIFI_MQTTCONN_WAIT;
            break;

        case WIFI_MQTTCONN_WAIT:
            /* Trust whichever signal arrives first - the command's own
             * terminal response, or the async +MQTTCONNECTED
             * notification (see esp32_mqtt_is_connected()'s doc comment
             * - these can race). Waiting on the command response alone
             * risks exactly the stuck loop described above. */
            if (esp32_mqtt_is_connected()) {
                state = WIFI_CONNECTED;
                last_publish_ms = now;
                break;
            }
            st = esp32_get_status();
            if (st == ESP_STATUS_OK) {
                state = WIFI_CONNECTED;
                last_publish_ms = now;
            } else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                retry_at(WIFI_MQTTCONN_SEND, now);
            }
            break;

        case WIFI_MQTTCLEAN_SEND:
            /* due() gate matters here: this is where every retry_at()
             * from a dropped link lands, so without it RETRY_DELAY_MS
             * (the backoff added to dodge "busy p...") is silently
             * skipped and MQTTCLEAN/MQTTCONN get hammered back-to-back. */
            if (!due(now)) break;
            esp32_send(WIFI_UART, "AT+MQTTCLEAN=0", SHORT_TIMEOUT_MS);
            state = WIFI_MQTTCLEAN_WAIT;
            break;

        case WIFI_MQTTCLEAN_WAIT:
            st = esp32_get_status();
            if (st == ESP_STATUS_OK || st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                state = WIFI_MQTTCONN_SEND;
            }
            break;

        case WIFI_CONNECTED:
            if ((int32_t)(now - last_publish_ms) >= (int32_t)PUBLISH_INTERVAL_MS) {
                state = WIFI_PUBLISH_MQTTPUBRAW_SEND;
            }
            break;

        case WIFI_PUBLISH_MQTTPUBRAW_SEND: {
            strbuf_t json;
            strbuf_init(&json, publish_payload, sizeof(publish_payload));
            record_to_json(&json, Task_Acquire_Last_Record());
            publish_payload_len = json.len;

            strbuf_init(&sb, cmd_buf, sizeof(cmd_buf));
            strbuf_str(&sb, "AT+MQTTPUBRAW=0,\"");
            strbuf_str_escaped(&sb, MQTT_TOPIC);
            strbuf_str(&sb, "\",");
            strbuf_uint(&sb, publish_payload_len);
            strbuf_str(&sb, ",0,0");
            esp32_send(WIFI_UART, cmd_buf, SHORT_TIMEOUT_MS);
            esp32_prompt_begin(SEND_CONFIRM_TIMEOUT_MS);
            state = WIFI_PUBLISH_PROMPT_WAIT;
            break;
        }

        case WIFI_PUBLISH_PROMPT_WAIT:
            /* Unreachable: handled at the top of this function, before
             * esp32_poll() runs, since it needs esp32_prompt_poll()
             * instead. Listed here only so the switch is exhaustive. */
            break;

        case WIFI_PUBLISH_RESULT_WAIT:
            st = esp32_get_status();
            if (st == ESP_STATUS_OK) {
                publish_count++;
                last_publish_ms = now;
                state = WIFI_CONNECTED;
            } else if (st == ESP_STATUS_ERROR || st == ESP_STATUS_TIMEOUT) {
                /* Publish failing usually means the MQTT link dropped -
                 * reconnect rather than just retrying the publish. */
                retry_at(WIFI_MQTTCLEAN_SEND, now);
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
        case WIFI_CWRECONNCFG_SEND:
        case WIFI_CWRECONNCFG_WAIT:
            return "configuring reconnect behavior";
        case WIFI_CWJAP_SEND:
        case WIFI_CWJAP_WAIT:
            return "joining WiFi";
        case WIFI_MQTTUSERCFG_SEND:
        case WIFI_MQTTUSERCFG_WAIT:
            return "configuring MQTT client";
        case WIFI_MQTTCONN_SEND:
        case WIFI_MQTTCONN_WAIT:
            return "connecting MQTT (TLS) to broker";
        case WIFI_MQTTCLEAN_SEND:
        case WIFI_MQTTCLEAN_WAIT:
            return "clearing dropped MQTT link";
        case WIFI_CONNECTED:
            return "connected, idle";
        case WIFI_PUBLISH_MQTTPUBRAW_SEND:
        case WIFI_PUBLISH_PROMPT_WAIT:
        case WIFI_PUBLISH_RESULT_WAIT:
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

int Task_WiFi_MQTT_Connected(void) {
    return esp32_mqtt_is_connected();
}
