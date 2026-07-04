#include "mqtt.h"

#define BODY_CAP 192

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static uint32_t append_u16(uint8_t *buf, uint32_t pos, uint16_t v) {
    buf[pos] = (uint8_t)(v >> 8);
    buf[pos + 1] = (uint8_t)(v & 0xFF);
    return pos + 2;
}

/* Appends a length-prefixed UTF-8 string. Returns a position > cap on
 * overflow (checked by the caller) rather than writing out of bounds. */
static uint32_t append_str(uint8_t *buf, uint32_t cap, uint32_t pos, const char *s) {
    uint32_t len = str_len(s);
    if (pos + 2 + len > cap) return cap + 1;
    pos = append_u16(buf, pos, (uint16_t)len);
    for (uint32_t i = 0; i < len; i++) buf[pos++] = (uint8_t)s[i];
    return pos;
}

static uint32_t encode_remaining_length(uint8_t *out, uint32_t value) {
    uint32_t n = 0;
    do {
        uint8_t b = (uint8_t)(value % 128);
        value /= 128;
        if (value > 0) b |= 0x80;
        out[n++] = b;
    } while (value > 0);
    return n;
}

uint32_t mqtt_build_connect(uint8_t *buf, uint32_t cap, const char *client_id,
                            const char *user, const char *pass, uint16_t keepalive_sec) {
    uint8_t body[BODY_CAP];
    uint32_t pos = append_str(body, BODY_CAP, 0, "MQTT");
    if (pos > BODY_CAP) return 0;
    body[pos++] = 0x04; /* protocol level 4 = MQTT 3.1.1 */

    uint8_t has_user = (user && user[0]) ? 1 : 0;
    uint8_t has_pass = (pass && pass[0]) ? 1 : 0;
    uint8_t flags = 0x02; /* clean session */
    if (has_user) flags |= 0x80;
    if (has_pass) flags |= 0x40;
    body[pos++] = flags;

    pos = append_u16(body, pos, keepalive_sec);

    pos = append_str(body, BODY_CAP, pos, client_id);
    if (pos > BODY_CAP) return 0;
    if (has_user) {
        pos = append_str(body, BODY_CAP, pos, user);
        if (pos > BODY_CAP) return 0;
    }
    if (has_pass) {
        pos = append_str(body, BODY_CAP, pos, pass);
        if (pos > BODY_CAP) return 0;
    }

    uint8_t rl[4];
    uint32_t rl_len = encode_remaining_length(rl, pos);
    uint32_t total = 1 + rl_len + pos;
    if (total > cap) return 0;

    uint32_t out = 0;
    buf[out++] = 0x10; /* CONNECT */
    for (uint32_t i = 0; i < rl_len; i++) buf[out++] = rl[i];
    for (uint32_t i = 0; i < pos; i++) buf[out++] = body[i];
    return out;
}

uint32_t mqtt_build_publish(uint8_t *buf, uint32_t cap, const char *topic,
                            const char *payload, uint32_t payload_len) {
    uint8_t body[BODY_CAP];
    uint32_t pos = append_str(body, BODY_CAP, 0, topic);
    if (pos > BODY_CAP) return 0;
    if (pos + payload_len > BODY_CAP) return 0;
    for (uint32_t i = 0; i < payload_len; i++) body[pos++] = (uint8_t)payload[i];

    uint8_t rl[4];
    uint32_t rl_len = encode_remaining_length(rl, pos);
    uint32_t total = 1 + rl_len + pos;
    if (total > cap) return 0;

    uint32_t out = 0;
    buf[out++] = 0x30; /* PUBLISH, QoS0, no DUP/retain */
    for (uint32_t i = 0; i < rl_len; i++) buf[out++] = rl[i];
    for (uint32_t i = 0; i < pos; i++) buf[out++] = body[i];
    return out;
}

int mqtt_connack_ok(const uint8_t *buf, uint32_t len) {
    /* buf[1] is the remaining-length byte: 2 for a plain MQTT 3.1.1
     * CONNACK (ack flags + return code, nothing else), or 3+ for MQTT 5
     * (ack flags + reason code + a properties length/properties block
     * we don't need to parse). Either way the reason/return code sits
     * at the same offset, buf[3], right after the ack flags - so accept
     * any remaining length that's at least long enough to contain it,
     * rather than requiring the v3.1.1-only exact length. */
    if (len < 4) return 0;
    if (buf[0] != 0x20 || buf[1] < 2) return 0;
    return buf[3] == 0x00;
}
