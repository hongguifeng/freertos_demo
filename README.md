# FreeRTOS 从零开始教程

本教程从零开始，深入讲解 FreeRTOS 的核心原理和使用方法。每个知识点都配有可在 QEMU 上运行的示例代码。

## 开发环境

- **编译器**: arm-none-eabi-gcc (ARM Cortex-M3)
- **模拟器**: QEMU `mps2-an385` (ARM MPS2 Cortex-M3)
- **FreeRTOS版本**: V10.5.1 (FreeRTOS-Kernel)
- **目标平台**: MPS2-AN385 (ARM Cortex-M3, CMSDK UART0 控制台输出)
- **工具链路径**: `/opt/gcc-arm-none-eabi-10.3-2021.10/bin/`

## 教程目录

| 课程 | 主题 | 核心知识点 |
|------|------|-----------|
| [01](01_task_basics/) | 任务基础 | 任务创建、调度器启动、任务状态、等待与唤醒原理 |
| [02](02_task_management/) | 任务管理 | 优先级、时间片、任务挂起与恢复 |
| [03](03_queue/) | 队列通信 | 队列创建、发送、接收、多任务通信、基于事件链表的阻塞与唤醒 |
| [04](04_semaphore_mutex/) | 信号量与互斥锁 | 二值信号量、计数信号量、互斥锁、优先级反转、优先级继承 |
| [05](05_software_timer/) | 软件定时器 | 单次定时器、周期定时器、定时器守护任务、命令队列 |
| [06](06_event_groups/) | 事件组 | 事件标志、多事件等待、任务同步、unordered event list |
| [07](07_memory_management/) | 内存管理 | heap_1~heap_5、内存分配策略 |
| [08](08_porting/) | 平台移植 | 移植层详解、SVC/PendSV/SysTick、上下文切换 |

## 原理补充

- [第1课讲义：任务等待与唤醒原理](01_task_basics/README.md)
- [第3课讲义：队列阻塞与事件链表原理](03_queue/README.md)
- [第4课讲义：信号量、互斥锁与优先级继承原理](04_semaphore_mutex/README.md)
- [第5课讲义：软件定时器守护任务与命令队列原理](05_software_timer/README.md)
- [第6课讲义：事件组与多条件等待原理](06_event_groups/README.md)
- [第8课讲义：Cortex-M3 移植层与上下文切换原理](08_porting/README.md)

## 快速开始

```bash
# 1. 确保工具链可用
/opt/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gcc --version
qemu-system-arm --version

# 2. 进入任意课程目录
cd 01_task_basics

# 3. 编译
make

# 4. 运行 (QEMU MPS2-AN385, Ctrl+A X 退出)
make run
# 或手动执行:
# qemu-system-arm -machine mps2-an385 -nographic -kernel lesson01.elf
```

## 项目结构

```
freertos_demo/
├── README.md                    # 本文件
├── FreeRTOS/                    # FreeRTOS 内核源码
│   ├── Source/                  # 内核源代码
│   │   ├── include/             # 头文件
│   │   ├── portable/            # 移植层代码
│   │   └── *.c                  # 内核实现
│   └── ...
├── common/                      # 公共代码 (启动文件、链接脚本等)
│   ├── startup.c                # 启动代码
│   ├── syscalls.c               # 系统调用桩函数
│   ├── linker.ld                # 链接脚本
│   └── Makefile.common          # 公共 Makefile
├── 01_task_basics/              # 课程1: 任务基础
├── 02_task_management/          # 课程2: 任务管理
├── 03_queue/                    # 课程3: 队列通信
├── 04_semaphore_mutex/          # 课程4: 信号量与互斥锁
├── 05_software_timer/           # 课程5: 软件定时器
├── 06_event_groups/             # 课程6: 事件组
├── 07_memory_management/        # 课程7: 内存管理
└── 08_porting/                  # 课程8: 平台移植
```

## FreeRTOS 核心架构

```
┌─────────────────────────────────────────────────┐
│                 用户应用程序                       │
├─────────────────────────────────────────────────┤
│              FreeRTOS API 层                      │
│  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌──────┐     │
│  │Task │ │Queue│ │Sema │ │Timer│ │Event │     │
│  │Mgmt │ │     │ │/Mutex│ │     │ │Group │     │
│  └─────┘ └─────┘ └─────┘ └─────┘ └──────┘     │
├─────────────────────────────────────────────────┤
│              FreeRTOS 内核                        │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐    │
│  │调度器     │ │内存管理   │ │中断管理       │    │
│  │Scheduler │ │Heap Mgmt │ │IRQ Handling  │    │
│  └──────────┘ └──────────┘ └──────────────┘    │
├─────────────────────────────────────────────────┤
│              硬件抽象层 (Portable)                 │
│  ┌──────────────────────────────────────────┐   │
│  │  port.c / portmacro.h (平台相关代码)       │   │
│  └──────────────────────────────────────────┘   │
├─────────────────────────────────────────────────┤
│              硬件 (MCU)                           │
└─────────────────────────────────────────────────┘
```
