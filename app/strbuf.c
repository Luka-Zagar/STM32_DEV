#include "strbuf.h"

void strbuf_init(strbuf_t *sb, char *buf, uint32_t cap) {
    sb->buf = buf;
    sb->cap = cap;
    sb->len = 0;
    if (cap > 0) buf[0] = '\0';
}

void strbuf_char(strbuf_t *sb, char c) {
    if (sb->len + 1 >= sb->cap) return; /* no room, drop silently */
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

void strbuf_str(strbuf_t *sb, const char *s) {
    while (*s) strbuf_char(sb, *s++);
}

void strbuf_str_escaped(strbuf_t *sb, const char *s) {
    while (*s) {
        if (*s == '"' || *s == '\\') strbuf_char(sb, '\\');
        strbuf_char(sb, *s++);
    }
}

void strbuf_uint(strbuf_t *sb, uint32_t v) {
    char digits[10];
    int n = 0;
    do {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n > 0) strbuf_char(sb, digits[--n]);
}

void strbuf_int(strbuf_t *sb, int32_t v) {
    if (v < 0) {
        strbuf_char(sb, '-');
        v = -v;
    }
    strbuf_uint(sb, (uint32_t)v);
}

void strbuf_fixed(strbuf_t *sb, int32_t v, uint32_t decimals) {
    uint32_t scale = 1;
    for (uint32_t i = 0; i < decimals; i++) scale *= 10;

    if (v < 0) {
        strbuf_char(sb, '-');
        v = -v;
    }

    uint32_t int_part = (uint32_t)v / scale;
    uint32_t frac_part = (uint32_t)v % scale;

    strbuf_uint(sb, int_part);
    if (decimals == 0) return;

    strbuf_char(sb, '.');
    for (uint32_t p = scale / 10; p > 0; p /= 10) {
        strbuf_char(sb, (char)('0' + (frac_part / p) % 10));
    }
}
