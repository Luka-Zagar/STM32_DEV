#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>

/* Minimal hand-rolled MQTT 3.1.1 client-side packet encoding - just
 * what a one-way QoS0 telemetry publisher needs (no subscribe, no
 * Will, no QoS>0). Pure encoding, no I/O; the caller sends the
 * resulting bytes however its transport works (here: AT+CIPSEND over
 * the ESP8266's raw TCP AT commands, since this module's AT firmware
 * predates the native MQTT AT command set). */

/* Builds a CONNECT packet (clean session, no Will) into buf (capacity
 * cap). user/pass may be NULL or empty for no authentication. Returns
 * the packet length, or 0 if it doesn't fit in cap. */
uint32_t mqtt_build_connect(uint8_t *buf, uint32_t cap, const char *client_id,
                            const char *user, const char *pass, uint16_t keepalive_sec);

/* Builds a QoS0 PUBLISH packet (no retain, no packet identifier) into
 * buf. Returns the packet length, or 0 if it doesn't fit in cap. */
uint32_t mqtt_build_publish(uint8_t *buf, uint32_t cap, const char *topic,
                            const char *payload, uint32_t payload_len);

/* True if `buf` (length len) is a CONNACK reporting success (return
 * code 0). */
int mqtt_connack_ok(const uint8_t *buf, uint32_t len);

#endif /* MQTT_H */
