/*
 * FreeRTOS 教程 - 第4课: 信号量、互斥锁与优先级继承
 *
 * Stage A: Binary Semaphore
 *   - 观察二值信号量的同步语义
 *   - 等待者在 token 为 0 时进入 Blocked
 *
 * Stage B: Counting Semaphore
 *   - 观察计数信号量作为资源池使用
 *   - 第三个 worker 会因没有剩余 token 而阻塞
 *
 * Stage C: Mutex
 *   - 观察 mutex 的 owner 语义
 *   - 观察高优先级任务等待时，低优先级持锁任务的优先级继承
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

extern void uart_init(void);

typedef enum
{
    DEMO_PHASE_BINARY = 0,
    DEMO_PHASE_COUNTING,
    DEMO_PHASE_MUTEX,
    DEMO_PHASE_DONE
} DemoPhase_t;

static SemaphoreHandle_t xBinarySem = NULL;
static SemaphoreHandle_t xCountingSem = NULL;
static SemaphoreHandle_t xMutex = NULL;

static TaskHandle_t xBinaryWaiterHandle = NULL;
static TaskHandle_t xBinaryGiverHandle = NULL;
static TaskHandle_t xCountWorkerAHandle = NULL;
static TaskHandle_t xCountWorkerBHandle = NULL;
static TaskHandle_t xCountWorkerCHandle = NULL;
static TaskHandle_t xLowTaskHandle = NULL;
static TaskHandle_t xMediumTaskHandle = NULL;
static TaskHandle_t xHighTaskHandle = NULL;

static volatile DemoPhase_t xCurrentPhase = DEMO_PHASE_BINARY;
static volatile uint32_t ulBinaryEventsHandled = 0;
static volatile uint32_t ulCountingWorkersDone = 0;
static volatile uint32_t ulMutexPhaseDone = 0;

typedef struct
{
    const char *pcName;
    TickType_t xStartDelay;
} CountingWorkerConfig_t;

static const CountingWorkerConfig_t xCountWorkerAConfig = { "CountA", 0 };
static const CountingWorkerConfig_t xCountWorkerBConfig = { "CountB", pdMS_TO_TICKS(5) };
static const CountingWorkerConfig_t xCountWorkerCConfig = { "CountC", pdMS_TO_TICKS(10) };

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
 * Stage A: Binary semaphore
 *-----------------------------------------------------------*/
static void vBinaryWaiterTask(void *pvParameters)
{
    uint32_t eventIndex;

    (void)pvParameters;

    for (eventIndex = 1; eventIndex <= 3U; eventIndex++)
    {
        TickType_t xStartTick = xTaskGetTickCount();
        TickType_t xEndTick;

        printf("[Waiter ] wait event=%lu at tick=%lu (sem count=%lu)\n",
               (unsigned long)eventIndex,
               (unsigned long)xStartTick,
               (unsigned long)uxSemaphoreGetCount(xBinarySem));

        xSemaphoreTake(xBinarySem, portMAX_DELAY);
        xEndTick = xTaskGetTickCount();
        ulBinaryEventsHandled = eventIndex;

        printf("[Waiter ] got event=%lu at tick=%lu after waiting %lu tick(s)\n",
               (unsigned long)eventIndex,
               (unsigned long)xEndTick,
               (unsigned long)(xEndTick - xStartTick));
    }

    vTaskDelete(NULL);
}

static void vBinaryGiverTask(void *pvParameters)
{
    uint32_t eventIndex;

    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(250));

    for (eventIndex = 1; eventIndex <= 3U; eventIndex++)
    {
        TickType_t xNow = xTaskGetTickCount();

        printf("[Giver  ] give event=%lu at tick=%lu (count before=%lu)\n",
               (unsigned long)eventIndex,
               (unsigned long)xNow,
               (unsigned long)uxSemaphoreGetCount(xBinarySem));
        xSemaphoreGive(xBinarySem);

        if (eventIndex < 3U)
        {
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * Stage B: Counting semaphore
 *-----------------------------------------------------------*/
static void vCountingWorkerTask(void *pvParameters)
{
    const CountingWorkerConfig_t *pxConfig = (const CountingWorkerConfig_t *)pvParameters;
    const char *pcName = pxConfig->pcName;
    TickType_t xStartTick = xTaskGetTickCount();
    TickType_t xTakeTick;

    if (pxConfig->xStartDelay > 0)
    {
        vTaskDelay(pxConfig->xStartDelay);
        xStartTick = xTaskGetTickCount();
    }

    printf("[%s] request token at tick=%lu (count=%lu)\n",
           pcName,
           (unsigned long)xStartTick,
           (unsigned long)uxSemaphoreGetCount(xCountingSem));

    xSemaphoreTake(xCountingSem, portMAX_DELAY);
    xTakeTick = xTaskGetTickCount();

    printf("[%s] got token at tick=%lu after waiting %lu tick(s), count now=%lu\n",
           pcName,
           (unsigned long)xTakeTick,
           (unsigned long)(xTakeTick - xStartTick),
           (unsigned long)uxSemaphoreGetCount(xCountingSem));

    vTaskDelay(pdMS_TO_TICKS(400));

    xSemaphoreGive(xCountingSem);
    printf("[%s] released token at tick=%lu, count now=%lu\n",
           pcName,
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)uxSemaphoreGetCount(xCountingSem));

    ulCountingWorkersDone++;
    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * Stage C: Mutex with priority inheritance
 *-----------------------------------------------------------*/
static void vLowPriorityMutexTask(void *pvParameters)
{
    TickType_t xEndTick;
    TickType_t xLastReport;

    (void)pvParameters;

    xSemaphoreTake(xMutex, portMAX_DELAY);
    xEndTick = xTaskGetTickCount() + pdMS_TO_TICKS(900);
    xLastReport = xTaskGetTickCount();

    printf("[Low    ] took mutex at tick=%lu, priority=%lu\n",
           (unsigned long)xLastReport,
           (unsigned long)uxTaskPriorityGet(NULL));

    while (xTaskGetTickCount() < xEndTick)
    {
        TickType_t xNow = xTaskGetTickCount();

        if ((xNow - xLastReport) >= pdMS_TO_TICKS(100))
        {
            printf("[Low    ] still holding mutex at tick=%lu, current priority=%lu\n",
                   (unsigned long)xNow,
                   (unsigned long)uxTaskPriorityGet(NULL));
            xLastReport = xNow;
        }
    }

    printf("[Low    ] release mutex at tick=%lu, current priority=%lu\n",
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)uxTaskPriorityGet(NULL));
    xSemaphoreGive(xMutex);
    printf("[Low    ] after give, priority restored to %lu\n",
           (unsigned long)uxTaskPriorityGet(NULL));

    vTaskDelete(NULL);
}

static void vMediumPriorityTask(void *pvParameters)
{
    TickType_t xEndTick;
    TickType_t xLastReport;

    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(120));
    xEndTick = xTaskGetTickCount() + pdMS_TO_TICKS(700);
    xLastReport = xTaskGetTickCount();

    printf("[Medium ] becomes ready at tick=%lu and wants CPU time\n",
           (unsigned long)xLastReport);

    while (xTaskGetTickCount() < xEndTick)
    {
        TickType_t xNow = xTaskGetTickCount();

        if ((xNow - xLastReport) >= pdMS_TO_TICKS(120))
        {
            printf("[Medium ] running at tick=%lu, priority=%lu\n",
                   (unsigned long)xNow,
                   (unsigned long)uxTaskPriorityGet(NULL));
            xLastReport = xNow;
        }
    }

    printf("[Medium ] work finished at tick=%lu\n",
           (unsigned long)xTaskGetTickCount());
    vTaskDelete(NULL);
}

static void vHighPriorityMutexTask(void *pvParameters)
{
    TickType_t xStartTick;
    TickType_t xTakeTick;

    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(220));
    xStartTick = xTaskGetTickCount();

    printf("[High   ] try take mutex at tick=%lu\n",
           (unsigned long)xStartTick);

    xSemaphoreTake(xMutex, portMAX_DELAY);
    xTakeTick = xTaskGetTickCount();

    printf("[High   ] got mutex at tick=%lu after waiting %lu tick(s)\n",
           (unsigned long)xTakeTick,
           (unsigned long)(xTakeTick - xStartTick));

    xSemaphoreGive(xMutex);
    printf("[High   ] gave mutex and completes phase C\n");
    ulMutexPhaseDone = 1U;

    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * Coordinator / Monitor
 *-----------------------------------------------------------*/
static void prvRunBinaryPhase(void)
{
    printf("\n--- Stage A: Binary Semaphore ---\n");
    printf("[Coord  ] Waiter blocks when count=0. Giver releases three events.\n\n");

    ulBinaryEventsHandled = 0;
    xCurrentPhase = DEMO_PHASE_BINARY;

    xTaskCreate(vBinaryWaiterTask, "BinWait", 256, NULL, 2, &xBinaryWaiterHandle);
    xTaskCreate(vBinaryGiverTask,  "BinGive", 256, NULL, 1, &xBinaryGiverHandle);

    while (ulBinaryEventsHandled < 3U)
    {
        printf("[Coord/B ] tick=%lu count=%lu waiter=%s giver=%s\n",
               (unsigned long)xTaskGetTickCount(),
               (unsigned long)uxSemaphoreGetCount(xBinarySem),
               prvHandleStateToString(xBinaryWaiterHandle),
               prvHandleStateToString(xBinaryGiverHandle));
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelay(pdMS_TO_TICKS(120));
    xBinaryWaiterHandle = NULL;
    xBinaryGiverHandle = NULL;
}

static void prvRunCountingPhase(void)
{
    printf("\n--- Stage B: Counting Semaphore ---\n");
    printf("[Coord  ] Resource pool has 2 tokens. WorkerC must wait until one token is returned.\n\n");

    ulCountingWorkersDone = 0;
    xCurrentPhase = DEMO_PHASE_COUNTING;

    xTaskCreate(vCountingWorkerTask, "CountA", 256, (void *)&xCountWorkerAConfig, 2, &xCountWorkerAHandle);
    xTaskCreate(vCountingWorkerTask, "CountB", 256, (void *)&xCountWorkerBConfig, 2, &xCountWorkerBHandle);
    xTaskCreate(vCountingWorkerTask, "CountC", 256, (void *)&xCountWorkerCConfig, 2, &xCountWorkerCHandle);

    while (ulCountingWorkersDone < 3U)
    {
        printf("[Coord/C ] tick=%lu count=%lu A=%s B=%s C=%s\n",
               (unsigned long)xTaskGetTickCount(),
               (unsigned long)uxSemaphoreGetCount(xCountingSem),
               prvHandleStateToString(xCountWorkerAHandle),
               prvHandleStateToString(xCountWorkerBHandle),
               prvHandleStateToString(xCountWorkerCHandle));
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelay(pdMS_TO_TICKS(120));
    xCountWorkerAHandle = NULL;
    xCountWorkerBHandle = NULL;
    xCountWorkerCHandle = NULL;
}

static void prvRunMutexPhase(void)
{
    printf("\n--- Stage C: Mutex + Priority Inheritance ---\n");
    printf("[Coord  ] Low takes mutex first, Medium preempts, High then blocks and forces Low to inherit priority.\n\n");

    ulMutexPhaseDone = 0;
    xCurrentPhase = DEMO_PHASE_MUTEX;

    xTaskCreate(vLowPriorityMutexTask, "Low",    256, NULL, 1, &xLowTaskHandle);
    xTaskCreate(vMediumPriorityTask,   "Medium", 256, NULL, 2, &xMediumTaskHandle);
    xTaskCreate(vHighPriorityMutexTask, "High",  256, NULL, 3, &xHighTaskHandle);

    while (ulMutexPhaseDone == 0U)
    {
        printf("[Coord/M ] tick=%lu low=%s(pri=%lu) medium=%s high=%s\n",
               (unsigned long)xTaskGetTickCount(),
               prvHandleStateToString(xLowTaskHandle),
               (unsigned long)uxTaskPriorityGet(xLowTaskHandle),
               prvHandleStateToString(xMediumTaskHandle),
               prvHandleStateToString(xHighTaskHandle));
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelay(pdMS_TO_TICKS(120));
    xLowTaskHandle = NULL;
    xMediumTaskHandle = NULL;
    xHighTaskHandle = NULL;
}

static void vCoordinatorTask(void *pvParameters)
{
    (void)pvParameters;

    prvRunBinaryPhase();
    prvRunCountingPhase();
    prvRunMutexPhase();

    xCurrentPhase = DEMO_PHASE_DONE;
    printf("\n[Coord  ] Demo complete. System stays in idle task until QEMU timeout.\n");
    vTaskSuspend(NULL);
}

int main(void)
{
    uart_init();

    printf("====================================================\n");
    printf("  FreeRTOS Lesson 04: Semaphore + Mutex Internals\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("====================================================\n\n");

    xBinarySem = xSemaphoreCreateBinary();
    xCountingSem = xSemaphoreCreateCounting(2, 2);
    xMutex = xSemaphoreCreateMutex();

    if ((xBinarySem == NULL) || (xCountingSem == NULL) || (xMutex == NULL))
    {
        printf("ERROR: Failed to create semaphores or mutex!\n");
        for (;;)
        {
        }
    }

    printf("Binary semaphore count starts at %lu.\n",
           (unsigned long)uxSemaphoreGetCount(xBinarySem));
    printf("Counting semaphore max=2, initial count=%lu.\n",
           (unsigned long)uxSemaphoreGetCount(xCountingSem));
    printf("Mutex is available and will demonstrate priority inheritance.\n\n");

    xTaskCreate(vCoordinatorTask, "Coord", 512, NULL, 4, NULL);

    vTaskStartScheduler();
    for (;;)
    {
    }

    return 0;
}
