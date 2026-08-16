#ifndef STRBUF_H
#define STRBUF_H

#include <stdint.h>

/* Minimal bounded string builder - assembles AT commands and JSON
 * payloads before handing them to esp32_send(). Mirrors
 * uart_write_uint/uart_write_fixed's digit conversion, just targeting
 * memory instead of a UART. Integer/fixed-point only, no floats. */
typedef struct {
    char *buf;
    uint32_t cap; /* including room for the NUL terminator */
    uint32_t len;
} strbuf_t;

void strbuf_init(strbuf_t *sb, char *buf, uint32_t cap);

void strbuf_char(strbuf_t *sb, char c);
void strbuf_str(strbuf_t *sb, const char *s);

/* Appends s with every '"' and '\' escaped as '\"'/'\\' - needed to
 * embed a JSON payload inside an AT command's quoted string argument. */
void strbuf_str_escaped(strbuf_t *sb, const char *s);

void strbuf_uint(strbuf_t *sb, uint32_t v);
void strbuf_int(strbuf_t *sb, int32_t v);

/* Fixed-point decimal with `decimals` implied fractional digits, same
 * convention as uart_write_fixed. */
void strbuf_fixed(strbuf_t *sb, int32_t v, uint32_t decimals);

#endif /* STRBUF_H */
