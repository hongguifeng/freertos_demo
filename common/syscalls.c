/*
 * 系统调用桩函数 (Syscalls)
 * 
 * 为 newlib 提供最小的系统调用实现
 * 使用 CMSDK UART0 输出到 QEMU 控制台
 * 
 * MPS2-AN385 平台 UART0 地址: 0x40004000
 * 该平台基于 ARM Cortex-M3，与 STM32F1/F2 系列架构相同
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>

/* CMSDK UART0 寄存器 (MPS2-AN385) */
#define UART0_BASE      0x40004000
#define UART0_DATA      (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART0_STATE     (*(volatile uint32_t *)(UART0_BASE + 0x04))
#define UART0_CTRL      (*(volatile uint32_t *)(UART0_BASE + 0x08))
#define UART0_BAUDDIV   (*(volatile uint32_t *)(UART0_BASE + 0x10))

#define UART_STATE_TXFULL  (1 << 0)
#define UART_CTRL_TX_EN    (1 << 0)

/*
 * 初始化 UART0
 */
void uart_init(void)
{
    /* 设置波特率分频 (25MHz / 115200 ≈ 217) */
    UART0_BAUDDIV = 217;
    /* 使能发送 */
    UART0_CTRL = UART_CTRL_TX_EN;
}

/*
 * 通过 UART0 发送一个字符
 */
static void uart_putc(char c)
{
    /* 等待发送缓冲区非满 */
    while (UART0_STATE & UART_STATE_TXFULL);
    UART0_DATA = (uint32_t)c;
}

/* 堆的结束地址 */
extern uint32_t end;
static uint8_t *heap_end = 0;

/*
 * _write - 写输出 (printf 最终会调用这个)
 */
static int line_start = 1;

int _write(int file, char *ptr, int len)
{
    (void)file;
    for (int i = 0; i < len; i++) {
        if (line_start) {
            const char *prefix = "[APP] --> ";
            while (*prefix) uart_putc(*prefix++);
            line_start = 0;
        }
        if (ptr[i] == '\n') {
            uart_putc('\r');
            line_start = 1;
        }
        uart_putc(ptr[i]);
    }
    return len;
}

/*
 * Assert handler for FreeRTOS configASSERT
 */
void vAssertCalled(const char *file, int line)
{
    (void)line;
    uart_putc('A'); uart_putc('S'); uart_putc('S'); uart_putc('E');
    uart_putc('R'); uart_putc('T'); uart_putc(':'); uart_putc(' ');
    while (*file) uart_putc(*file++);
    uart_putc(':');
    /* Print line number */
    char buf[10];
    int i = 0;
    int l = line;
    if (l == 0) { buf[i++] = '0'; }
    else { while(l) { buf[i++] = '0' + l%10; l /= 10; } }
    while (i > 0) uart_putc(buf[--i]);
    uart_putc('\r'); uart_putc('\n');
    while (1);
}

/*
 * _read - 读输入 (暂不实现)
 */
int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

/*
 * _sbrk - 堆内存分配
 */
void *_sbrk(int incr)
{
    if (heap_end == 0) {
        heap_end = (uint8_t *)&end;
    }
    uint8_t *prev_heap_end = heap_end;
    heap_end += incr;
    return (void *)prev_heap_end;
}

/*
 * 其他必要的桩函数
 */
int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

void _exit(int status)
{
    (void)status;
    while (1);
}

void _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
}

int _getpid(void)
{
    return 1;
}
