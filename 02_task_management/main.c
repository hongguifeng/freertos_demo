/*
 * FreeRTOS 教程 - 第2课: 任务管理 —— 内核调度原理演示
 *
 * 本示例深入展示 FreeRTOS 调度器的内部行为：
 *   阶段1: 时间片轮转 - 3个同优先级任务通过 listGET_OWNER_OF_NEXT_ENTRY 轮转
 *   阶段2: 优先级抢占 - 高优先级任务立即抢占低优先级
 *   阶段3: 挂起/恢复 - 观察任务在 Ready ↔ Suspended 列表间迁移
 *   阶段4: 动态优先级修改 - 观察任务在不同优先级链表间移动
 *   阶段5: 任务删除 - 展示 Idle 任务的 TCB 回收机制
 *
 * 原理说明见 README.md
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

extern void uart_init(void);

/* 串口互斥锁，避免多任务 printf 交错 */
static SemaphoreHandle_t xPrintMutex;

#define SAFE_PRINTF(...)  do { \
    xSemaphoreTake(xPrintMutex, portMAX_DELAY); \
    printf(__VA_ARGS__); \
    xSemaphoreGive(xPrintMutex); \
} while(0)

static TaskHandle_t xRR_A = NULL, xRR_B = NULL, xRR_C = NULL;
static TaskHandle_t xHighTask = NULL;
static TaskHandle_t xCtrlHandle = NULL;
static volatile uint32_t ulRR_CountA = 0, ulRR_CountB = 0, ulRR_CountC = 0;

/*-----------------------------------------------------------
 * 阶段1: 时间片轮转任务 (同优先级 = 1)
 * 每次被调度到就递增计数器，用于统计时间片分配
 *-----------------------------------------------------------*/
void vRoundRobinTask(void *pvParameters)
{
    const char *pcName = pcTaskGetName(NULL);
    volatile uint32_t *pCounter = (volatile uint32_t *)pvParameters;

    for (;;)
    {
        (*pCounter)++;
        /* 不调用 vTaskDelay - 持续占用 CPU，靠时间片切换 */
        /* 短暂循环以产生可观测的计数差异 */
        volatile int i;
        for (i = 0; i < 1000; i++) {}
    }
    (void)pcName;
}

/*-----------------------------------------------------------
 * 阶段2: 高优先级抢占任务
 *-----------------------------------------------------------*/
void vHighPriorityTask(void *pvParameters)
{
    (void)pvParameters;
    TickType_t xWakeTime = xTaskGetTickCount();

    SAFE_PRINTF("[High  ] Preempted! Running at tick=%lu, pri=%lu\n",
                (unsigned long)xWakeTime,
                (unsigned long)uxTaskPriorityGet(NULL));
    SAFE_PRINTF("[High  ] I preempted the round-robin tasks immediately.\n");

    /* 执行完毕，挂起自己 */
    vTaskSuspend(NULL);
    for (;;) {}
}

/*-----------------------------------------------------------
 * 控制任务 - 协调所有演示阶段 (最高优先级)
 *-----------------------------------------------------------*/
void vControllerTask(void *pvParameters)
{
    (void)pvParameters;
    eTaskState state;
    UBaseType_t uxPri;

    /* ============ 阶段1: 时间片轮转 ============ */
    SAFE_PRINTF("\n=== Phase 1: Time Slicing (Round-Robin) ===\n");
    SAFE_PRINTF("[Ctrl  ] Creating 3 tasks at SAME priority (1)\n");
    SAFE_PRINTF("[Ctrl  ] Kernel uses listGET_OWNER_OF_NEXT_ENTRY to rotate\n");
    SAFE_PRINTF("[Ctrl  ] each tick among tasks in pxReadyTasksLists[1]\n\n");

    xTaskCreate(vRoundRobinTask, "RR_A", 256, (void *)&ulRR_CountA, 1, &xRR_A);
    xTaskCreate(vRoundRobinTask, "RR_B", 256, (void *)&ulRR_CountB, 1, &xRR_B);
    xTaskCreate(vRoundRobinTask, "RR_C", 256, (void *)&ulRR_CountC, 1, &xRR_C);

    /* 让轮转运行 30ms */
    vTaskDelay(pdMS_TO_TICKS(30));

    /* 挂起轮转任务以读取计数 */
    vTaskSuspend(xRR_A);
    vTaskSuspend(xRR_B);
    vTaskSuspend(xRR_C);

    SAFE_PRINTF("[Ctrl  ] After 30 ticks of round-robin:\n");
    SAFE_PRINTF("[Ctrl  ]   RR_A count = %lu\n", (unsigned long)ulRR_CountA);
    SAFE_PRINTF("[Ctrl  ]   RR_B count = %lu\n", (unsigned long)ulRR_CountB);
    SAFE_PRINTF("[Ctrl  ]   RR_C count = %lu\n", (unsigned long)ulRR_CountC);
    SAFE_PRINTF("[Ctrl  ] Counts should be roughly equal (time slicing works!)\n");

    /* ============ 阶段2: 优先级抢占 ============ */
    SAFE_PRINTF("\n=== Phase 2: Priority Preemption ===\n");
    SAFE_PRINTF("[Ctrl  ] Resuming RR tasks, then creating High(pri=2)\n");
    SAFE_PRINTF("[Ctrl  ] High will preempt all pri=1 tasks immediately\n\n");

    ulRR_CountA = ulRR_CountB = ulRR_CountC = 0;
    vTaskResume(xRR_A);
    vTaskResume(xRR_B);
    vTaskResume(xRR_C);

    /* 创建优先级 2 的任务 - 它比 RR 任务 (pri=1) 高，将立即抢占 */
    xTaskCreate(vHighPriorityTask, "High", 256, NULL, 2, &xHighTask);

    /* Ctrl (pri=3) 继续运行，High (pri=2) 要等 Ctrl delay 才能运行 */
    SAFE_PRINTF("[Ctrl  ] Ctrl(pri=3) still runs. Delaying to let High run...\n");
    vTaskDelay(pdMS_TO_TICKS(5));

    /* High 已经运行并挂起自己了 */
    vTaskSuspend(xRR_A);
    vTaskSuspend(xRR_B);
    vTaskSuspend(xRR_C);
    SAFE_PRINTF("[Ctrl  ] RR counts during 5ms with High preempting:\n");
    SAFE_PRINTF("[Ctrl  ]   RR_A=%lu, RR_B=%lu, RR_C=%lu\n",
                (unsigned long)ulRR_CountA,
                (unsigned long)ulRR_CountB,
                (unsigned long)ulRR_CountC);
    SAFE_PRINTF("[Ctrl  ] (Lower than Phase 1 because High ran first)\n");

    /* ============ 阶段3: 挂起/恢复 - 观察就绪列表变化 ============ */
    SAFE_PRINTF("\n=== Phase 3: Suspend/Resume - Ready List Migration ===\n");

    vTaskResume(xRR_A);
    vTaskResume(xRR_B);
    /* RR_C stays suspended */

    state = eTaskGetState(xRR_A);
    SAFE_PRINTF("[Ctrl  ] RR_A state: %s (in pxReadyTasksLists[1])\n",
                state == eReady ? "Ready" : "?");
    state = eTaskGetState(xRR_C);
    SAFE_PRINTF("[Ctrl  ] RR_C state: %s (in xSuspendedTaskList)\n",
                state == eSuspended ? "Suspended" : "?");

    SAFE_PRINTF("[Ctrl  ] Suspending RR_A (removes from Ready list)...\n");
    vTaskSuspend(xRR_A);
    state = eTaskGetState(xRR_A);
    SAFE_PRINTF("[Ctrl  ] RR_A state: %s\n",
                state == eSuspended ? "Suspended" : "?");

    SAFE_PRINTF("[Ctrl  ] Resuming RR_C (adds back to Ready list)...\n");
    vTaskResume(xRR_C);
    state = eTaskGetState(xRR_C);
    SAFE_PRINTF("[Ctrl  ] RR_C state: %s\n",
                state == eReady ? "Ready" : "?");

    vTaskDelay(pdMS_TO_TICKS(10));
    vTaskSuspend(xRR_B);
    vTaskSuspend(xRR_C);

    /* ============ 阶段4: 动态优先级修改 ============ */
    SAFE_PRINTF("\n=== Phase 4: Dynamic Priority Change ===\n");
    SAFE_PRINTF("[Ctrl  ] RR_B current priority: %lu\n",
                (unsigned long)uxTaskPriorityGet(xRR_B));

    vTaskResume(xRR_A);
    vTaskResume(xRR_B);
    ulRR_CountA = ulRR_CountB = 0;

    SAFE_PRINTF("[Ctrl  ] Raising RR_B priority: 1 -> 2\n");
    SAFE_PRINTF("[Ctrl  ] RR_B moves from pxReadyTasksLists[1] to [2]\n");
    SAFE_PRINTF("[Ctrl  ] RR_B(pri=2) will preempt RR_A(pri=1)\n\n");
    vTaskPrioritySet(xRR_B, 2);

    uxPri = uxTaskPriorityGet(xRR_B);
    SAFE_PRINTF("[Ctrl  ] RR_B new priority: %lu\n", (unsigned long)uxPri);

    vTaskDelay(pdMS_TO_TICKS(20));
    vTaskSuspend(xRR_A);
    vTaskSuspend(xRR_B);

    SAFE_PRINTF("[Ctrl  ] After 20ms: RR_A(pri=1)=%lu, RR_B(pri=2)=%lu\n",
                (unsigned long)ulRR_CountA, (unsigned long)ulRR_CountB);
    SAFE_PRINTF("[Ctrl  ] RR_B got all CPU time (higher priority)!\n");

    /* 恢复优先级 */
    vTaskPrioritySet(xRR_B, 1);

    /* ============ 阶段5: 任务删除 & Idle 回收 ============ */
    SAFE_PRINTF("\n=== Phase 5: Task Deletion & Idle Cleanup ===\n");
    SAFE_PRINTF("[Ctrl  ] Deleting RR_A, RR_B, RR_C, High...\n");
    SAFE_PRINTF("[Ctrl  ] vTaskDelete() moves TCB to xTasksWaitingTermination\n");
    SAFE_PRINTF("[Ctrl  ] Idle task calls prvCheckTasksWaitingTermination()\n");
    SAFE_PRINTF("[Ctrl  ] to actually free TCB + stack memory via prvDeleteTCB()\n\n");

    /* 删除所有演示任务 */
    vTaskDelete(xRR_A);
    vTaskDelete(xRR_B);
    vTaskResume(xRR_C);  /* 必须先恢复才能删除（或直接删除suspended也可以） */
    vTaskDelete(xRR_C);
    vTaskDelete(xHighTask);

    SAFE_PRINTF("[Ctrl  ] All demo tasks deleted.\n");
    SAFE_PRINTF("[Ctrl  ] Idle will reclaim memory on next idle cycle.\n");

    /* 让 Idle 运行一下以触发回收 */
    vTaskDelay(pdMS_TO_TICKS(5));

    SAFE_PRINTF("[Ctrl  ] Free heap after cleanup: %u bytes\n",
                (unsigned)xPortGetFreeHeapSize());

    SAFE_PRINTF("\n[Ctrl  ] === All phases complete! ===\n");

    /* 挂起自己，让 QEMU timeout 结束 */
    vTaskSuspend(NULL);
    for (;;) {}
}

int main(void)
{
    uart_init();

    printf("========================================\n");
    printf("  FreeRTOS Lesson 02: Task Management\n");
    printf("  Scheduling Internals Deep Dive\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("========================================\n");

    xPrintMutex = xSemaphoreCreateMutex();

    xTaskCreate(vControllerTask, "Ctrl", 512, NULL, 3, &xCtrlHandle);

    vTaskStartScheduler();
    for (;;);
    return 0;
}
