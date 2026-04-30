/*
 * FreeRTOS 教程 - 第1课: 任务基础
 * 
 * 知识点:
 * 1. 什么是 RTOS 任务 (Task)
 * 2. 如何创建任务 (xTaskCreate)
 * 3. 启动调度器 (vTaskStartScheduler)
 * 4. 任务延时 (vTaskDelay)
 * 5. 任务状态: Running, Ready, Blocked, Suspended
 *
 * 本示例创建两个任务:
 * - Task1: 每500ms打印一次
 * - Task2: 每1000ms打印一次
 * 
 * 预期输出:
 *   [Task1] Hello from Task1! Count: 1
 *   [Task1] Hello from Task1! Count: 2
 *   [Task2] Hello from Task2! Count: 1
 *   [Task1] Hello from Task1! Count: 3
 *   [Task1] Hello from Task1! Count: 4
 *   [Task2] Hello from Task2! Count: 2
 *   ...
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

/* 外部函数: UART初始化 */
extern void uart_init(void);

/*-----------------------------------------------------------
 * 任务1: 每500ms执行一次
 *-----------------------------------------------------------*/
void vTask1(void *pvParameters)
{
    (void)pvParameters;  /* 未使用参数 */
    uint32_t count = 0;

    for (;;)  /* 任务必须是无限循环 */
    {
        count++;
        printf("[Task1] Hello from Task1! Count: %lu\n", (unsigned long)count);

        /* vTaskDelay: 将任务置为 Blocked 状态
         * pdMS_TO_TICKS(500) 将毫秒转换为 Tick 数
         * 在此期间，CPU 可以运行其他任务 */
        vTaskDelay(pdMS_TO_TICKS(500));

        /* 运行一段时间后退出演示 */
        if (count >= 8) {
            printf("[Task1] Demo complete. Halting.\n");
            vTaskEndScheduler();
            for(;;);
        }
    }
}

/*-----------------------------------------------------------
 * 任务2: 每1000ms执行一次
 *-----------------------------------------------------------*/
void vTask2(void *pvParameters)
{
    (void)pvParameters;
    uint32_t count = 0;

    for (;;)
    {
        count++;
        printf("[Task2] Hello from Task2! Count: %lu\n", (unsigned long)count);

        /* 延时1000ms */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*-----------------------------------------------------------
 * main 函数
 *-----------------------------------------------------------*/
int main(void)
{
    /* 初始化 UART (用于 printf 输出) */
    uart_init();

    printf("========================================\n");
    printf("  FreeRTOS Lesson 01: Task Basics\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("========================================\n\n");

    /*
     * xTaskCreate() 参数说明:
     * 1. pvTaskCode:    任务函数指针
     * 2. pcName:        任务名称 (调试用)
     * 3. usStackDepth:  任务栈大小 (单位: 字/word)
     * 4. pvParameters:  传递给任务的参数
     * 5. uxPriority:    任务优先级 (数字越大优先级越高)
     * 6. pxCreatedTask: 任务句柄 (可为NULL)
     *
     * 返回值: pdPASS=成功, errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY=失败
     */

    BaseType_t ret;

    ret = xTaskCreate(vTask1,       /* 任务函数 */
                      "Task1",      /* 任务名称 */
                      256,          /* 栈大小 (256个字 = 1024字节) */
                      NULL,         /* 参数 */
                      2,            /* 优先级: 2 */
                      NULL);        /* 不需要任务句柄 */
    if (ret != pdPASS) {
        printf("ERROR: Failed to create Task1!\n");
    }

    ret = xTaskCreate(vTask2,       /* 任务函数 */
                      "Task2",      /* 任务名称 */
                      256,          /* 栈大小 */
                      NULL,         /* 参数 */
                      1,            /* 优先级: 1 (低于Task1) */
                      NULL);        /* 不需要任务句柄 */
    if (ret != pdPASS) {
        printf("ERROR: Failed to create Task2!\n");
    }

    printf("Tasks created. Starting scheduler...\n\n");

    /*
     * vTaskStartScheduler():
     * - 启动 FreeRTOS 调度器
     * - 创建空闲任务 (Idle Task)
     * - 开始任务调度
     * - 此函数正常情况下不会返回!
     * - 如果返回，说明内存不足
     */
    vTaskStartScheduler();

    /* 不应到达这里 */
    printf("ERROR: Scheduler returned! Insufficient memory?\n");
    for (;;);

    return 0;
}
