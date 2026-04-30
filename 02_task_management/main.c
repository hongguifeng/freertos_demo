/*
 * FreeRTOS 教程 - 第2课: 任务管理
 * 
 * 知识点:
 * 1. 任务优先级 (Priority) - 数字越大优先级越高
 * 2. 动态修改优先级 (vTaskPrioritySet)
 * 3. 任务挂起与恢复 (vTaskSuspend / vTaskResume)
 * 4. 任务删除 (vTaskDelete)
 * 5. 任务状态查询 (eTaskGetState)
 *
 * 演示流程由 Controller 任务统一协调
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

extern void uart_init(void);

static TaskHandle_t xTaskAHandle = NULL;
static TaskHandle_t xTaskBHandle = NULL;

/*-----------------------------------------------------------
 * 工作任务A
 *-----------------------------------------------------------*/
void vTaskA(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        printf("[TaskA ] Running! Priority: %lu\n",
               (unsigned long)uxTaskPriorityGet(NULL));
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/*-----------------------------------------------------------
 * 工作任务B
 *-----------------------------------------------------------*/
void vTaskB(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        printf("[TaskB ] Running! Priority: %lu\n",
               (unsigned long)uxTaskPriorityGet(NULL));
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/*-----------------------------------------------------------
 * 控制任务 - 演示各种任务管理操作
 *-----------------------------------------------------------*/
void vControllerTask(void *pvParameters)
{
    (void)pvParameters;
    eTaskState state;

    /* === 阶段1: 观察优先级调度 === */
    printf("\n--- Phase 1: Observe priority scheduling ---\n");
    printf("[Ctrl  ] TaskA(pri=2), TaskB(pri=1), Controller(pri=3)\n");
    printf("[Ctrl  ] Controller delays -> TaskA runs -> A delays -> B runs\n\n");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* === 阶段2: 挂起任务 === */
    printf("\n--- Phase 2: Suspend TaskA ---\n");
    vTaskSuspend(xTaskAHandle);
    state = eTaskGetState(xTaskAHandle);
    printf("[Ctrl  ] TaskA state: %s\n",
           state == eSuspended ? "Suspended" : "Unknown");
    printf("[Ctrl  ] Only TaskB should run now:\n");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* === 阶段3: 恢复任务 === */
    printf("\n--- Phase 3: Resume TaskA ---\n");
    vTaskResume(xTaskAHandle);
    printf("[Ctrl  ] TaskA resumed, both tasks run again:\n");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* === 阶段4: 动态改变优先级 === */
    printf("\n--- Phase 4: Change priorities ---\n");
    printf("[Ctrl  ] Raising TaskB priority from 1 to 3\n");
    vTaskPrioritySet(xTaskBHandle, 3);
    printf("[Ctrl  ] Now TaskB(pri=3) >= TaskA(pri=2), B runs first:\n");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* === 阶段5: 删除任务 === */
    printf("\n--- Phase 5: Delete TaskA ---\n");
    vTaskDelete(xTaskAHandle);
    printf("[Ctrl  ] TaskA deleted. Only TaskB remains:\n");
    vTaskDelay(pdMS_TO_TICKS(300));

    printf("\n[Ctrl  ] Demo complete!\n");
    vTaskEndScheduler();
    for(;;);
}

int main(void)
{
    uart_init();

    printf("========================================\n");
    printf("  FreeRTOS Lesson 02: Task Management\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("========================================\n");

    xTaskCreate(vControllerTask, "Ctrl",  256, NULL, 3, NULL);
    xTaskCreate(vTaskA,          "TaskA", 256, NULL, 2, &xTaskAHandle);
    xTaskCreate(vTaskB,          "TaskB", 256, NULL, 1, &xTaskBHandle);

    vTaskStartScheduler();
    for (;;);
    return 0;
}
