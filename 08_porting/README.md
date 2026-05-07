# 第8课：Cortex-M3 移植层与上下文切换原理

这一课的重点不是“怎么改编译选项”，而是 FreeRTOS 在 Cortex-M3 上究竟靠什么把任务跑起来、切出去、再切回来。

最核心的链路只有三条：

1. `SVC`：启动第一 个任务
2. `PendSV`：做真正的上下文切换
3. `SysTick`：驱动 tick 递增，并在需要时请求 `PendSV`

## 本课目标

- 理解 `vTaskStartScheduler()` 之后第一个任务是如何启动的
- 理解 `taskYIELD()` 为什么只是“请求切换”，真正切换由 `PendSV` 完成
- 理解 `vTaskDelay()` 为什么最终依赖 `SysTick -> xTaskIncrementTick()` 才能被唤醒
- 通过可执行示例观察主动让出 CPU 和 tick 驱动唤醒这两类切换

## 运行方式

```bash
cd 08_porting
make clean
make
make run
```

## 启动第一任务的链路

从应用视角看，调用顺序好像只是：

```text
main()
  -> xTaskCreate(...)
  -> vTaskStartScheduler()
```

但移植层里真实发生的是：

```text
vTaskStartScheduler()
  -> xPortStartScheduler()
      -> 配置 PendSV / SysTick 优先级
      -> 配置 SysTick 周期
      -> prvPortStartFirstTask()
          -> svc 0
              -> vPortSVCHandler()
                  -> 从 pxCurrentTCB 恢复首个任务栈帧
                  -> 切到 PSP
                  -> 从异常返回进入第一个任务
```

也就是说，第一个任务并不是“普通函数调用进去的”，而是通过一次 SVC 异常返回进入的。

## `pxPortInitialiseStack()` 为什么重要

任务在真正运行前，FreeRTOS 就已经为它“伪造好”了一份异常返回现场。`pxPortInitialiseStack()` 会把这些内容压到任务栈里：

- xPSR
- PC
- LR
- R0-R3
- R12
- R4-R11

这样 `vPortSVCHandler()` / `xPortPendSVHandler()` 只要按约定把寄存器恢复出来，CPU 就会像“从一次中断返回”那样进入任务函数。

## 为什么 `PendSV` 用来做上下文切换

`PendSV` 的优势是：

- 它可以被延后处理
- 通常被设置为最低优先级异常
- 不会在普通中断处理中途打断更关键的异常路径

因此 FreeRTOS 的策略是：

- 谁想切换任务，不直接切
- 只负责把 `PendSV` 挂起
- 等 `PendSV` 统一完成上下文保存/恢复

## `taskYIELD()` 的真实含义

在这个 Cortex-M3 port 中，`taskYIELD()` 最终只是做：

```text
设置 ICSR.PENDSVSET 位
```

也就是请求一次 PendSV。

真正的切换要等 `xPortPendSVHandler()` 运行时才发生，它会：

1. 取出当前任务的 PSP
2. 保存 R4-R11 到当前任务栈
3. 把新栈顶写回当前 TCB
4. 调用 `vTaskSwitchContext()` 选下一个任务
5. 读取下一个任务的栈顶
6. 恢复 R4-R11
7. 写回 PSP 并异常返回

## `SysTick` 在切换里扮演什么角色

`SysTick` 本身不做完整上下文切换，它做的是两件事：

1. 调用 `xTaskIncrementTick()` 推进 RTOS tick
2. 如果发现需要调度，则挂起 `PendSV`

所以 `vTaskDelay()` 的唤醒路径是：

```text
任务调用 vTaskDelay()
  -> 进入 Blocked / delayed list

SysTick 到来
  -> xTaskIncrementTick()
      -> 发现延时到期，任务回到 ready list
      -> 如果需要切换，设置 PendSV

PendSV 运行
  -> 真正切到被唤醒的任务
```

## 为什么 `SysTick` 和 `PendSV` 都设为最低优先级

因为：

- 它们属于内核调度基础设施
- 不应该抢占更高优先级、时序更敏感的设备中断

但这也带来一条约束：

- 更高优先级的 ISR 如果要调用 FreeRTOS API，必须使用 `FromISR` 版本
- 并且其中断优先级不能高于 `configMAX_SYSCALL_INTERRUPT_PRIORITY`

## 启动文件为什么必须把向量表接对

在这个项目中，向量表必须直接指向：

- `SVCall -> vPortSVCHandler`
- `PendSV -> xPortPendSVHandler`
- `SysTick -> xPortSysTickHandler`

如果任何一项没接对，就会出现：

- 调度器启动后任务不跑
- tick 不增长
- 切换不发生
- 或者直接 HardFault

## 本课示例怎么看

示例分三段：

### 1. 首任务启动

第一个打印来自 `Coordinator`。只要它在 `vTaskStartScheduler()` 后马上运行，就说明：

- `xPortStartScheduler()` 已完成基础配置
- `prvPortStartFirstTask()` 已执行 `svc 0`
- `vPortSVCHandler()` 已成功恢复第一任务上下文

### 2. 主动让出 CPU

`YieldA` 和 `YieldB` 是同优先级任务，会在同一个 tick 附近互相 `taskYIELD()`。

如果你看到它们在非常接近的 tick 上交替输出，就说明：

- 切换不是靠 delay 或超时触发的
- 而是主动挂起 `PendSV` 完成的

### 3. tick 驱动唤醒

`DelayTask` 调用 `vTaskDelay(200ms)` 后进入 `Blocked`。

监控输出会看到它先变成 `Blocked`，再在目标 tick 附近恢复运行。这正对应：

```text
SysTick -> xTaskIncrementTick() -> PendSV
```

## 和源码对照时建议先看哪里

建议按这个顺序：

1. `common/startup.c` 中的向量表
2. `FreeRTOS/portable/GCC/ARM_CM3/port.c` 中的 `prvPortStartFirstTask()`
3. `FreeRTOS/portable/GCC/ARM_CM3/port.c` 中的 `vPortSVCHandler()`
4. `FreeRTOS/portable/GCC/ARM_CM3/portmacro.h` 中的 `portYIELD()`
5. `FreeRTOS/portable/GCC/ARM_CM3/port.c` 中的 `xPortPendSVHandler()`
6. `FreeRTOS/portable/GCC/ARM_CM3/port.c` 中的 `xPortSysTickHandler()`
7. `FreeRTOS/tasks.c` 中的 `xTaskIncrementTick()` 和 `vTaskSwitchContext()`

这样能把“启动首任务”和“运行时切换”两条主线都串起来。