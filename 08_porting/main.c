/*
 * FreeRTOS 教程 - 第8课: 平台移植
 *
 * 知识点:
 * 1. FreeRTOS 移植层 (port.c / portmacro.h) 的职责
 * 2. 移植需要实现的关键函数:
 *    - pxPortInitialiseStack(): 初始化任务栈帧
 *    - xPortStartScheduler(): 启动调度器 (配置SysTick, PendSV, SVC)
 *    - vPortYield(): 触发上下文切换 (PendSV)
 *    - vPortSVCHandler / xPortPendSVHandler: 上下文切换汇编
 *    - xPortSysTickHandler: 系统时钟中断
 * 3. FreeRTOSConfig.h 中的关键配置
 * 4. 中断优先级与临界区
 * 5. 移植验证步骤
 *
 * 本课不运行新代码, 而是通过打印解释移植原理
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

extern void uart_init(void);

void vPortingGuide(void *pvParameters)
{
    (void)pvParameters;

    printf("==== FreeRTOS Porting Guide ====\n\n");

    printf("1. PORT LAYER STRUCTURE\n");
    printf("   FreeRTOS/portable/<COMPILER>/<ARCH>/\n");
    printf("     port.c       - Port-specific C code\n");
    printf("     portmacro.h  - Port-specific macros & types\n\n");
    printf("   Our platform: GCC/ARM_CM3/\n");
    printf("   portSTACK_TYPE = uint32_t\n");
    printf("   portBASE_TYPE  = long\n");
    printf("   portMAX_DELAY  = 0xFFFFFFFF\n\n");

    printf("2. KEY FUNCTIONS TO IMPLEMENT\n\n");

    printf("   pxPortInitialiseStack(pxTopOfStack, pxCode, pvParameters)\n");
    printf("   - Sets up initial stack frame as if task was interrupted\n");
    printf("   - ARM CM3 auto-saves: xPSR, PC, LR, R12, R3-R0\n");
    printf("   - Port manually saves: R4-R11\n");
    printf("   - xPSR bit 24 = 1 (Thumb mode)\n");
    printf("   - PC = task function address\n");
    printf("   - R0 = pvParameters\n\n");

    printf("   xPortStartScheduler()\n");
    printf("   - Set PendSV & SysTick to lowest priority\n");
    printf("   - Configure SysTick for configTICK_RATE_HZ\n");
    printf("   - Call vPortStartFirstTask() via SVC\n\n");

    printf("   PendSV Handler (context switch - assembly)\n");
    printf("   - Save R4-R11 to current task's stack\n");
    printf("   - Save new SP to current TCB\n");
    printf("   - Call vTaskSwitchContext() to select next task\n");
    printf("   - Restore new task's R4-R11 from its stack\n");
    printf("   - Return (hardware restores R0-R3,R12,LR,PC,xPSR)\n\n");

    printf("3. CRITICAL SECTION IMPLEMENTATION\n");
    printf("   portENTER_CRITICAL() - Raise BASEPRI to mask interrupts\n");
    printf("   portEXIT_CRITICAL()  - Restore previous BASEPRI\n");
    printf("   Only masks interrupts <= configMAX_SYSCALL_INTERRUPT_PRIORITY\n");
    printf("   Higher-priority interrupts still execute (but can't use FreeRTOS API)\n\n");

    printf("4. FreeRTOSConfig.h KEY SETTINGS\n");
    printf("   configCPU_CLOCK_HZ        = %u\n", (unsigned)configCPU_CLOCK_HZ);
    printf("   configTICK_RATE_HZ         = %u\n", (unsigned)configTICK_RATE_HZ);
    printf("   configMAX_PRIORITIES        = %u\n", (unsigned)configMAX_PRIORITIES);
    printf("   configMINIMAL_STACK_SIZE    = %u words\n", (unsigned)configMINIMAL_STACK_SIZE);
    printf("   configTOTAL_HEAP_SIZE       = %u bytes\n", (unsigned)configTOTAL_HEAP_SIZE);
    printf("   configKERNEL_INTERRUPT_PRIORITY     = %u (lowest)\n",
           (unsigned)configKERNEL_INTERRUPT_PRIORITY);
    printf("   configMAX_SYSCALL_INTERRUPT_PRIORITY = %u\n\n",
           (unsigned)configMAX_SYSCALL_INTERRUPT_PRIORITY);

    printf("5. PORTING CHECKLIST\n");
    printf("   [x] Startup code: vector table with correct handlers\n");
    printf("       - SVC_Handler -> vPortSVCHandler\n");
    printf("       - PendSV_Handler -> xPortPendSVHandler\n");
    printf("       - SysTick_Handler -> xPortSysTickHandler\n");
    printf("   [x] Linker script: define FLASH/RAM regions, stack/heap\n");
    printf("   [x] FreeRTOSConfig.h: match CPU clock, set priorities\n");
    printf("   [x] Select memory allocator (heap_1..heap_5)\n");
    printf("   [x] Implement _write() for printf (UART or semihosting)\n");
    printf("   [x] Test: create two tasks, verify context switching\n\n");

    printf("6. COMMON PITFALLS\n");
    printf("   - Wrong interrupt priority bits (configPRIO_BITS)\n");
    printf("   - Stack overflow (enable configCHECK_FOR_STACK_OVERFLOW)\n");
    printf("   - Missing SysTick/PendSV/SVC handler connections\n");
    printf("   - Using FreeRTOS API from high-priority ISR\n");
    printf("   - Insufficient heap for tasks + queues + timers\n\n");

    printf("==== Porting Guide Complete ====\n");

    vTaskEndScheduler();
    for(;;);
}

int main(void)
{
    uart_init();

    printf("========================================\n");
    printf("  FreeRTOS Lesson 08: Platform Porting\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("========================================\n\n");

    xTaskCreate(vPortingGuide, "Porting", 512, NULL, 1, NULL);

    vTaskStartScheduler();
    for (;;);
    return 0;
}
