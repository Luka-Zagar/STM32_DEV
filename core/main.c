#include "system_clock.h"
#include "systick.h"
#include "scheduler.h"
#include "task_heartbeat.h"
#include "task_console.h"
#include "task_status.h"
#include "task_gps.h"
#include "task_wifi.h"
#include "task_sd.h"
#include "task_acquire.h"
#include "task_logger.h"
#include "task_led.h"
#include "task_battery.h"
#include "task_imu.h"

int main(void) {
    SysTick_Init();
    SCH_Init();

    Heartbeat_Task_Init();
    int heartbeat_task_id = SCH_Add_Task(Heartbeat_Task, 0, 500);

    Console_Task_Init(heartbeat_task_id);
    int console_task_id = SCH_Add_Task(Console_Task, 0, 10);

    Task_GPS_Init();
    int gps_task_id = SCH_Add_Task(Task_GPS, 0, 10);

    Task_WiFi_Init();
    int wifi_task_id = SCH_Add_Task(Task_WiFi, 0, 20);

    Task_SD_Init();
    SCH_Add_Task(Task_SD_Button, 0, 20); /* 20ms - responsive without over-polling a plain GPIO */

    Task_Battery_Init();
    SCH_Add_Task(Task_Battery, 0, 500); /* faster than Task_Acquire's 1Hz so a reading is always fresh when sampled */

    Task_IMU_Init(); /* after Task_Battery_Init() - shares I2C1, which that call already brought up */
    SCH_Add_Task(Task_IMU, 0, 200); /* 5Hz - plenty for vibration trend logging at 1Hz acquire rate */

    Task_Acquire_Init();
    int acquire_task_id = SCH_Add_Task(Task_Acquire, 0, 1000); /* 1Hz - matches GPS's own update rate */

    Task_Logger_Init();
    int logger_task_id = SCH_Add_Task(Task_Logger, 0, 100); /* drains whatever Task_Acquire queued */

    Task_LED_Init();
    SCH_Add_Task(Task_LED, 0, 50); /* 50ms - fine enough for the fastest pattern (~3 flash/s) */

    int status_task_id = SCH_Add_Task(Task_Status, 3000, 3000);
    Task_Status_Init(heartbeat_task_id, console_task_id, gps_task_id, wifi_task_id,
                      acquire_task_id, logger_task_id);
    Console_Set_Status_Task(status_task_id);

    while (1) {
        SCH_Dispatch();
    }
}
