# FreeRTOS QEMU 调试排查记录

## 问题现象

所有课程示例在 QEMU MPS2-AN385 上运行时，`vTaskStartScheduler()` 调用后任务不执行。
printf 在 `main()` 中正常输出，但进入调度器后无任何任务输出，QEMU 超时退出。

```
========================================
  FreeRTOS Lesson 01: Task Basics
  Platform: QEMU MPS2-AN385 (Cortex-M3)
========================================

Tasks created. Starting scheduler...

<--- 这里之后没有任何输出，任务未执行 --->
qemu-system-arm: terminating on signal 15 from pid XXXX (timeout)
```

## 排查思路

### 第一步：排除编译/链接问题

- 清除所有 `.o` 文件重新编译 → 问题依旧
- 检查 ELF 文件 size 正常，无编译错误/警告
- 用 `objdump` 确认 `vPortSVCHandler`、`xPortPendSVHandler`、`xPortSysTickHandler` 存在于二进制中

```bash
arm-none-eabi-objdump -d lesson01.elf | grep -A2 "vPortSVCHandler\|xPortPendSVHandler\|xPortSysTickHandler"
```

**结论：** 编译链接正常，FreeRTOS port 函数正确链入。

### 第二步：检查中断向量表

用 `objdump -s -j .isr_vector` 检查向量表内容：

```bash
arm-none-eabi-objdump -s -j .isr_vector lesson01.elf
```

确认：
- SP 初始值指向 `0x20400000`（RAM 顶部）✓
- Reset_Handler 地址正确 ✓
- SVCall (向量11) 指向 `vPortSVCHandler` ✓
- PendSV (向量14) 指向 `xPortPendSVHandler` ✓
- SysTick (向量15) 指向 `xPortSysTickHandler` ✓

**结论：** 向量表正确。

### 第三步：QEMU 中断追踪

使用 QEMU 的 `-d int` 选项追踪中断：

```bash
timeout 2 qemu-system-arm -machine mps2-an385 -nographic -kernel lesson01.elf -D /tmp/qemu_trace.log -d int
```

**发现：** 日志只有 2 行（reset 加载），没有任何中断触发！

**推断：** 调度器启动过程中某处阻塞，SysTick 未能成功配置或 SVC 调用出现问题。

### 第四步：裸机测试 SVC 和 SysTick

创建不依赖 FreeRTOS 的最小测试程序，分别测试 SVC 和 SysTick：

```c
// SVC 测试
__asm volatile("svc 0");
if (svc_fired) raw_puts("SVC OK!\r\n");

// SysTick 测试
*(volatile uint32_t *)0xE000E014 = 999999;  // LOAD
*(volatile uint32_t *)0xE000E010 = 7;       // CTRL: ENABLE|TICKINT|CLKSOURCE
```

**结果：**
- SVC → **OK** ✓
- SysTick (LOAD=999999, CTRL=7) → **FAILED** ✗

### 第五步：SysTick 详细诊断

进一步测试不同配置：

```c
SYSTICK_LOAD = 100;  // 非常小的 reload 值
SYSTICK_CTRL = 7;    // ENABLE | TICKINT | CLKSOURCE (processor clock)
// ... 循环等待 ...
// 结果: tick_count = 0x349 (大量tick触发!) → SysTick 正常工作！
```

**关键发现：** SysTick 本身在 QEMU 上可以正常工作，reload 值小时能触发大量中断。

**新推断：** 问题不在 SysTick 硬件本身，而是 FreeRTOS 启动过程中的断言失败导致死循环。

### 第六步：添加 HardFault 诊断输出

修改 `startup.c` 中的 `HardFault_Handler`，添加 UART 输出：

```c
void HardFault_Handler(void) {
    const char *msg = "!!! HARDFAULT !!!\r\n";
    // 通过 UART0 直接输出
    while (*msg) { while (*uart_state & 1); *uart_data = *msg++; }
    while (1);
}
```

**结果：** 没有 HardFault 输出。

### 第七步：添加 configASSERT 可视化 (突破口!)

在 `FreeRTOSConfig.h` 中定义带输出的 `configASSERT`：

```c
extern void vAssertCalled(const char *file, int line);
#define configASSERT( x ) if( !( x ) ) vAssertCalled( __FILE__, __LINE__ )
```

在 `syscalls.c` 中实现 `vAssertCalled`，通过 UART 打印文件名和行号。

**问题：** 发现文件中存在 **两处** `configASSERT` 定义！
- 第 15 行：新加的带输出版本
- 第 117 行：原有的静默死循环版本 `if((x) == 0) { taskDISABLE_INTERRUPTS(); for(;;); }`

第 117 行的定义覆盖了第 15 行的定义（预处理器 redefinition），导致：
- configASSERT 失败时只是静默关中断死循环
- 没有任何输出，看起来就像"什么都没发生"

**修复：** 删除第 117 行的重复定义。

### 第八步：定位真正的断言失败

修复重复定义后重新编译运行：

```
ASSERT: .../FreeRTOS/portable/GCC/ARM_CM3/port.c:369
```

**找到了！** 断言在 port.c 第 369 行失败。

### 第九步：分析 port.c:369

查看 port.c 第 369 行代码：

```c
configASSERT( ( configMAX_SYSCALL_INTERRUPT_PRIORITY & 0x1U ) == 0U );
```

这个断言的上下文：

```c
if( ulImplementedPrioBits == 8 )
{
    /* 当硬件实现 8 位优先级时，最低位始终用作子优先级。
     * 因此 configMAX_SYSCALL_INTERRUPT_PRIORITY 的 bit0 必须为 0 */
    configASSERT( ( configMAX_SYSCALL_INTERRUPT_PRIORITY & 0x1U ) == 0U );
    ulMaxPRIGROUPValue = 0;
}
```

**根因分析：**

QEMU MPS2-AN385 的 NVIC 实现了 **8 位**优先级（写入 0xFF 读回 0xFF）。当硬件有 8 位优先级时：
- 最低 1 位用于子优先级 (sub-priority)
- 高 7 位用于抢占优先级 (preemption priority)
- FreeRTOS 要求 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 的 bit0 必须为 0

我们原来的值：`191 = 0xBF = 10111111b`，bit0 = 1 → **断言失败！**

### 第十步：修复

将 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 从 191 (0xBF) 改为 160 (0xA0)：

```c
#define configKERNEL_INTERRUPT_PRIORITY         255   // 最低优先级
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    160   // 0xA0, bit0=0
```

160 = 0xA0 = 10100000b，bit0 = 0 → 满足要求。

### 最终验证

修复后所有 8 个课程均正常运行，任务调度、队列、信号量、定时器等功能全部正确。

---

## 根因总结

| 问题 | 原因 | 表现 |
|------|------|------|
| configASSERT 静默失败 | FreeRTOSConfig.h 中有两处 configASSERT 定义，后者（静默版本）覆盖了前者 | 断言失败时无任何输出，只是关中断死循环 |
| 调度器启动失败 | `configMAX_SYSCALL_INTERRUPT_PRIORITY = 191 (0xBF)` 的 bit0 为 1 | port.c:369 断言失败，FreeRTOS 要求 8 位优先级硬件上该值 bit0 必须为 0 |
| QEMU 8 位优先级 | QEMU MPS2-AN385 模拟了完整的 8 位 NVIC 优先级 | 与真实 STM32 (通常 3-4 位) 不同，需要不同的优先级配置 |

## 经验教训

### 1. configASSERT 必须有可见输出

```c
// ❌ 错误：静默死循环，极难调试
#define configASSERT(x) if((x)==0) { taskDISABLE_INTERRUPTS(); for(;;); }

// ✓ 正确：带文件名行号输出
extern void vAssertCalled(const char *file, int line);
#define configASSERT(x) if(!(x)) vAssertCalled(__FILE__, __LINE__)
```

### 2. 中断优先级配置必须匹配硬件

不同平台的 NVIC 优先级位数不同：
- STM32F1/F4: 4 位 (0-15)
- STM32L4: 4 位
- NXP LPC: 3-5 位
- **QEMU MPS2-AN385: 8 位** (0-255)

FreeRTOS 对 8 位优先级有额外要求：`configMAX_SYSCALL_INTERRUPT_PRIORITY` 的 bit0 必须为 0。

### 3. 调试 FreeRTOS 启动失败的优先级

1. 首先确认 HardFault 是否触发
2. 启用 configASSERT 并确保有输出
3. 用裸机代码验证硬件功能 (SVC, SysTick, UART)
4. 检查 QEMU `-d int` 追踪中断
5. 逐步缩小范围：main → scheduler → port → 具体断言

### 4. 避免头文件中的重复宏定义

C 语言的 `#define` 不会报错（只有 warning），后定义会覆盖前定义。
在 FreeRTOSConfig.h 中应确保每个宏只定义一次，使用 `#ifndef` 保护或在文件末尾统一放置。

---

## 最终配置 (已验证可用)

```c
// FreeRTOSConfig.h - QEMU MPS2-AN385 (Cortex-M3, 8-bit priority)
#define configCPU_CLOCK_HZ                  ((uint32_t)16000000)
#define configTICK_RATE_HZ                  ((TickType_t)1000)
#define configKERNEL_INTERRUPT_PRIORITY     255     // 最低优先级
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 160    // 0xA0, bit0=0

// Assert - 带可视化输出
extern void vAssertCalled(const char *file, int line);
#define configASSERT(x) if(!(x)) vAssertCalled(__FILE__, __LINE__)
```

## 使用的调试工具

| 工具 | 用途 |
|------|------|
| `arm-none-eabi-objdump -s -j .isr_vector` | 检查中断向量表内容 |
| `arm-none-eabi-objdump -d` | 反汇编确认函数存在 |
| `arm-none-eabi-nm` | 查看符号表 |
| `arm-none-eabi-gcc -E -dM` | 查看预处理后的宏定义 |
| `qemu-system-arm -d int -D logfile` | QEMU 中断追踪 |
| 裸机 UART 直接写入 | 绕过 printf/newlib 验证硬件 |
| `timeout N qemu-system-arm ...` | 自动超时退出 QEMU |
