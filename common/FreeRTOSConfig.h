/*
 * FreeRTOS 配置文件
 * 
 * 目标平台: STM32F405 (QEMU netduinoplus2)
 * 
 * 这个文件定义了 FreeRTOS 内核的所有配置选项。
 * 每个宏的含义在注释中详细说明。
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * 基本配置
 *-----------------------------------------------------------*/

/* 调度器配置 */
#define configUSE_PREEMPTION                    1   /* 1=抢占式调度, 0=协作式调度 */
#define configUSE_TIME_SLICING                  1   /* 1=启用时间片轮转 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0   /* 使用通用任务选择算法 */

/* 时钟配置 */
#define configCPU_CLOCK_HZ                      ((uint32_t)16000000)  /* CPU频率 16MHz */
#define configTICK_RATE_HZ                      ((TickType_t)1000)    /* Tick频率 1000Hz = 1ms */

/* 任务配置 */
#define configMAX_PRIORITIES                    5    /* 最大优先级数 (0~4) */
#define configMINIMAL_STACK_SIZE                ((uint16_t)128) /* 最小栈大小 (字) */
#define configTOTAL_HEAP_SIZE                   ((size_t)(40 * 1024)) /* 堆大小 40KB */
#define configMAX_TASK_NAME_LEN                 16   /* 任务名最大长度 */

/* 功能开关 */
#define configUSE_16_BIT_TICKS                  0    /* 0=32位Tick计数器 */
#define configIDLE_SHOULD_YIELD                 1    /* 空闲任务让出CPU */
#define configUSE_MUTEXES                       1    /* 启用互斥锁 */
#define configUSE_RECURSIVE_MUTEXES             1    /* 启用递归互斥锁 */
#define configUSE_COUNTING_SEMAPHORES           1    /* 启用计数信号量 */
#define configUSE_QUEUE_SETS                    1    /* 启用队列集合 */
#define configUSE_TASK_NOTIFICATIONS            1    /* 启用任务通知 */

/*-----------------------------------------------------------
 * 内存管理配置
 *-----------------------------------------------------------*/
#define configSUPPORT_STATIC_ALLOCATION         0    /* 不使用静态分配 */
#define configSUPPORT_DYNAMIC_ALLOCATION        1    /* 使用动态分配 */

/*-----------------------------------------------------------
 * 钩子函数配置
 *-----------------------------------------------------------*/
#define configUSE_IDLE_HOOK                     0    /* 不使用空闲钩子 */
#define configUSE_TICK_HOOK                     0    /* 不使用Tick钩子 */
#define configUSE_MALLOC_FAILED_HOOK            0    /* 不使用malloc失败钩子 */
#define configCHECK_FOR_STACK_OVERFLOW          0    /* 不检查栈溢出 */

/*-----------------------------------------------------------
 * 软件定时器配置
 *-----------------------------------------------------------*/
#define configUSE_TIMERS                        1    /* 启用软件定时器 */
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1) /* 定时器任务优先级 */
#define configTIMER_QUEUE_LENGTH                10   /* 定时器命令队列长度 */
#define configTIMER_TASK_STACK_DEPTH            (configMINIMAL_STACK_SIZE * 2) /* 定时器任务栈 */

/*-----------------------------------------------------------
 * 运行时统计和协程
 *-----------------------------------------------------------*/
#define configUSE_CO_ROUTINES                   0    /* 不使用协程 */
#define configGENERATE_RUN_TIME_STATS           0    /* 不生成运行统计 */
#define configUSE_TRACE_FACILITY                1    /* 启用追踪功能 */
#define configUSE_STATS_FORMATTING_FUNCTIONS    1    /* 启用统计格式化 */

/*-----------------------------------------------------------
 * API 函数开关 (1=包含, 0=排除)
 *-----------------------------------------------------------*/
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_eTaskGetState                   1

/*-----------------------------------------------------------
 * Cortex-M4 特定配置
 *-----------------------------------------------------------*/

/* Cortex-M 中断优先级配置
 * STM32F4 使用 4 位优先级 (0-15)
 * FreeRTOS 需要知道最低优先级的中断号
 */
/* 
 * 中断优先级配置
 * MPS2-AN385 QEMU 平台使用 NVIC 优先级寄存器
 * configKERNEL_INTERRUPT_PRIORITY: 内核使用最低优先级 (255)
 * configMAX_SYSCALL_INTERRUPT_PRIORITY: 可调用 FreeRTOS API 的最高中断优先级
 */
#define configKERNEL_INTERRUPT_PRIORITY         255
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    191  /* 优先级5 shifted left, 高于此优先级的中断不受FreeRTOS管理 */

/*-----------------------------------------------------------
 * 中断处理函数映射
 * FreeRTOS 的 port.c 定义了: vPortSVCHandler, xPortPendSVHandler, xPortSysTickHandler
 * 我们在 startup.c 中直接使用这些名称
 *-----------------------------------------------------------*/
/* 不需要重映射，startup.c 直接引用 FreeRTOS 的中断处理函数 */

/*-----------------------------------------------------------
 * 断言配置 (调试用)
 *-----------------------------------------------------------*/
#define configASSERT(x) if((x) == 0) { taskDISABLE_INTERRUPTS(); for(;;); }

#endif /* FREERTOS_CONFIG_H */
