#ifndef GPS_NEO8M_H
#define GPS_NEO8M_H

#include <stdint.h>
#include "stm32g474xx.h"

typedef enum {
    GPS_CONST_GPS = 0,
    GPS_CONST_GLONASS,
    GPS_CONST_GALILEO,
    GPS_CONST_BEIDOU,
    GPS_CONST_COUNT
} gps_constellation_t;

typedef struct {
    int32_t lat_e7;         /* 1e-7 deg, +N/-S, from RMC */
    int32_t lon_e7;         /* 1e-7 deg, +E/-W, from RMC */
    int32_t alt_dm;         /* altitude, tenths of a meter (MSL), from GGA */
    int32_t speed_kmh_x10;  /* km/h * 10, converted from RMC's knots */
    int32_t heading_x10;    /* track angle, degrees * 10, from RMC */
    uint32_t utc_time;      /* HHMMSS, from RMC */
    uint32_t utc_date;      /* DDMMYY, from RMC */
    uint8_t fix_valid;      /* 1 once RMC status is 'A' and GGA quality != 0 */
    uint8_t fix_type;       /* 1=no fix, 2=2D, 3=3D, from GSA */
    uint8_t num_sats;       /* satellites used in fix, from GGA */
    uint8_t sats_in_view[GPS_CONST_COUNT]; /* satellites in view, from GSV, per constellation */
    uint16_t pdop_x10;      /* from GSA */
    uint16_t hdop_x10;      /* from GSA */
    uint16_t vdop_x10;      /* from GSA */
    uint16_t avg_snr_x10;   /* dBHz * 10, averaged over the last full round of GSV sentences */
} gps_fix_t;

/* Bus-handle convention: gps_init(USART1, 9600), never gps_uart_init(). */
void gps_init(USART_TypeDef *uart, uint32_t baud);

/* Feeds any bytes waiting in the UART's RX ring buffer through the NMEA
 * parser, updating the fix as complete sentences arrive ($GNRMC, $GNGGA,
 * $GNGSA, $..GSV). Non-blocking; call frequently (e.g. every scheduler
 * tick). */
void gps_poll(USART_TypeDef *uart);

/* Latest parsed fix. Check fix_valid before trusting lat_e7/lon_e7. */
const gps_fix_t *gps_get_fix(void);

#endif /* GPS_NEO8M_H */
