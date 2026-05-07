/*
 * FreeRTOS 教程 - 第6课: Event Groups 原理示例
 *
 * Stage A:
 *   - WaitAny: 等 NET 或 STORAGE 任意一位
 *   - WaitAll: 等 STORAGE 与 SENSOR 全部位，并在退出时清位
 *
 * Stage B:
 *   - 三个任务在 xEventGroupSync() 上汇合
 *   - 演示 rendezvous 的 set + wait-all + clear-on-exit 原子语义
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

extern void uart_init(void);

#define EVT_NET_READY      ( 1U << 0 )
#define EVT_STORAGE_READY  ( 1U << 1 )
#define EVT_SENSOR_READY   ( 1U << 2 )

#define EVT_WAIT_ANY_MASK  ( EVT_NET_READY | EVT_STORAGE_READY )
#define EVT_WAIT_ALL_MASK  ( EVT_STORAGE_READY | EVT_SENSOR_READY )

#define EVT_SYNC_A         ( 1U << 0 )
#define EVT_SYNC_B         ( 1U << 1 )
#define EVT_SYNC_C         ( 1U << 2 )
#define EVT_SYNC_ALL       ( EVT_SYNC_A | EVT_SYNC_B | EVT_SYNC_C )

typedef struct
{
    const char *pcName;
    EventBits_t uxBit;
    TickType_t xDelay;
    TickType_t xPostSyncLogDelay;
} SyncTaskConfig_t;

static EventGroupHandle_t xWaitEvents = NULL;
static EventGroupHandle_t xSyncEvents = NULL;

static TaskHandle_t xWaitAnyHandle = NULL;
static TaskHandle_t xWaitAllHandle = NULL;
static TaskHandle_t xSetterHandle = NULL;
static TaskHandle_t xSyncTaskAHandle = NULL;
static TaskHandle_t xSyncTaskBHandle = NULL;
static TaskHandle_t xSyncTaskCHandle = NULL;

static volatile uint32_t ulWaitStageDone = 0;
static volatile uint32_t ulSyncStageDone = 0;

static const SyncTaskConfig_t xSyncTaskAConfig = { "SyncA", EVT_SYNC_A, pdMS_TO_TICKS(150), 0 };
static const SyncTaskConfig_t xSyncTaskBConfig = { "SyncB", EVT_SYNC_B, pdMS_TO_TICKS(300), pdMS_TO_TICKS(5) };
static const SyncTaskConfig_t xSyncTaskCConfig = { "SyncC", EVT_SYNC_C, pdMS_TO_TICKS(450), pdMS_TO_TICKS(10) };

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
 * Stage A: WaitBits demo
 *-----------------------------------------------------------*/
static void vWaitAnyTask(void *pvParameters)
{
    TickType_t xStartTick;
    EventBits_t uxBits;

    (void)pvParameters;

    xStartTick = xTaskGetTickCount();
    printf("[WaitAny] wait ANY mask=0x%02lx at tick=%lu\n",
           (unsigned long)EVT_WAIT_ANY_MASK,
           (unsigned long)xStartTick);

    uxBits = xEventGroupWaitBits(xWaitEvents,
                                 EVT_WAIT_ANY_MASK,
                                 pdFALSE,
                                 pdFALSE,
                                 portMAX_DELAY);

    printf("[WaitAny] resumed at tick=%lu, returned bits=0x%02lx, current bits=0x%02lx\n",
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)uxBits,
           (unsigned long)xEventGroupGetBits(xWaitEvents));
    ulWaitStageDone++;
    vTaskDelete(NULL);
}

static void vWaitAllTask(void *pvParameters)
{
    TickType_t xStartTick;
    EventBits_t uxBits;

    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(5));

    xStartTick = xTaskGetTickCount();
    printf("[WaitAll] wait ALL mask=0x%02lx with clear-on-exit at tick=%lu\n",
           (unsigned long)EVT_WAIT_ALL_MASK,
           (unsigned long)xStartTick);

    uxBits = xEventGroupWaitBits(xWaitEvents,
                                 EVT_WAIT_ALL_MASK,
                                 pdTRUE,
                                 pdTRUE,
                                 portMAX_DELAY);

    printf("[WaitAll] resumed at tick=%lu, returned bits=0x%02lx, bits after clear=0x%02lx\n",
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)uxBits,
           (unsigned long)xEventGroupGetBits(xWaitEvents));
    ulWaitStageDone++;
    vTaskDelete(NULL);
}

static void vSetterTask(void *pvParameters)
{
    EventBits_t uxAfterSet;

    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(200));
    uxAfterSet = xEventGroupSetBits(xWaitEvents, EVT_NET_READY);
    printf("[Setter ] set NET at tick=%lu -> bits now=0x%02lx\n",
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)uxAfterSet);

    vTaskDelay(pdMS_TO_TICKS(220));
    uxAfterSet = xEventGroupSetBits(xWaitEvents, EVT_STORAGE_READY);
    printf("[Setter ] set STORAGE at tick=%lu -> bits now=0x%02lx\n",
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)uxAfterSet);

    vTaskDelay(pdMS_TO_TICKS(220));
    uxAfterSet = xEventGroupSetBits(xWaitEvents, EVT_SENSOR_READY);
    printf("[Setter ] set SENSOR at tick=%lu -> bits now=0x%02lx\n",
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)uxAfterSet);

    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * Stage B: xEventGroupSync demo
 *-----------------------------------------------------------*/
static void vSyncTask(void *pvParameters)
{
    const SyncTaskConfig_t *pxConfig = (const SyncTaskConfig_t *)pvParameters;
    EventBits_t uxReturnedBits;

    vTaskDelay(pxConfig->xDelay);
    printf("[%s] reach barrier at tick=%lu, set bit=0x%02lx, bits before=0x%02lx\n",
           pxConfig->pcName,
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)pxConfig->uxBit,
           (unsigned long)xEventGroupGetBits(xSyncEvents));

    uxReturnedBits = xEventGroupSync(xSyncEvents,
                                     pxConfig->uxBit,
                                     EVT_SYNC_ALL,
                                     pdMS_TO_TICKS(1500));

    if (pxConfig->xPostSyncLogDelay > 0)
    {
        vTaskDelay(pxConfig->xPostSyncLogDelay);
    }

    printf("[%s] passed barrier at tick=%lu, returned bits=0x%02lx, current bits=0x%02lx\n",
           pxConfig->pcName,
           (unsigned long)xTaskGetTickCount(),
           (unsigned long)uxReturnedBits,
           (unsigned long)xEventGroupGetBits(xSyncEvents));

    ulSyncStageDone++;
    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * Coordinator
 *-----------------------------------------------------------*/
static void prvRunWaitBitsStage(void)
{
    printf("\n--- Stage A: WaitBits (ANY / ALL / clear-on-exit) ---\n");
    printf("[Coord  ] WaitAny should wake on NET, WaitAll should wake only after STORAGE+SENSOR and then clear those bits.\n\n");

    ulWaitStageDone = 0;
    xTaskCreate(vWaitAnyTask, "WaitAny", 256, NULL, 2, &xWaitAnyHandle);
    xTaskCreate(vWaitAllTask, "WaitAll", 256, NULL, 2, &xWaitAllHandle);
    xTaskCreate(vSetterTask,  "Setter",  256, NULL, 1, &xSetterHandle);

    while (ulWaitStageDone < 2U)
    {
        printf("[Coord/W ] tick=%lu bits=0x%02lx any=%s all=%s setter=%s\n",
               (unsigned long)xTaskGetTickCount(),
               (unsigned long)xEventGroupGetBits(xWaitEvents),
               prvHandleStateToString(xWaitAnyHandle),
               prvHandleStateToString(xWaitAllHandle),
               prvHandleStateToString(xSetterHandle));
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("[Coord/W ] final wait-event bits=0x%02lx\n",
           (unsigned long)xEventGroupGetBits(xWaitEvents));
    vTaskDelay(pdMS_TO_TICKS(120));

    xWaitAnyHandle = NULL;
    xWaitAllHandle = NULL;
    xSetterHandle = NULL;
}

static void prvRunSyncStage(void)
{
    printf("\n--- Stage B: xEventGroupSync (Rendezvous) ---\n");
    printf("[Coord  ] Three tasks arrive at different times and pass the barrier together when all sync bits are set.\n\n");

    ulSyncStageDone = 0;
    xTaskCreate(vSyncTask, "SyncA", 256, (void *)&xSyncTaskAConfig, 2, &xSyncTaskAHandle);
    xTaskCreate(vSyncTask, "SyncB", 256, (void *)&xSyncTaskBConfig, 2, &xSyncTaskBHandle);
    xTaskCreate(vSyncTask, "SyncC", 256, (void *)&xSyncTaskCConfig, 2, &xSyncTaskCHandle);

    while (ulSyncStageDone < 3U)
    {
        printf("[Coord/S ] tick=%lu bits=0x%02lx A=%s B=%s C=%s\n",
               (unsigned long)xTaskGetTickCount(),
               (unsigned long)xEventGroupGetBits(xSyncEvents),
               prvHandleStateToString(xSyncTaskAHandle),
               prvHandleStateToString(xSyncTaskBHandle),
               prvHandleStateToString(xSyncTaskCHandle));
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("[Coord/S ] final sync bits=0x%02lx\n",
           (unsigned long)xEventGroupGetBits(xSyncEvents));
    vTaskDelay(pdMS_TO_TICKS(120));

    xSyncTaskAHandle = NULL;
    xSyncTaskBHandle = NULL;
    xSyncTaskCHandle = NULL;
}

static void vCoordinatorTask(void *pvParameters)
{
    (void)pvParameters;

    prvRunWaitBitsStage();
    prvRunSyncStage();

    printf("\n[Coord  ] Demo complete. Idle task keeps the system alive until QEMU timeout.\n");
    vTaskSuspend(NULL);
}

int main(void)
{
    uart_init();

    printf("====================================================\n");
    printf("  FreeRTOS Lesson 06: Event Group Internals\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("====================================================\n\n");

    xWaitEvents = xEventGroupCreate();
    xSyncEvents = xEventGroupCreate();

    if ((xWaitEvents == NULL) || (xSyncEvents == NULL))
    {
        printf("ERROR: Failed to create event groups!\n");
        for (;;)
        {
        }
    }

    xTaskCreate(vCoordinatorTask, "Coord", 512, NULL, 3, NULL);

    vTaskStartScheduler();
    for (;;)
    {
    }

    return 0;
}
