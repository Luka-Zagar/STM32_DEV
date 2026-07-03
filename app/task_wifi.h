#ifndef TASK_WIFI_H
#define TASK_WIFI_H

/* Initializes USART3 for the ESP8266 (ESP-01S, PB10/PB11, 115200 baud)
 * and kicks off the AT bring-up check (plain "AT", expect "OK") - proves
 * the UART link and module power are good before anything WiFi/MQTT
 * specific is attempted. */
void Task_WiFi_Init(void);

/* Register with the scheduler (e.g. every 20ms). Drives the AT command
 * state machine; currently just the bring-up check, retried on
 * failure/timeout. */
void Task_WiFi(void);

/* Human-readable bring-up status for the console/status task. */
const char *Task_WiFi_Status_Str(void);

#endif /* TASK_WIFI_H */
