/*
 * FreeRTOS 教程 - 第3课: 队列阻塞与事件链表
 *
 * 本示例刻意制造两种等待：
 * 1. Consumer 在空队列上执行 xQueueReceive(portMAX_DELAY)
 * 2. Producer 在满队列上执行 xQueueSend(timeout)
 *
 * 观察重点：
 * - 等数据的任务会进入 xTasksWaitingToReceive
 * - 等空间的任务会进入 xTasksWaitingToSend
 * - Monitor 任务定期采样，帮助把运行输出和 Blocked/Ready 状态对上
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

extern void uart_init(void);

typedef struct
{
    uint32_t sequence;
    TickType_t producedAt;
} QueueMessage_t;

static QueueHandle_t xDemoQueue = NULL;
static TaskHandle_t xProducerHandle = NULL;
static TaskHandle_t xConsumerHandle = NULL;

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
 * 生产者: 启动稍晚，先唤醒消费者，随后持续把队列填满
 *-----------------------------------------------------------*/
static void vProducerTask(void *pvParameters)
{
    const TickType_t xStartupDelay = pdMS_TO_TICKS(350);
    const TickType_t xSendTimeout = pdMS_TO_TICKS(500);
    const TickType_t xProductionPeriod = pdMS_TO_TICKS(150);
    QueueMessage_t message;
    uint32_t sequence = 0;

    (void)pvParameters;

    vTaskDelay(xStartupDelay);

    for (;;)
    {
        BaseType_t ret;
        TickType_t xStartTick = xTaskGetTickCount();
        TickType_t xEndTick;

        sequence++;
        message.sequence = sequence;
        message.producedAt = xStartTick;

        printf("[Producer] seq=%lu attempt send at tick=%lu (depth before=%lu)\n",
               (unsigned long)message.sequence,
               (unsigned long)xStartTick,
               (unsigned long)uxQueueMessagesWaiting(xDemoQueue));

        ret = xQueueSend(xDemoQueue, &message, xSendTimeout);
        xEndTick = xTaskGetTickCount();

        if (ret == pdPASS)
        {
            printf("[Producer] seq=%lu send done at tick=%lu, waited=%lu tick(s), depth now=%lu\n",
                   (unsigned long)message.sequence,
                   (unsigned long)xEndTick,
                   (unsigned long)(xEndTick - xStartTick),
                   (unsigned long)uxQueueMessagesWaiting(xDemoQueue));
        }
        else
        {
            printf("[Producer] seq=%lu send timeout at tick=%lu after waiting %lu tick(s)\n",
                   (unsigned long)message.sequence,
                   (unsigned long)xEndTick,
                   (unsigned long)(xEndTick - xStartTick));
        }

        vTaskDelay(xProductionPeriod);
    }
}

/*-----------------------------------------------------------
 * 消费者: 启动即等空队列，收到后故意慢处理，逼出发送侧阻塞
 *-----------------------------------------------------------*/
static void vConsumerTask(void *pvParameters)
{
    const TickType_t xProcessingDelay = pdMS_TO_TICKS(700);
    QueueMessage_t received;

    (void)pvParameters;

    for (;;)
    {
        BaseType_t ret;
        TickType_t xStartTick = xTaskGetTickCount();
        TickType_t xEndTick;

        printf("[Consumer] wait receive at tick=%lu (depth=%lu)\n",
               (unsigned long)xStartTick,
               (unsigned long)uxQueueMessagesWaiting(xDemoQueue));

        ret = xQueueReceive(xDemoQueue, &received, portMAX_DELAY);
        xEndTick = xTaskGetTickCount();

        if (ret == pdPASS)
        {
            printf("[Consumer] got seq=%lu at tick=%lu, waited=%lu tick(s), queued-for=%lu tick(s), depth now=%lu\n",
                   (unsigned long)received.sequence,
                   (unsigned long)xEndTick,
                   (unsigned long)(xEndTick - xStartTick),
                   (unsigned long)(xEndTick - received.producedAt),
                   (unsigned long)uxQueueMessagesWaiting(xDemoQueue));
            printf("[Consumer] process seq=%lu for 700ms -> queue may fill and block the sender\n",
                   (unsigned long)received.sequence);
            vTaskDelay(xProcessingDelay);
        }
    }
}

/*-----------------------------------------------------------
 * 监控任务: 周期采样队列深度和任务状态
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

        sample++;
        printf("[Monitor ] sample=%lu tick=%lu depth=%lu producer=%s consumer=%s\n",
               (unsigned long)sample,
               (unsigned long)xNow,
               (unsigned long)uxQueueMessagesWaiting(xDemoQueue),
               prvTaskStateToString(eTaskGetState(xProducerHandle)),
               prvTaskStateToString(eTaskGetState(xConsumerHandle)));

        if (sample == 24U)
        {
            printf("[Monitor ] Stop demo: suspend producer and consumer to compare with explicit suspend.\n");
            vTaskSuspend(xProducerHandle);
            vTaskSuspend(xConsumerHandle);
            printf("[Monitor ] after suspend: producer=%s consumer=%s\n",
                   prvTaskStateToString(eTaskGetState(xProducerHandle)),
                   prvTaskStateToString(eTaskGetState(xConsumerHandle)));
            printf("[Monitor ] Demo complete. Idle task keeps running until QEMU timeout.\n");
            vTaskSuspend(NULL);
        }

        vTaskDelayUntil(&xLastWakeTime, xSamplePeriod);
    }
}

int main(void)
{
    uart_init();

    printf("====================================================\n");
    printf("  FreeRTOS Lesson 03: Queue Wait Internals\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("====================================================\n\n");
    printf("Consumer(pri=2) blocks first on an empty queue.\n");
    printf("Producer(pri=1) starts later and eventually blocks on a full queue.\n");
    printf("Monitor(pri=3) samples task states every 100ms.\n\n");

    xDemoQueue = xQueueCreate(2, sizeof(QueueMessage_t));
    if (xDemoQueue == NULL)
    {
        printf("ERROR: Failed to create demo queue!\n");
        for (;;)
        {
        }
    }

    printf("[Main   ] Queue created (capacity: 2, item size: %u bytes)\n\n",
           (unsigned int)sizeof(QueueMessage_t));

    xTaskCreate(vMonitorTask, "Monitor",  256, NULL, 3, NULL);
    xTaskCreate(vConsumerTask, "Consumer", 256, NULL, 2, &xConsumerHandle);
    xTaskCreate(vProducerTask, "Producer", 256, NULL, 1, &xProducerHandle);

    vTaskStartScheduler();

    printf("ERROR: Scheduler returned!\n");
    for (;;)
    {
    }

    return 0;
}
