/*
 * FreeRTOS 教程 - 第5课: 软件定时器
 *
 * 知识点:
 * 1. 软件定时器概念 - 在定时器服务任务(daemon)中执行回调
 * 2. xTimerCreate() - 创建定时器
 * 3. 单次定时器 (One-shot) vs 周期定时器 (Auto-reload)
 * 4. xTimerStart() / xTimerStop() / xTimerReset()
 * 5. xTimerChangePeriod() - 运行时修改定时器周期
 * 6. pvTimerGetTimerID() - 定时器ID用于回调中识别定时器
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

extern void uart_init(void);

static volatile int periodic_count = 0;
static volatile int oneshot_fired = 0;

/*-----------------------------------------------------------
 * 周期定时器回调 - 每200ms触发一次
 * 注意: 回调在定时器服务任务的上下文中执行
 *       不能调用会阻塞的API (如 vTaskDelay)
 *-----------------------------------------------------------*/
void vPeriodicTimerCallback(TimerHandle_t xTimer)
{
    periodic_count++;
    printf("[Periodic ] Timer fired! Count: %d (ID: %lu)\n",
           periodic_count,
           (unsigned long)pvTimerGetTimerID(xTimer));
}

/*-----------------------------------------------------------
 * 单次定时器回调 - 只触发一次
 *-----------------------------------------------------------*/
void vOneShotTimerCallback(TimerHandle_t xTimer)
{
    (void)xTimer;
    oneshot_fired = 1;
    printf("[OneShot  ] FIRED! (only once)\n");
}

/*-----------------------------------------------------------
 * 控制任务
 *-----------------------------------------------------------*/
void vTimerDemo(void *pvParameters)
{
    (void)pvParameters;
    TimerHandle_t xPeriodicTimer, xOneShotTimer;

    /* 创建周期定时器
     * 参数: 名称, 周期(ticks), 自动重载(pdTRUE=周期), ID, 回调 */
    xPeriodicTimer = xTimerCreate("Periodic",
                                   pdMS_TO_TICKS(200),
                                   pdTRUE,         /* Auto-reload = 周期定时器 */
                                   (void *)1,      /* Timer ID */
                                   vPeriodicTimerCallback);

    /* 创建单次定时器 */
    xOneShotTimer = xTimerCreate("OneShot",
                                  pdMS_TO_TICKS(500),
                                  pdFALSE,         /* One-shot = 只触发一次 */
                                  (void *)2,
                                  vOneShotTimerCallback);

    if (xPeriodicTimer == NULL || xOneShotTimer == NULL) {
        printf("ERROR: Timer creation failed!\n");
        vTaskDelete(NULL);
    }

    /* 启动两个定时器 */
    printf("[Demo     ] Starting periodic timer (200ms) and one-shot timer (500ms)\n\n");
    xTimerStart(xPeriodicTimer, 0);
    xTimerStart(xOneShotTimer, 0);

    /* 等待一段时间观察 */
    vTaskDelay(pdMS_TO_TICKS(1200));

    /* 修改周期定时器的周期 */
    printf("\n[Demo     ] Changing periodic timer to 400ms period\n\n");
    xTimerChangePeriod(xPeriodicTimer, pdMS_TO_TICKS(400), 0);

    vTaskDelay(pdMS_TO_TICKS(1000));

    /* 停止定时器 */
    printf("\n[Demo     ] Stopping periodic timer\n");
    xTimerStop(xPeriodicTimer, 0);

    /* 重启单次定时器 (可以重新触发) */
    printf("[Demo     ] Restarting one-shot timer\n\n");
    xTimerStart(xOneShotTimer, 0);

    vTaskDelay(pdMS_TO_TICKS(600));

    printf("\n[Demo     ] Total periodic fires: %d, OneShot fires: %d\n",
           periodic_count, oneshot_fired);
    printf("[Demo     ] Demo complete!\n");
    vTaskEndScheduler();
    for(;;);
}

int main(void)
{
    uart_init();

    printf("========================================\n");
    printf("  FreeRTOS Lesson 05: Software Timers\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("========================================\n\n");

    xTaskCreate(vTimerDemo, "TimerDemo", 512, NULL, 2, NULL);

    vTaskStartScheduler();
    for (;;);
    return 0;
}
