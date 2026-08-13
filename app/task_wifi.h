#ifndef TASK_WIFI_H
#define TASK_WIFI_H

#include <stdint.h>

/* Initializes USART3 for the WiFi module (ESP32-WROOM-32, PB10/PB11,
 * 115200 baud - see docs/pinout.md) and kicks off the connect sequence:
 * AT bring-up -> station mode -> WiFi join -> MQTT client config ->
 * MQTT connect -> periodic publish of the current GPS fix, using the
 * ESP32's native AT+MQTTxxx command set (devices/esp32.h). Credentials/
 * broker details come from wifi_config.h (gitignored, not committed -
 * see wifi_config.h.example). */
void Task_WiFi_Init(void);

/* Register with the scheduler (e.g. every 20ms). Drives the AT command
 * state machine end to end; never blocks on a disconnected/unpowered
 * module or a broker that's unreachable - every step has its own
 * timeout and retries indefinitely. */
void Task_WiFi(void);

/* Human-readable state for the console/status task. */
const char *Task_WiFi_Status_Str(void);

/* How many GPS fixes have been successfully published this boot. */
uint32_t Task_WiFi_Publish_Count(void);

/* Raw AT+GMR response captured once at boot (firmware/AT version info). */
const char *Task_WiFi_Firmware_Str(void);

/* True once AT+MQTTCONN has confirmed a live broker connection (mirrors
 * devices/esp32.h's esp32_mqtt_is_connected() - exposed here so the
 * console doesn't need to reach into the device layer directly). */
int Task_WiFi_MQTT_Connected(void);

#endif /* TASK_WIFI_H */
