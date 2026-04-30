/*
 * FreeRTOS 教程 - 第6课: 事件组 (Event Groups)
 *
 * 知识点:
 * 1. 事件组概念 - 多个事件标志的集合
 * 2. xEventGroupCreate() - 创建事件组
 * 3. xEventGroupSetBits() - 设置事件位
 * 4. xEventGroupWaitBits() - 等待事件位
 * 5. xEventGroupSync() - 多任务同步 (集合点/Rendezvous)
 * 6. 等待所有位 vs 等待任一位
 *
 * 演示: 模拟系统启动流程
 * - 三个初始化任务各自完成后设置对应事件位
 * - 主任务等待所有初始化完成后继续
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

extern void uart_init(void);

/* 事件位定义 */
#define EVT_WIFI_READY    (1 << 0)  /* Bit 0: WiFi就绪 */
#define EVT_SENSOR_READY  (1 << 1)  /* Bit 1: 传感器就绪 */
#define EVT_STORAGE_READY (1 << 2)  /* Bit 2: 存储就绪 */
#define EVT_ALL_READY     (EVT_WIFI_READY | EVT_SENSOR_READY | EVT_STORAGE_READY)

static EventGroupHandle_t xStartupEvents = NULL;

/*-----------------------------------------------------------
 * WiFi 初始化任务
 *-----------------------------------------------------------*/
void vWifiInit(void *pvParameters)
{
    (void)pvParameters;
    printf("[WiFi   ] Initializing... (takes 300ms)\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    printf("[WiFi   ] Ready!\n");

    /* 设置 WiFi 就绪位 */
    xEventGroupSetBits(xStartupEvents, EVT_WIFI_READY);
    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * 传感器初始化任务
 *-----------------------------------------------------------*/
void vSensorInit(void *pvParameters)
{
    (void)pvParameters;
    printf("[Sensor ] Initializing... (takes 500ms)\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("[Sensor ] Ready!\n");

    xEventGroupSetBits(xStartupEvents, EVT_SENSOR_READY);
    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * 存储初始化任务
 *-----------------------------------------------------------*/
void vStorageInit(void *pvParameters)
{
    (void)pvParameters;
    printf("[Storage] Initializing... (takes 200ms)\n");
    vTaskDelay(pdMS_TO_TICKS(200));
    printf("[Storage] Ready!\n");

    xEventGroupSetBits(xStartupEvents, EVT_STORAGE_READY);
    vTaskDelete(NULL);
}

/*-----------------------------------------------------------
 * 主应用任务 - 等待所有子系统就绪
 *-----------------------------------------------------------*/
void vAppTask(void *pvParameters)
{
    (void)pvParameters;
    EventBits_t bits;

    printf("[App    ] Waiting for all subsystems...\n\n");

    /* xEventGroupWaitBits:
     * 参数1: 事件组
     * 参数2: 要等待的位 (EVT_ALL_READY = bit0|bit1|bit2)
     * 参数3: pdTRUE = 退出前清除这些位
     * 参数4: pdTRUE = 等待所有位都被设置 (AND)
     *         pdFALSE = 任一位被设置即返回 (OR)
     * 参数5: 超时时间 */
    bits = xEventGroupWaitBits(xStartupEvents,
                               EVT_ALL_READY,
                               pdTRUE,    /* 清除位 */
                               pdTRUE,    /* 等待所有位 (AND) */
                               pdMS_TO_TICKS(2000));

    printf("\n[App    ] WaitBits returned: 0x%02lx\n", (unsigned long)bits);

    if ((bits & EVT_ALL_READY) == EVT_ALL_READY) {
        printf("[App    ] All subsystems ready! System boot complete.\n");
    } else {
        printf("[App    ] Timeout! Not all subsystems ready.\n");
    }

    /* Part 2: 演示等待任一位 (OR) */
    printf("\n--- Part 2: Wait for ANY event (OR mode) ---\n");
    xEventGroupClearBits(xStartupEvents, 0xFF);

    /* 创建一个任务只设置一个位 */
    xTaskCreate(vWifiInit, "WiFi2", 256, NULL, 1, NULL);

    bits = xEventGroupWaitBits(xStartupEvents,
                               EVT_WIFI_READY | EVT_SENSOR_READY,
                               pdTRUE,
                               pdFALSE,   /* OR模式: 任一位即可 */
                               pdMS_TO_TICKS(1000));

    printf("[App    ] OR wait returned: 0x%02lx (only WiFi needed)\n",
           (unsigned long)bits);

    printf("\n[App    ] Demo complete!\n");
    vTaskEndScheduler();
    for(;;);
}

int main(void)
{
    uart_init();

    printf("========================================\n");
    printf("  FreeRTOS Lesson 06: Event Groups\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("========================================\n\n");

    xStartupEvents = xEventGroupCreate();
    if (xStartupEvents == NULL) {
        printf("ERROR: Failed to create event group!\n");
        for(;;);
    }

    /* 创建各初始化任务和主应用任务 */
    xTaskCreate(vAppTask,     "App",     512, NULL, 1, NULL);
    xTaskCreate(vWifiInit,    "WiFi",    256, NULL, 2, NULL);
    xTaskCreate(vSensorInit,  "Sensor",  256, NULL, 2, NULL);
    xTaskCreate(vStorageInit, "Storage", 256, NULL, 2, NULL);

    vTaskStartScheduler();
    for (;;);
    return 0;
}
