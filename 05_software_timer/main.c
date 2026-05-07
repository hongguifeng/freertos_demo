/*
 * FreeRTOS 教程 - 第5课: 软件定时器守护任务
 *
 * 观察重点：
 * 1. timer callback 在 daemon task 中执行，不在调用者任务中执行
 * 2. xTimerStart/xTimerChangePeriod/xTimerStop 只是向 xTimerQueue 发送命令
 * 3. xTimerPendFunctionCall() 与普通 timer 命令复用同一条守护任务执行通道
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

extern void uart_init(void);

static TimerHandle_t xPeriodicTimer = NULL;
static TimerHandle_t xOneShotTimer = NULL;

static volatile uint32_t ulPeriodicCount = 0;
static volatile uint32_t ulOneShotCount = 0;
static volatile uint32_t ulPendedCount = 0;

/*-----------------------------------------------------------
 * Timer callbacks and deferred function
 *-----------------------------------------------------------*/
static void vPeriodicTimerCallback(TimerHandle_t xTimer)
{
    ulPeriodicCount++;
    printf("[Periodic ] fired at tick=%lu count=%lu daemon-pri=%lu timer-id=%lu\n",
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)ulPeriodicCount,
           (unsigned long)uxTaskPriorityGet(NULL),
           (unsigned long)pvTimerGetTimerID(xTimer));
}

static void vOneShotTimerCallback(TimerHandle_t xTimer)
{
    ulOneShotCount++;
    printf("[OneShot  ] fired at tick=%lu count=%lu daemon-pri=%lu timer-id=%lu\n",
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)ulOneShotCount,
           (unsigned long)uxTaskPriorityGet(NULL),
           (unsigned long)pvTimerGetTimerID(xTimer));
}

static void vDeferredFunction(void *pvParameter1, uint32_t ulParameter2)
{
    const char *pcReason = (const char *)pvParameter1;

    ulPendedCount++;
    printf("[Pended   ] run at tick=%lu count=%lu reason=%s arg=%lu daemon-pri=%lu\n",
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)ulPendedCount,
           pcReason,
           (unsigned long)ulParameter2,
           (unsigned long)uxTaskPriorityGet(NULL));
}

/*-----------------------------------------------------------
 * Controller task
 *-----------------------------------------------------------*/
static void vTimerControllerTask(void *pvParameters)
{
    BaseType_t xResult;

    (void)pvParameters;

    xPeriodicTimer = xTimerCreate("Periodic",
                                  pdMS_TO_TICKS(250),
                                  pdTRUE,
                                  (void *)1,
                                  vPeriodicTimerCallback);

    xOneShotTimer = xTimerCreate("OneShot",
                                 pdMS_TO_TICKS(600),
                                 pdFALSE,
                                 (void *)2,
                                 vOneShotTimerCallback);

    if ((xPeriodicTimer == NULL) || (xOneShotTimer == NULL))
    {
        printf("ERROR: Timer creation failed!\n");
        vTaskDelete(NULL);
    }

    printf("[Ctrl     ] Timer daemon priority=%u, command queue length=%u\n",
           (unsigned int)configTIMER_TASK_PRIORITY,
           (unsigned int)configTIMER_QUEUE_LENGTH);
    printf("[Ctrl     ] Start Periodic(250ms) and OneShot(600ms)\n");

    xTimerStart(xPeriodicTimer, 0);
    xTimerStart(xOneShotTimer, 0);
    xTimerPendFunctionCall(vDeferredFunction, (void *)"after-start", 1U, 0);

    vTaskDelay(pdMS_TO_TICKS(950));

    printf("\n[Ctrl     ] Change Periodic to 400ms and reset OneShot\n");
    xTimerChangePeriod(xPeriodicTimer, pdMS_TO_TICKS(400), 0);
    xTimerReset(xOneShotTimer, 0);
    xTimerPendFunctionCall(vDeferredFunction, (void *)"after-change", 2U, 0);

    vTaskDelay(pdMS_TO_TICKS(1100));

    printf("\n[Ctrl     ] Stop Periodic and restart OneShot\n");
    xTimerStop(xPeriodicTimer, 0);
    xTimerStart(xOneShotTimer, 0);
    xTimerPendFunctionCall(vDeferredFunction, (void *)"after-stop", 3U, 0);

    vTaskDelay(pdMS_TO_TICKS(800));

    printf("\n[Ctrl     ] Delete both timers\n");
    xResult = xTimerDelete(xPeriodicTimer, 0);
    printf("[Ctrl     ] xTimerDelete(periodic) -> %s\n", xResult == pdPASS ? "pdPASS" : "pdFAIL");
    xResult = xTimerDelete(xOneShotTimer, 0);
    printf("[Ctrl     ] xTimerDelete(one-shot) -> %s\n", xResult == pdPASS ? "pdPASS" : "pdFAIL");

    printf("\n[Ctrl     ] periodic fires=%lu one-shot fires=%lu pended calls=%lu\n",
           (unsigned long)ulPeriodicCount,
           (unsigned long)ulOneShotCount,
           (unsigned long)ulPendedCount);
    printf("[Ctrl     ] Demo complete. Timer daemon will block on its queue/list until QEMU timeout.\n");

    vTaskSuspend(NULL);
}

int main(void)
{
    uart_init();

    printf("====================================================\n");
    printf("  FreeRTOS Lesson 05: Software Timer Internals\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("====================================================\n\n");

    xTaskCreate(vTimerControllerTask, "TimerCtrl", 512, NULL, 2, NULL);

    vTaskStartScheduler();
    for (;;)
    {
    }

    return 0;
}
