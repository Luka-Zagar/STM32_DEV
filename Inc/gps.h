/**
 ******************************************************************************
 * @file        gps.h
 * @brief       NEO-8M GPS Module Driver (NMEA Parser).
 ******************************************************************************
 */

#ifndef GPS_H
#define GPS_H

#include <stdint.h>
#include <stdbool.h>

/* ── GPS Data Structure ──────────────────────────────────────────────────── */

typedef struct {
    float latitude;
    float longitude;
    float altitude;     /* Altitude in meters above sea level */
    float geoid_sep;    /* Geoid separation in meters */
    float h_acc;        /* Estimated Horizontal Accuracy (meters) */            
    float v_acc;        /* Estimated Vertical Accuracy (meters) */
    float speed;        /* Speed in km/h */
    float course;       /* Heading/Course in degrees */
    float hdop, vdop, pdop;
    float avg_snr;
    uint32_t time;      /* HHMMSS (UTC) */
    uint32_t date;      /* DDMMYY */
    uint8_t sats_used;  /* Total satellites used in fix */
    uint8_t sats_gps;   /* GPS satellites in view */
    uint8_t sats_bd;    /* BeiDou satellites in view */
    uint8_t sats_ga;    /* Galileo satellites in view */
    uint8_t sats_gl;    /* GLONASS satellites in view */
    uint8_t fix_type;   /* 1=No Fix, 2=2D Fix, 3=3D Fix */
    uint32_t packets_pubx; /* Debug counter for accuracy packets */
    char status;        /* 'A' = Valid, 'V' = Invalid */
    bool updated;       /* Flag set when new data is parsed */
    char last_raw[128]; /* Last raw NMEA sentence for debugging */
} GPS_Data_t;

/* ── Public Functions ────────────────────────────────────────────────────── */

/**
 * @brief Initializes USART1 for GPS communication at 9600 baud.
 */
void GPS_Init(void);

/**
 * @brief Updates GPS data by reading from USART1.
 * Call this frequently in the main loop or as a task.
 */
void GPS_Update(void);

/**
 * @brief Returns the latest parsed GPS data.
 */
GPS_Data_t GPS_GetData(void);

/**
 * @brief Sends a raw NMEA command string to the GPS module.
 */
void GPS_Send(const char *cmd);

/**
 * @brief Sends a binary UBX frame to the GPS module.
 */
void GPS_Send_UBX(uint8_t *data, uint16_t len);

#endif /* GPS_H */
