/**
 ******************************************************************************
 * @file        gps.c
 * @brief       NEO-8M GPS Module Driver (NMEA + UBX Binary Parser).
 ******************************************************************************
 */

#include "gps.h"
#include "stm32g474xx.h"
#include <string.h>

/* Internal GPS state storage */
static GPS_Data_t current_gps_data = {0};
static char nmea_buffer[256];
static uint8_t buffer_index = 0;

/* UBX State Machine */
typedef enum {
    UBX_IDLE, UBX_SYNC2, UBX_CLASS, UBX_ID, UBX_LEN1, UBX_LEN2, UBX_PAYLOAD, UBX_CKA, UBX_CKB
} UBX_State_t;

static float parse_float(char* s) {
    if (!s || *s == '\0') return 0.0f;
    float res = 0.0f, div = 1.0f; int dot = 0;
    while (*s) {
        if (*s == '.') { dot = 1; s++; continue; }
        if (*s < '0' || *s > '9') break;
        res = res * 10.0f + (*s - '0');
        if (dot) div *= 10.0f;
        s++;
    }
    return res / div;
}

static uint32_t parse_int(char* s) {
    if (!s || *s == '\0') return 0;
    uint32_t res = 0;
    while (*s && *s >= '0' && *s <= '9') { res = res * 10 + (*s - '0'); s++; }
    return res;
}

void GPS_Send(const char *cmd) {
    while (*cmd) {
        GPIOA->ODR ^= GPIO_ODR_5; /* Toggle Green LED during TX */
        while (!(USART1->ISR & USART_ISR_TXE));
        USART1->TDR = *cmd++;
    }
}

void GPS_Send_UBX(uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        GPIOA->ODR ^= GPIO_ODR_5; /* Toggle Green LED during TX */
        while (!(USART1->ISR & USART_ISR_TXE));
        USART1->TDR = data[i];
    }
}

static void GPS_Parse_PUBX_00(char *line) {
    char *p = strstr(line, "$PUBX,00,");
    if (!p) return;
    current_gps_data.packets_pubx++;
    p += 9;
    int field = 1; char token[32]; int token_idx = 0;
    while (*p) {
        if (*p == ',' || *p == '*' || *p == '\r' || *p == '\n') {
            token[token_idx] = '\0';
            if (token_idx > 0) {
                switch (field) {
                    case 8:  current_gps_data.h_acc = parse_float(token); break;
                    case 9:  current_gps_data.v_acc = parse_float(token); break;
                }
            }
            if (*p == '*') break;
            field++; token_idx = 0; p++;
        } else { if (token_idx < 31) token[token_idx++] = *p; p++; }
    }
    current_gps_data.updated = true;
}

static void GPS_Parse_UBX_PVT(uint8_t *payload) {
    uint32_t h_acc_mm = (uint32_t)payload[40] | ((uint32_t)payload[41] << 8) | ((uint32_t)payload[42] << 16) | ((uint32_t)payload[43] << 24);
    uint32_t v_acc_mm = (uint32_t)payload[44] | ((uint32_t)payload[45] << 8) | ((uint32_t)payload[46] << 16) | ((uint32_t)payload[47] << 24);
    current_gps_data.h_acc = (float)h_acc_mm / 1000.0f;
    current_gps_data.v_acc = (float)v_acc_mm / 1000.0f;
    current_gps_data.packets_pubx++;
    current_gps_data.updated = true;
}

static void GPS_Parse_RMC_Aggressive(char *line) {
    char *p = strstr(line, "RMC"); if (!p) return; p += 4;
    int field = 1; char token[32]; int token_idx = 0;
    while (*p) {
        if (*p == ',' || *p == '*' || *p == '\r' || *p == '\n') {
            token[token_idx] = '\0';
            if (token_idx > 0) {
                switch (field) {
                    case 1: current_gps_data.time = parse_int(token); break;
                    case 2: current_gps_data.status = token[0]; break;
                    case 3: { float raw = parse_float(token); int deg = (int)(raw / 100); current_gps_data.latitude = deg + (raw - deg * 100) / 60.0f; break; }
                    case 4: if (token[0] == 'S') current_gps_data.latitude *= -1.0f; break;
                    case 5: { float raw = parse_float(token); int deg = (int)(raw / 100); current_gps_data.longitude = deg + (raw - deg * 100) / 60.0f; break; }
                    case 6: if (token[0] == 'W') current_gps_data.longitude *= -1.0f; break;
                    case 7: current_gps_data.speed = parse_float(token) * 1.852f; break;
                    case 8: current_gps_data.course = parse_float(token); break;
                    case 9: current_gps_data.date = parse_int(token); break;
                }
            }
            if (*p == '*') break;
            field++; token_idx = 0; p++;
        } else { if (token_idx < 31) token[token_idx++] = *p; p++; }
    }
    current_gps_data.updated = true;
}

static void GPS_Parse_GSA_Aggressive(char *line) {
    char *p = strstr(line, "GSA"); if (!p) return; p += 4;
    int field = 1; char token[32]; int token_idx = 0;
    while (*p) {
        if (*p == ',' || *p == '*' || *p == '\r' || *p == '\n') {
            token[token_idx] = '\0';
            if (token_idx > 0) {
                switch (field) {
                    case 2:  current_gps_data.fix_type = (uint8_t)parse_int(token); break;
                    case 15: current_gps_data.pdop = parse_float(token); break;
                    case 16: current_gps_data.hdop = parse_float(token); break;
                    case 17: current_gps_data.vdop = parse_float(token); break;
                }
            }
            if (*p == '*') break;
            field++; token_idx = 0; p++;
        } else { if (token_idx < 31) token[token_idx++] = *p; p++; }
    }
}

static void GPS_Parse_GSV_Aggressive(char *line) {
    uint8_t *count_ptr = NULL;
    if (strstr(line, "GPGSV"))      count_ptr = &current_gps_data.sats_gps;
    else if (strstr(line, "BDGSV")) count_ptr = &current_gps_data.sats_bd;
    else if (strstr(line, "GAGSV")) count_ptr = &current_gps_data.sats_ga;
    else if (strstr(line, "GLGSV")) count_ptr = &current_gps_data.sats_gl;
    if (!count_ptr) return;
    char *p = strstr(line, "GSV"); p += 4;
    int field = 1; char token[32]; int token_idx = 0;
    static float snr_sum = 0; static int snr_count = 0;
    while (*p) {
        if (*p == ',' || *p == '*' || *p == '\r' || *p == '\n') {
            token[token_idx] = '\0';
            if (field == 2 && token_idx > 0) if (parse_int(token) == 1) { snr_sum = 0; snr_count = 0; }
            if (field == 3 && token_idx > 0) *count_ptr = (uint8_t)parse_int(token);
            if ((field == 7 || field == 11 || field == 15 || field == 19) && token_idx > 0) {
                float snr = parse_float(token); if (snr > 0) { snr_sum += snr; snr_count++; current_gps_data.avg_snr = snr_sum / snr_count; }
            }
            if (*p == '*') break;
            field++; token_idx = 0; p++;
        } else { if (token_idx < 31) token[token_idx++] = *p; p++; }
    }
}

static void GPS_Parse_GGA_Aggressive(char *line) {
    char *p = strstr(line, "GGA"); if (!p) return; p += 4;
    int field = 1; char token[32]; int token_idx = 0;
    while (*p) {
        if (*p == ',' || *p == '*' || *p == '\r' || *p == '\n') {
            token[token_idx] = '\0';
            if (token_idx > 0) {
                switch (field) {
                    case 6:  if (token[0] == '0') current_gps_data.status = 'V'; break;
                    case 7:  current_gps_data.sats_used = (uint8_t)parse_int(token); break;
                    case 8:  current_gps_data.hdop = parse_float(token); break;
                    case 9:  current_gps_data.altitude = parse_float(token); break;
                    case 11: current_gps_data.geoid_sep = parse_float(token); break;
                }
            }
            if (*p == '*') break;
            field++; token_idx = 0; p++;
        } else { if (token_idx < 31) token[token_idx++] = *p; p++; }
    }
}

void GPS_Init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN | RCC_AHB2ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->CCIPR &= ~(3UL << 0); RCC->CCIPR |= (1UL << 0);

    /* PC4: TX, PC5: RX */
    GPIOC->MODER &= ~(3UL << (4 * 2)); GPIOC->MODER |= (1UL << (4 * 2)); /* GPIO Output for Pulse Test */
    GPIOC->OSPEEDR |= (3UL << (4 * 2));
    
    /* HARDWARE PULSE TEST: Blink LED and PC4 synchronously */
    for(int i=0; i<10; i++) {
        GPIOA->ODR |= (1UL << 5); GPIOC->ODR |= (1UL << 4);
        for(volatile int j=0; j<200000; j++);
        GPIOA->ODR &= ~(1UL << 5); GPIOC->ODR &= ~(1UL << 4);
        for(volatile int j=0; j<200000; j++);
    }

    /* Configure Pins for USART1 (AF7) */
    GPIOC->MODER &= ~(3UL << (4 * 2)); GPIOC->MODER |= (2UL << (4 * 2));
    GPIOC->AFR[0] &= ~(0xF << (4 * 4)); GPIOC->AFR[0] |= (7UL << (4 * 4));
    GPIOC->PUPDR |= (1UL << (4 * 2));
    
    GPIOC->MODER &= ~(3UL << (5 * 2)); GPIOC->MODER |= (2UL << (5 * 2));
    GPIOC->AFR[0] &= ~(0xF << (5 * 4)); GPIOC->AFR[0] |= (7UL << (5 * 4));

    USART1->BRR = 17708; 
    USART1->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;
}

void GPS_Update(void) {
    static UBX_State_t ubx_state = UBX_IDLE;
    static uint8_t ubx_class, ubx_id, ubx_cka, ubx_ckb, ubx_payload[100];
    static uint16_t ubx_payload_len, ubx_payload_idx;

    if (USART1->ISR & (USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE | USART_ISR_PE)) USART1->ICR = 0x1F;

    while (USART1->ISR & USART_ISR_RXNE) {
        uint8_t c = (uint8_t)USART1->RDR;
        if (ubx_state == UBX_IDLE) {
            if (c == 0xB5) ubx_state = UBX_SYNC2;
            else if (c == '$') { buffer_index = 0; nmea_buffer[buffer_index++] = '$'; }
            else if (buffer_index > 0 && buffer_index < sizeof(nmea_buffer) - 1) {
                nmea_buffer[buffer_index++] = c;
                if (c == '\n' || c == '\r') {
                    nmea_buffer[buffer_index] = '\0';
                    strncpy(current_gps_data.last_raw, nmea_buffer, sizeof(current_gps_data.last_raw)-1);
                    if (strstr(nmea_buffer, "PUBX"))     GPS_Parse_PUBX_00(nmea_buffer);
                    else if (strstr(nmea_buffer, "RMC")) GPS_Parse_RMC_Aggressive(nmea_buffer);
                    else if (strstr(nmea_buffer, "GGA")) GPS_Parse_GGA_Aggressive(nmea_buffer);
                    else if (strstr(nmea_buffer, "GSA")) GPS_Parse_GSA_Aggressive(nmea_buffer);
                    else if (strstr(nmea_buffer, "GSV")) GPS_Parse_GSV_Aggressive(nmea_buffer);
                    buffer_index = 0;
                }
            }
        } else if (ubx_state == UBX_SYNC2) { if (c == 0x62) { ubx_state = UBX_CLASS; ubx_cka = ubx_ckb = 0; } else ubx_state = UBX_IDLE; }
        else if (ubx_state == UBX_CLASS) { ubx_class = c; ubx_cka += c; ubx_ckb += ubx_cka; ubx_state = UBX_ID; }
        else if (ubx_state == UBX_ID) { ubx_id = c; ubx_cka += c; ubx_ckb += ubx_cka; ubx_state = UBX_LEN1; }
        else if (ubx_state == UBX_LEN1) { ubx_payload_len = c; ubx_cka += c; ubx_ckb += ubx_cka; ubx_state = UBX_LEN2; }
        else if (ubx_state == UBX_LEN2) {
            ubx_payload_len |= (c << 8); ubx_cka += c; ubx_ckb += ubx_cka;
            if (ubx_payload_len > sizeof(ubx_payload)) ubx_state = UBX_IDLE;
            else { ubx_payload_idx = 0; ubx_state = (ubx_payload_len == 0) ? UBX_CKA : UBX_PAYLOAD; }
        } else if (ubx_state == UBX_PAYLOAD) { ubx_payload[ubx_payload_idx++] = c; ubx_cka += c; ubx_ckb += ubx_cka; if (ubx_payload_idx >= ubx_payload_len) ubx_state = UBX_CKA; }
        else if (ubx_state == UBX_CKA) { if (c == ubx_cka) ubx_state = UBX_CKB; else ubx_state = UBX_IDLE; }
        else if (ubx_state == UBX_CKB) {
            if (c == ubx_ckb && ubx_class == 0x01 && ubx_id == 0x07) { GPS_Parse_UBX_PVT(ubx_payload); strcpy(current_gps_data.last_raw, "UBX-NAV-PVT Decoded"); }
            ubx_state = UBX_IDLE;
        }
    }
}

GPS_Data_t GPS_GetData(void) { GPS_Data_t data = current_gps_data; current_gps_data.updated = false; return data; }
