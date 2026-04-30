/*
 * FreeRTOS 教程 - 第4课: 信号量与互斥锁
 *
 * 知识点:
 * 1. 二值信号量 (Binary Semaphore) - 用于任务同步/事件通知
 * 2. 计数信号量 (Counting Semaphore) - 用于资源计数
 * 3. 互斥锁 (Mutex) - 用于保护共享资源
 * 4. 优先级继承 (Priority Inheritance) - 互斥锁自动处理
 * 5. 递归互斥锁 (Recursive Mutex)
 *
 * 演示:
 * Part A: 二值信号量 - ISR通知任务
 * Part B: 互斥锁 - 保护共享打印资源
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

extern void uart_init(void);

/* 信号量/互斥锁句柄 */
static SemaphoreHandle_t xBinarySem = NULL;
static SemaphoreHandle_t xMutex = NULL;
static volatile int demo_phase = 0;
static volatile int shared_counter = 0;

/*-----------------------------------------------------------
 * Part A: 二值信号量演示
 * 模拟 "中断触发 -> 任务处理" 的模式
 *-----------------------------------------------------------*/
void vEventGenerator(void *pvParameters)
{
    (void)pvParameters;
    int i;

    printf("\n--- Part A: Binary Semaphore (Sync) ---\n");
    printf("[Generator] Will 'give' semaphore 5 times\n\n");

    for (i = 0; i < 5; i++) {
        vTaskDelay(pdMS_TO_TICKS(300));
        printf("[Generator] Event %d! Giving semaphore...\n", i + 1);

        /* xSemaphoreGive: 释放信号量
         * 二值信号量: 从0变为1，通知等待的任务 */
        xSemaphoreGive(xBinarySem);
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    demo_phase = 1;
    /* Give one more time so Handler can wake up and see demo_phase==1 */
    xSemaphoreGive(xBinarySem);
    vTaskDelete(NULL);
}

void vEventHandler(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        /* xSemaphoreTake: 获取信号量
         * 二值信号量: 等待值变为1，获取后值变回0
         * portMAX_DELAY: 无限等待 */
        if (xSemaphoreTake(xBinarySem, portMAX_DELAY) == pdTRUE) {
            printf("[Handler ] Got semaphore! Processing event...\n");
        }

        if (demo_phase == 1) {
            break;
        }
    }

    printf("[Handler ] Part A complete.\n");

    /* 通知进入Part B */
    demo_phase = 2;
    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * Part B: 互斥锁演示
 * 保护共享资源 (shared_counter) 的并发访问
 *-----------------------------------------------------------*/
void vMutexWorker(void *pvParameters)
{
    const char *name = (const char *)pvParameters;
    int i;
    int local;

    for (i = 0; i < 5; i++) {
        /* xSemaphoreTake on mutex: 获取互斥锁
         * 如果已被其他任务持有，当前任务阻塞等待
         * 互斥锁有 "优先级继承": 如果高优先级任务等待，
         * 持有锁的低优先级任务临时提升到同等优先级 */
        xSemaphoreTake(xMutex, portMAX_DELAY);

        /* 临界区开始 - 安全访问共享资源 */
        local = shared_counter;
        local++;
        /* 模拟耗时操作 */
        vTaskDelay(pdMS_TO_TICKS(50));
        shared_counter = local;
        printf("[%-6s] counter = %d\n", name, shared_counter);

        /* 临界区结束 - 释放互斥锁 */
        xSemaphoreGive(xMutex);

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * 协调任务
 *-----------------------------------------------------------*/
void vCoordinator(void *pvParameters)
{
    (void)pvParameters;

    /* 等待Part A完成 */
    while (demo_phase < 2) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("\n--- Part B: Mutex (Shared Resource Protection) ---\n");
    printf("[Coord ] Two workers incrementing shared counter with mutex\n\n");

    shared_counter = 0;
    xTaskCreate(vMutexWorker, "Work1", 256, (void *)"Work1", 1, NULL);
    xTaskCreate(vMutexWorker, "Work2", 256, (void *)"Work2", 1, NULL);

    /* 等待workers完成 */
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("\n[Coord ] Final counter = %d (expected: 10)\n", shared_counter);
    printf("[Coord ] Demo complete!\n");
    vTaskEndScheduler();
    for(;;);
}

int main(void)
{
    uart_init();

    printf("========================================\n");
    printf("  FreeRTOS Lesson 04: Semaphore & Mutex\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("========================================\n");

    /* 创建二值信号量 (初始值为0，即"空") */
    xBinarySem = xSemaphoreCreateBinary();

    /* 创建互斥锁 (初始值为1，即"可用") */
    xMutex = xSemaphoreCreateMutex();

    if (xBinarySem == NULL || xMutex == NULL) {
        printf("ERROR: Failed to create semaphore/mutex!\n");
        for(;;);
    }

    xTaskCreate(vEventGenerator, "Gen",     256, NULL, 2, NULL);
    xTaskCreate(vEventHandler,   "Handler", 256, NULL, 1, NULL);
    xTaskCreate(vCoordinator,    "Coord",   256, NULL, 3, NULL);

    vTaskStartScheduler();
    for (;;);
    return 0;
}
