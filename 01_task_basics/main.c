/*
 * FreeRTOS 教程 - 第1课: 任务基础与等待原理
 *
 * 本示例不只演示如何创建任务，还直接把“等待模块”跑出来：
 * 1. RelativeDelayTask 使用 vTaskDelay() 进入 Blocked 状态
 * 2. AbsoluteDelayTask 使用 vTaskDelayUntil() 按绝对 tick 周期唤醒
 * 3. MonitorTask 以更高优先级采样其他任务状态，观察 Ready/Blocked/Suspended
 *
 * 观察重点：
 * - 任务等待不是忙等，而是被移入 delayed list
 * - 到达唤醒 tick 后，任务会被移回 ready list
 * - vTaskDelayUntil() 通过固定目标 tick 减少周期漂移
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

extern void uart_init(void);

static TaskHandle_t xRelativeDelayTaskHandle = NULL;
static TaskHandle_t xAbsoluteDelayTaskHandle = NULL;

static const char *prvTaskStateToString(eTaskState state)
{
    switch (state)
    {
        case eRunning:
            return "Running";
        case eReady:
            return "Ready";
        case eBlocked:
            return "Blocked";
        case eSuspended:
            return "Suspended";
        case eDeleted:
            return "Deleted";
        default:
            return "Invalid";
    }
}

/*-----------------------------------------------------------
 * 相对延时任务: 当前 tick + 固定延时
 *-----------------------------------------------------------*/
static void vRelativeDelayTask(void *pvParameters)
{
    const TickType_t xDelayTicks = pdMS_TO_TICKS(300);
    uint32_t round = 0;

    (void)pvParameters;

    for (;;)
    {
        TickType_t xNow = xTaskGetTickCount();

        round++;
        printf("[Relative] round=%lu running tick=%lu, vTaskDelay(%lu) -> wake around tick=%lu\n",
               (unsigned long)round,
               (unsigned long)xNow,
               (unsigned long)xDelayTicks,
               (unsigned long)(xNow + xDelayTicks));

        vTaskDelay(xDelayTicks);
    }
}

/*-----------------------------------------------------------
 * 绝对延时任务: 以固定周期唤醒，避免累积漂移
 *-----------------------------------------------------------*/
static void vAbsoluteDelayTask(void *pvParameters)
{
    const TickType_t xPeriodTicks = pdMS_TO_TICKS(500);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t round = 0;

    (void)pvParameters;

    for (;;)
    {
        TickType_t xNow = xTaskGetTickCount();
        TickType_t xNextWake = xLastWakeTime + xPeriodTicks;

        round++;
        printf("[Absolute] round=%lu running tick=%lu, next target tick=%lu via vTaskDelayUntil()\n",
               (unsigned long)round,
               (unsigned long)xNow,
               (unsigned long)xNextWake);

        vTaskDelayUntil(&xLastWakeTime, xPeriodTicks);
    }
}

/*-----------------------------------------------------------
 * 监控任务: 更高优先级采样其他任务状态
 *-----------------------------------------------------------*/
static void vMonitorTask(void *pvParameters)
{
    const TickType_t xSamplePeriod = pdMS_TO_TICKS(100);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t sample = 0;

    (void)pvParameters;

    for (;;)
    {
        TickType_t xNow = xTaskGetTickCount();
        eTaskState xRelativeState = eTaskGetState(xRelativeDelayTaskHandle);
        eTaskState xAbsoluteState = eTaskGetState(xAbsoluteDelayTaskHandle);

        sample++;
        printf("[Monitor ] sample=%lu tick=%lu relative=%s absolute=%s\n",
               (unsigned long)sample,
               (unsigned long)xNow,
               prvTaskStateToString(xRelativeState),
               prvTaskStateToString(xAbsoluteState));

        if (sample == 18U)
        {
            printf("[Monitor ] Compare with suspended state: suspend both worker tasks now.\n");
            vTaskSuspend(xRelativeDelayTaskHandle);
            vTaskSuspend(xAbsoluteDelayTaskHandle);
            printf("[Monitor ] after suspend: relative=%s absolute=%s\n",
                   prvTaskStateToString(eTaskGetState(xRelativeDelayTaskHandle)),
                   prvTaskStateToString(eTaskGetState(xAbsoluteDelayTaskHandle)));
            printf("[Monitor ] Demo complete. Idle task keeps the system alive until QEMU timeout.\n");
            vTaskSuspend(NULL);
        }

        vTaskDelayUntil(&xLastWakeTime, xSamplePeriod);
    }
}

/*-----------------------------------------------------------
 * main 函数
 *-----------------------------------------------------------*/
int main(void)
{
    BaseType_t ret;

    uart_init();

    printf("====================================================\n");
    printf("  FreeRTOS Lesson 01: Task Basics + Wait Internals\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("====================================================\n\n");
    printf("Monitor(pri=3, 100ms) samples worker states before they run.\n");
    printf("RelativeDelay(pri=2) uses vTaskDelay(300ms).\n");
    printf("AbsoluteDelay(pri=1) uses vTaskDelayUntil(500ms).\n\n");

    ret = xTaskCreate(vMonitorTask,
                      "Monitor",
                      256,
                      NULL,
                      3,
                      NULL);
    if (ret != pdPASS)
    {
        printf("ERROR: Failed to create Monitor task!\n");
    }

    ret = xTaskCreate(vRelativeDelayTask,
                      "RelDelay",
                      256,
                      NULL,
                      2,
                      &xRelativeDelayTaskHandle);
    if (ret != pdPASS)
    {
        printf("ERROR: Failed to create RelativeDelay task!\n");
    }

    ret = xTaskCreate(vAbsoluteDelayTask,
                      "AbsDelay",
                      256,
                      NULL,
                      1,
                      &xAbsoluteDelayTaskHandle);
    if (ret != pdPASS)
    {
        printf("ERROR: Failed to create AbsoluteDelay task!\n");
    }

    printf("Tasks created. Starting scheduler...\n\n");

    vTaskStartScheduler();

    printf("ERROR: Scheduler returned! Insufficient memory?\n");
    for (;;)
    {
    }

    return 0;
}
