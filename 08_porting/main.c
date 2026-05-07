/*
 * FreeRTOS 教程 - 第8课: Cortex-M3 Port Flow Demo
 *
 * 观察重点：
 * 1. 第一个任务在 scheduler 启动后立即运行，证明 SVC 启动链路已完成
 * 2. YieldA / YieldB 通过 taskYIELD() 主动请求 PendSV 切换
 * 3. DelayTask 通过 vTaskDelay() 进入 Blocked，再由 SysTick 驱动唤醒
 */

#include <stdarg.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

extern void uart_init(void);

static TaskHandle_t xYieldATaskHandle = NULL;
static TaskHandle_t xYieldBTaskHandle = NULL;
static TaskHandle_t xDelayTaskHandle = NULL;

static volatile uint32_t ulYieldTasksDone = 0;
static volatile uint32_t ulDelayTaskDone = 0;

static void prvPrintLocked(const char *pcFormat, ...)
{
    va_list xArgs;

    taskENTER_CRITICAL();
    va_start(xArgs, pcFormat);
    vprintf(pcFormat, xArgs);
    va_end(xArgs);
    taskEXIT_CRITICAL();
}

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

static const char *prvHandleStateToString(TaskHandle_t xHandle)
{
    if (xHandle == NULL)
    {
        return "n/a";
    }

    return prvTaskStateToString(eTaskGetState(xHandle));
}

/*-----------------------------------------------------------
 * Same-priority yield tasks: demonstrate PendSV without waiting for a tick
 *-----------------------------------------------------------*/
static void vYieldTask(void *pvParameters)
{
    const char *pcName = (const char *)pvParameters;
    uint32_t ulRound;

    for (ulRound = 1; ulRound <= 3U; ulRound++)
    {
         prvPrintLocked("[%s] round=%lu tick=%lu before taskYIELD()\n",
                  pcName,
                  (unsigned long)ulRound,
                  (unsigned long)xTaskGetTickCount());

        taskYIELD();

         prvPrintLocked("[%s] round=%lu tick=%lu after taskYIELD()\n",
                  pcName,
                  (unsigned long)ulRound,
                  (unsigned long)xTaskGetTickCount());
    }

        prvPrintLocked("[%s] finished at tick=%lu\n",
                 pcName,
                 (unsigned long)xTaskGetTickCount());
    ulYieldTasksDone++;
    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * Delayed task: demonstrate SysTick -> xTaskIncrementTick -> PendSV path
 *-----------------------------------------------------------*/
static void vDelayTask(void *pvParameters)
{
    uint32_t ulRound;

    (void)pvParameters;

    for (ulRound = 1; ulRound <= 2U; ulRound++)
    {
        TickType_t xNow = xTaskGetTickCount();

        prvPrintLocked("[Delay   ] round=%lu running at tick=%lu, call vTaskDelay(200ms) -> wake around %lu\n",
                   (unsigned long)ulRound,
                   (unsigned long)xNow,
                   (unsigned long)(xNow + pdMS_TO_TICKS(200)));

        vTaskDelay(pdMS_TO_TICKS(200));

        prvPrintLocked("[Delay   ] round=%lu resumed at tick=%lu after SysTick unblocked it\n",
                   (unsigned long)ulRound,
                   (unsigned long)xTaskGetTickCount());
    }

    ulDelayTaskDone = 1U;
    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * Coordinator: first task after scheduler start
 *-----------------------------------------------------------*/
static void vPortFlowCoordinator(void *pvParameters)
{
    TickType_t xLastSample = 0;

    (void)pvParameters;

        prvPrintLocked("[First   ] first task entered at tick=%lu\n",
                 (unsigned long)xTaskGetTickCount());
        prvPrintLocked("[First   ] Reaching here means xPortStartScheduler() configured the port, prvPortStartFirstTask() executed svc 0, and vPortSVCHandler restored the initial task stack.\n\n");

        prvPrintLocked("[First   ] configTICK_RATE_HZ=%u, kernel IRQ priority=%u, max syscall IRQ priority=%u\n\n",
                 (unsigned int)configTICK_RATE_HZ,
                 (unsigned int)configKERNEL_INTERRUPT_PRIORITY,
                 (unsigned int)configMAX_SYSCALL_INTERRUPT_PRIORITY);

    xTaskCreate(vYieldTask, "YieldA", 256, (void *)"YieldA ", 2, &xYieldATaskHandle);
    xTaskCreate(vYieldTask, "YieldB", 256, (void *)"YieldB ", 2, &xYieldBTaskHandle);
    xTaskCreate(vDelayTask, "Delay",  256, NULL,              1, &xDelayTaskHandle);

    while ((ulYieldTasksDone < 2U) || (ulDelayTaskDone == 0U))
    {
        TickType_t xNow = xTaskGetTickCount();

        if ((xNow - xLastSample) >= pdMS_TO_TICKS(100))
        {
                 prvPrintLocked("[Coord   ] tick=%lu yieldA=%s yieldB=%s delay=%s\n",
                          (unsigned long)xNow,
                          prvHandleStateToString(xYieldATaskHandle),
                          prvHandleStateToString(xYieldBTaskHandle),
                          prvHandleStateToString(xDelayTaskHandle));
            xLastSample = xNow;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    prvPrintLocked("\n[Coord   ] Demo complete. The observed paths were:\n");
    prvPrintLocked("[Coord   ] 1) SVC started the first task.\n");
    prvPrintLocked("[Coord   ] 2) taskYIELD() requested PendSV for voluntary switches.\n");
    prvPrintLocked("[Coord   ] 3) SysTick advanced the tick and woke delayed tasks.\n");
    prvPrintLocked("[Coord   ] System now idles until QEMU timeout.\n");

    vTaskSuspend(NULL);
}

int main(void)
{
    uart_init();

    printf("====================================================\n");
    printf("  FreeRTOS Lesson 08: Cortex-M3 Port Flow\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("====================================================\n\n");

    xTaskCreate(vPortFlowCoordinator, "Coord", 512, NULL, 3, NULL);

    vTaskStartScheduler();
    for (;;)
    {
    }

    return 0;
}
