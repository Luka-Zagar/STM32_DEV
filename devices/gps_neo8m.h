#ifndef GPS_NEO8M_H
#define GPS_NEO8M_H

#include <stdint.h>
#include "stm32g474xx.h"

typedef struct {
    int32_t lat_e7;    /* 1e-7 deg, +N/-S */
    int32_t lon_e7;    /* 1e-7 deg, +E/-W */
    uint32_t utc_time; /* HHMMSS, from RMC */
    uint32_t utc_date; /* DDMMYY, from RMC */
    uint8_t fix_valid; /* 1 once RMC status is 'A' and GGA quality != 0 */
    uint8_t num_sats;  /* from GGA, 0 if unknown */
} gps_fix_t;

/* Bus-handle convention: gps_init(USART1, 9600), never gps_uart_init(). */
void gps_init(USART_TypeDef *uart, uint32_t baud);

/* Feeds any bytes waiting in the UART's RX ring buffer through the NMEA
 * parser, updating the fix as complete $GNRMC/$GNGGA sentences arrive.
 * Non-blocking; call frequently (e.g. every scheduler tick). */
void gps_poll(USART_TypeDef *uart);

/* Latest parsed fix. Check fix_valid before trusting lat_e7/lon_e7. */
const gps_fix_t *gps_get_fix(void);

#endif /* GPS_NEO8M_H */
