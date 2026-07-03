#include "system_clock.h"
#include "systick.h"
#include "scheduler.h"
#include "task_heartbeat.h"
#include "task_console.h"
#include "task_status.h"

int main(void) {
    SysTick_Init();
    SCH_Init();

    Heartbeat_Task_Init();
    int heartbeat_task_id = SCH_Add_Task(Heartbeat_Task, 0, 500);

    Console_Task_Init(heartbeat_task_id);
    int console_task_id = SCH_Add_Task(Console_Task, 0, 10);

    Task_Status_Init(heartbeat_task_id, console_task_id);
    SCH_Add_Task(Task_Status, 3000, 3000);

    while (1) {
        SCH_Dispatch();
    }
}
