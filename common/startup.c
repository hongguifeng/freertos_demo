/*
 * STM32F405 启动代码 (Startup Code)
 * 
 * 用于 QEMU netduinoplus2 模拟器
 * 
 * 功能：
 * 1. 定义中断向量表
 * 2. 初始化 .data 段 (从Flash复制到RAM)
 * 3. 清零 .bss 段
 * 4. 调用 main()
 */

#include <stdint.h>

/* 来自链接脚本的符号 */
extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

/* main 函数声明 */
extern int main(void);

/* 系统初始化 (弱定义，可被覆盖) */
void SystemInit(void) __attribute__((weak));

/* 默认中断处理函数 */
void Default_Handler(void);
void Reset_Handler(void);

/* Cortex-M4 系统中断 */
void NMI_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)   __attribute__((weak));
void MemManage_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)    __attribute__((weak, alias("Default_Handler")));

/* FreeRTOS 使用的中断处理函数 (在 port.c 中定义) */
extern void vPortSVCHandler(void);
extern void xPortPendSVHandler(void);
extern void xPortSysTickHandler(void);

/* STM32F405 外设中断 (部分) */
void USART1_IRQHandler(void)   __attribute__((weak, alias("Default_Handler")));
void USART2_IRQHandler(void)   __attribute__((weak, alias("Default_Handler")));
void TIM2_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void TIM3_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void TIM4_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));

/*
 * 中断向量表
 * 放置在 .isr_vector 段，链接到Flash起始地址 0x08000000
 */
__attribute__((section(".isr_vector")))
const uint32_t vector_table[] = {
    (uint32_t)&_estack,          /* 初始栈指针 */
    (uint32_t)Reset_Handler,     /* Reset */
    (uint32_t)NMI_Handler,       /* NMI */
    (uint32_t)HardFault_Handler, /* Hard Fault */
    (uint32_t)MemManage_Handler, /* MPU Fault */
    (uint32_t)BusFault_Handler,  /* Bus Fault */
    (uint32_t)UsageFault_Handler,/* Usage Fault */
    0, 0, 0, 0,                  /* Reserved */
    (uint32_t)vPortSVCHandler,   /* SVCall (FreeRTOS使用) */
    (uint32_t)DebugMon_Handler,  /* Debug Monitor */
    0,                           /* Reserved */
    (uint32_t)xPortPendSVHandler,/* PendSV (FreeRTOS使用) */
    (uint32_t)xPortSysTickHandler,/* SysTick (FreeRTOS使用) */

    /* STM32F405 外设中断 (IRQ 0-81) */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* IRQ 0-9 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* IRQ 10-19 */
    0, 0, 0, 0, 0, 0, 0, 0,        /* IRQ 20-27 */
    (uint32_t)TIM2_IRQHandler,      /* IRQ 28: TIM2 */
    (uint32_t)TIM3_IRQHandler,      /* IRQ 29: TIM3 */
    (uint32_t)TIM4_IRQHandler,      /* IRQ 30: TIM4 */
    0, 0, 0, 0, 0, 0,              /* IRQ 31-36 */
    (uint32_t)USART1_IRQHandler,    /* IRQ 37: USART1 */
    (uint32_t)USART2_IRQHandler,    /* IRQ 38: USART2 */
};

/*
 * Reset Handler - 系统启动入口
 */
void Reset_Handler(void)
{
    uint32_t *src, *dst;

    /* 复制 .data 段从 Flash 到 RAM */
    src = &_sidata;
    dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* 清零 .bss 段 */
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    /* 调用系统初始化 */
    SystemInit();

    /* 调用 main */
    main();

    /* main 不应该返回，如果返回则死循环 */
    while (1);
}

/*
 * 默认中断处理 - 死循环
 */
void Default_Handler(void)
{
    while (1);
}

/*
 * Hard Fault 处理
 */
void HardFault_Handler(void)
{
    /* Write "HARDFAULT!\n" directly via UART0 */
    volatile uint32_t *uart_data = (volatile uint32_t *)0x40004000;
    volatile uint32_t *uart_state = (volatile uint32_t *)0x40004004;
    const char *msg = "!!! HARDFAULT !!!\r\n";
    while (*msg) {
        while (*uart_state & 1);
        *uart_data = *msg++;
    }
    while (1);
}

/*
 * 弱定义的 SystemInit，可被用户覆盖
 */
void __attribute__((weak)) SystemInit(void)
{
    /* MPS2-AN385 (Cortex-M3): 无FPU，无需特殊初始化 */
}
