/*
 * FreeRTOS 教程 - 第3课: 队列通信 (Queue)
 *
 * 知识点:
 * 1. 队列的概念 - 任务间安全的数据传递机制 (FIFO)
 * 2. xQueueCreate() - 创建队列
 * 3. xQueueSend() / xQueueReceive() - 发送/接收数据
 * 4. xQueueSendToFront() / xQueueSendToBack() - 队首/队尾发送
 * 5. uxQueueMessagesWaiting() - 查询队列中的消息数
 * 6. 多生产者单消费者模型
 *
 * 演示:
 * - Producer1: 发送温度数据
 * - Producer2: 发送湿度数据
 * - Consumer: 接收并打印所有数据
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

extern void uart_init(void);

/* 定义消息结构体 */
typedef struct {
    char source[12];   /* 数据来源 */
    int32_t value;     /* 数据值 */
} SensorData_t;

/* 队列句柄 */
static QueueHandle_t xSensorQueue = NULL;
static volatile int demo_count = 0;

/*-----------------------------------------------------------
 * 温度传感器任务 (生产者1)
 *-----------------------------------------------------------*/
void vTempProducer(void *pvParameters)
{
    (void)pvParameters;
    SensorData_t data;
    int32_t temp = 25;
    BaseType_t ret;

    for (;;)
    {
        temp = 20 + (temp + 3) % 15;
        strcpy(data.source, "Temp");
        data.value = temp;

        /* xQueueSend: 发送数据到队列尾部
         * 参数: 队列句柄, 数据指针, 超时时间
         * pdMS_TO_TICKS(100): 如果队列满，等待最多100ms */
        ret = xQueueSend(xSensorQueue, &data, pdMS_TO_TICKS(100));
        if (ret != pdPASS) {
            printf("[Temp  ] Queue full! Data lost.\n");
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

/*-----------------------------------------------------------
 * 湿度传感器任务 (生产者2)
 *-----------------------------------------------------------*/
void vHumidProducer(void *pvParameters)
{
    (void)pvParameters;
    SensorData_t data;
    int32_t humid = 60;

    for (;;)
    {
        humid = 50 + (humid + 7) % 30;
        strcpy(data.source, "Humidity");
        data.value = humid;

        xQueueSendToBack(xSensorQueue, &data, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/*-----------------------------------------------------------
 * 数据处理任务 (消费者)
 *-----------------------------------------------------------*/
void vConsumer(void *pvParameters)
{
    (void)pvParameters;
    SensorData_t received;
    BaseType_t ret;

    for (;;)
    {
        /* xQueueReceive: 从队列头部接收数据
         * portMAX_DELAY: 无限等待直到有数据可用
         * 接收后数据从队列中移除 */
        ret = xQueueReceive(xSensorQueue, &received, portMAX_DELAY);

        if (ret == pdPASS) {
            printf("[Consumer] src=%-8s value=%ld  (queue remaining: %lu)\n",
                   received.source, (long)received.value,
                   (unsigned long)uxQueueMessagesWaiting(xSensorQueue));
            demo_count++;
        }

        if (demo_count >= 10) {
            printf("\n[Consumer] Received 10 messages. Demo complete!\n");
            vTaskEndScheduler();
        }
    }
}

int main(void)
{
    uart_init();

    printf("========================================\n");
    printf("  FreeRTOS Lesson 03: Queue\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("========================================\n\n");

    /* xQueueCreate: 创建队列
     * 参数1: 队列长度 (最多存放5条消息)
     * 参数2: 每条消息的大小 (字节) */
    xSensorQueue = xQueueCreate(5, sizeof(SensorData_t));
    if (xSensorQueue == NULL) {
        printf("ERROR: Failed to create queue!\n");
        for(;;);
    }

    printf("[Main  ] Queue created (capacity: 5, item size: %u bytes)\n\n",
           (unsigned int)sizeof(SensorData_t));

    xTaskCreate(vTempProducer,  "Temp",     256, NULL, 1, NULL);
    xTaskCreate(vHumidProducer, "Humid",    256, NULL, 1, NULL);
    xTaskCreate(vConsumer,      "Consumer", 256, NULL, 2, NULL);

    vTaskStartScheduler();
    for (;;);
    return 0;
}
