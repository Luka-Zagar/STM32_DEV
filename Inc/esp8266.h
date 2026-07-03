/**
 * @file  esp8266.h
 * @brief ESP8266-01S AT Command Driver (MQTT via ESP-AT 2.x firmware)
 */
#ifndef ESP8266_H
#define ESP8266_H

#include <stdint.h>
#include <stdbool.h>

/* ── Configuration ──────────────────────────────────────────────────────── */
#define WIFI_SSID       "WiFi 5G soba"
#define WIFI_PASS       "++++++++++"
#define MQTT_BROKER_IP  "mqtt.turboorca.com"
#define MQTT_BROKER_PORT 49152
#define MQTT_USER       "lukazagar"
#define MQTT_PASS       "++++++++++"
#define MQTT_CLIENT_ID  "EURUS-01"
#define MQTT_TOPIC_DATA "project/eko-sonda"

/* ── State Machine ──────────────────────────────────────────────────────── */
typedef enum {
    ESP_STATE_RESET = 0,
    ESP_STATE_WAIT_READY,
    ESP_STATE_SET_MODE,
    ESP_STATE_WAIT_MODE_OK,
    ESP_STATE_CONNECT_WIFI,
    ESP_STATE_WAIT_WIFI,
    ESP_STATE_MQTT_CFG,
    ESP_STATE_WAIT_MQTT_CFG,
    ESP_STATE_MQTT_CONNECT,
    ESP_STATE_WAIT_MQTT,
    ESP_STATE_READY,
    ESP_STATE_ERROR
} ESP_State_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

void ESP_Init(void);
void ESP_Task(void);   /* Call from scheduler every 100ms */
bool ESP_IsReady(void);
void ESP_Publish_SensorData(float t, float p, float h, float g);

#endif /* ESP8266_H */
