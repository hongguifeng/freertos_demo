# 第1课：任务基础与等待原理
---

## 什么是 Task：从裸机到 RTOS 的核心抽象

### 裸机程序 vs RTOS 任务

在裸机（bare-metal）程序里，只有一个 `main()` 函数顺序执行，所有逻辑塞在一个大循环中：

```c
// 裸机典型结构
int main(void) {
    while(1) {
        read_sensor();
        update_display();
        check_buttons();
    }
}
```

问题：如果 `read_sensor()` 要等 10ms 的 I2C 传输，整个系统都在空转。

**RTOS 的解决方案**：把每个逻辑单元封装为"任务"（Task）。每个任务拥有自己独立的栈和执行上下文，调度器在它们之间快速切换，让多个任务"看起来"同时在运行。

### 任务的本质：一个永不返回的函数 + 独立栈

在 FreeRTOS 中，一个任务就是一个符合特定签名的 C 函数：

```c
void vMyTask(void *pvParameters)
{
    // 初始化
    for (;;) {
        // 任务逻辑（永远不退出）
    }
}
```

关键规则：
- **永不返回** — 任务函数必须是无限循环或在结束前调用 `vTaskDelete(NULL)`
- **独立栈** — 每个任务有自己的栈空间，局部变量互不干扰
- **独立上下文** — 每个任务"认为"自己独占 CPU

---

## TCB：任务控制块 (Task Control Block)

每创建一个任务，内核都会分配一个 `TCB_t` 结构体来管理它的全部信息：

```c
typedef struct tskTaskControlBlock {
    volatile StackType_t *pxTopOfStack;   // ★ 栈顶指针 (必须是第一个成员)
    
    ListItem_t xStateListItem;   // 状态链表节点 (Ready/Blocked/Suspended)
    ListItem_t xEventListItem;   // 事件链表节点 (等待队列/信号量时用)
    UBaseType_t uxPriority;      // 任务优先级 (0=最低)
    StackType_t *pxStack;        // 栈底指针
    char pcTaskName[configMAX_TASK_NAME_LEN];  // 任务名 (调试用)
    
    // ... 其他可选字段 (互斥量、通知、统计等)
} TCB_t;
```

### 内存布局

`xTaskCreate()` 会从堆上分配 TCB + 栈：

```
pvPortMalloc 分配的内存：
┌─────────────────┐  ← pxStack (栈底, 低地址)
│                 │
│   Task Stack    │  ← 向上增长 (ARM: 实际向下增长)
│                 │
│─────────────────│  ← pxTopOfStack (当前栈顶)
│  [保存的寄存器]  │     R4-R11, R0-R3, R12, LR, PC, xPSR
│                 │
└─────────────────┘  ← 栈顶 (高地址)

┌─────────────────┐
│      TCB_t      │  ← 任务控制块
└─────────────────┘
```

### pxTopOfStack 为什么是第一个成员？

PendSV 中断做上下文切换时，需要快速找到当前任务的栈顶指针来保存/恢复寄存器。
把 `pxTopOfStack` 放在结构体第一个位置意味着：

```c
// TCB 的地址 == pxTopOfStack 的地址
// 汇编里不需要任何偏移计算
LDR R0, [pxCurrentTCB]   // 取 TCB 地址
LDR R0, [R0]             // 偏移 0 就是 pxTopOfStack
```

---

## 任务创建过程 (xTaskCreate 内部)

```
xTaskCreate(vMyTask, "Task", stackDepth, params, priority, &handle)
  │
  ├─ 1. pvPortMalloc(sizeof(TCB_t))          // 分配 TCB
  ├─ 2. pvPortMalloc(stackDepth * 4)         // 分配栈 (Cortex-M: 4字节/单位)
  ├─ 3. 初始化 TCB 各字段
  │       uxPriority = priority
  │       pcTaskName = "Task"
  │       xStateListItem / xEventListItem 初始化
  │
  ├─ 4. pxPortInitialiseStack()              // ★ 伪造初始栈帧
  │       在栈顶构造一个"假的"异常返回帧：
  │       ┌──────────┐  高地址
  │       │  xPSR    │  = 0x01000000 (Thumb 位)
  │       │  PC      │  = vMyTask (任务入口)
  │       │  LR      │  = prvTaskExitError (防止错误返回)
  │       │  R12     │
  │       │  R3      │
  │       │  R2      │
  │       │  R1      │
  │       │  R0      │  = pvParameters (任务参数)
  │       │  R11~R4  │  = 0 (手动保存的寄存器)
  │       └──────────┘  低地址 ← pxTopOfStack
  │
  └─ 5. prvAddTaskToReadyList(pxNewTCB)      // 加入就绪列表
         如果新任务优先级 > 当前任务 → 触发调度
```

### 为什么要"伪造栈帧"？

当调度器第一次切换到新任务时，PendSV 中断会执行"恢复上下文"操作——从栈中弹出寄存器。
新任务从未运行过，没有真实的上下文可恢复，所以创建时就预先在栈上放好初始值。
CPU 恢复这些值后，PC = 任务函数地址，R0 = 参数，程序就从任务入口开始执行了。

---

## 上下文切换：任务如何"暂停"和"恢复"

### 切换触发

上下文切换由 **PendSV** 异常执行（最低优先级，确保不打断其他中断）：

```
触发时机：
1. SysTick 中断 → xTaskIncrementTick() 返回 pdTRUE → 设置 PendSV
2. 任务主动让出 → taskYIELD() → 设置 PendSV
3. 高优先级任务就绪 → 设置 PendSV
```

### PendSV 中切换过程

```
xPortPendSVHandler:
  ┌─ 保存当前任务上下文 ─┐
  │  MRS R0, PSP          │  // 取当前任务栈指针
  │  STMDB R0!, {R4-R11}  │  // 手动保存 R4-R11 (硬件自动保存了其余)
  │  STR R0, [pxCurrentTCB]│  // 更新 TCB.pxTopOfStack
  └────────────────────────┘
  
  ┌─ 选择下一个任务 ─────┐
  │  BL vTaskSwitchContext │  // 调用 taskSELECT_HIGHEST_PRIORITY_TASK
  └────────────────────────┘
  
  ┌─ 恢复新任务上下文 ───┐
  │  LDR R0, [pxCurrentTCB]│  // 新任务的 pxTopOfStack
  │  LDMIA R0!, {R4-R11}  │  // 恢复 R4-R11
  │  MSR PSP, R0          │  // 设置 PSP
  │  BX LR                │  // 异常返回 (硬件自动恢复 R0-R3,R12,LR,PC,xPSR)
  └────────────────────────┘
```

**核心思想**：每个任务的栈保存了该任务被中断时的完整 CPU 状态。
切换时只需把 SP 指向另一个任务的栈，CPU 就"回到"那个任务上次被打断的地方继续执行。

---

## 任务状态与生命周期

```
                  xTaskCreate()
                       │
                       ▼
                ┌─────────────┐
                │    Ready    │◄──────── vTaskResume()
                └──────┬──────┘         xQueueReceive() 成功
                       │                xSemaphoreTake() 成功
            调度器选中  │
                       ▼
                ┌─────────────┐
         ┌─────│   Running   │─────┐
         │     └─────────────┘     │
         │            │            │
   被更高优先级抢占    │    vTaskDelay() / 等待队列 / 等待信号量
   或时间片用完       │            │
         │            │            ▼
         │            │     ┌─────────────┐
         │            │     │   Blocked   │ (在 delayed list 或 event list 中)
         │            │     └─────────────┘
         │            │
         │     vTaskSuspend()
         │            │
         │            ▼
         │     ┌─────────────┐
         │     │  Suspended  │ (在 xSuspendedTaskList 中)
         │     └─────────────┘
         │
         └───► 回到 Ready
```

每个状态对应一条链表：
- **Ready**: `pxReadyTasksLists[优先级]` — 按优先级分组
- **Blocked**: `pxDelayedTaskList` — 按唤醒时间排序
- **Suspended**: `xSuspendedTaskList` — 无序，需显式恢复

---

## 从"任务是什么"到"任务如何等待"

理解了上面的基础后，后续内容将深入**任务等待**的具体实现：
当任务调用 `vTaskDelay()` 时，内核如何把它从 Ready 链表迁移到 Delayed 链表，
以及 SysTick 中断如何在正确的时刻将其唤醒。

---
这一课不再只停留在 API 用法，而是把 `Task` 的等待/唤醒链路拆开。重点不是“调用了 `vTaskDelay()` 会延时”，而是任务在内核里究竟被放进了哪张链表、何时被挪回 Ready 列表，以及为什么 `vTaskDelayUntil()` 能避免周期漂移。

## 本课目标

- 看懂 `Running -> Blocked -> Ready -> Running` 的真实内核路径
- 理解 `vTaskDelay()` 和 `vTaskDelayUntil()` 的差异
- 通过可执行示例观察 `eTaskGetState()`、tick 计数和唤醒时机
- 知道等待机制并不是“忙等”，而是任务从 Ready 链表迁移到 Delayed 链表

## 运行方式

```bash
cd 01_task_basics
make clean
make
make run
```

示例里有三个任务：

- `Monitor`：高优先级监控任务，每 100ms 采样一次其他任务状态
- `RelativeDelay`：优先级 2，调用 `vTaskDelay(300ms)`，演示相对延时
- `AbsoluteDelay`：优先级 1，调用 `vTaskDelayUntil(500ms)`，演示绝对周期唤醒

`Monitor` 故意比工作任务优先级更高，这样它能在任务刚被唤醒、但还没真正拿到 CPU 之前先看到它是 `Ready` 还是 `Blocked`。

## 任务等待不是忙等

很多初学者会把 `vTaskDelay(500)` 理解成“当前任务在 CPU 上空转 500 个 tick”。这和 FreeRTOS 的真实实现正好相反。

当任务进入等待时，内核会把它从对应优先级的 Ready 链表中移除，然后按“唤醒 tick”插入延时链表。CPU 会立即转去执行别的 Ready 任务，或者在无任务可运行时执行 Idle Task。

核心状态迁移可以概括成：

```text
Running
  |
  | vTaskDelay() / vTaskDelayUntil()
  v
Blocked
  |   xStateListItem 被插入 delayed list
  |   key = xTimeToWake
  |
  | SysTick -> xTaskIncrementTick()
  v
Ready
  |   回到对应优先级 ready list
  |
  | PendSV / 调度决策
  v
Running
```

## `vTaskDelay()` 的实现链路

`vTaskDelay()` 是“相对等待”。调用点只给出“还要等多少 tick”，不关心绝对唤醒时刻。

调用链可以简化为：

```text
vTaskDelay(xTicksToDelay)
  -> vTaskSuspendAll()
  -> prvAddCurrentTaskToDelayedList(xTicksToDelay, pdFALSE)
  -> xTaskResumeAll()
  -> taskYIELD_WITHIN_API()
```

关键逻辑在 `prvAddCurrentTaskToDelayedList()`：

1. 先把当前任务从 Ready 链表移除。
2. 计算 `xTimeToWake = xTickCount + xTicksToWait`。
3. 把 `xStateListItem` 的排序值设置为 `xTimeToWake`。
4. 按唤醒时间有序插入 `pxDelayedTaskList`。
5. 如果它排在链表头部，顺便更新 `xNextTaskUnblockTime`。

这意味着等待逻辑的本质是“链表迁移 + 按唤醒时间排序”，不是死循环，也不是定时器回调表。

## 为什么有两张 delayed list

在 `tasks.c` 里你会看到：

- `xDelayedTaskList1`
- `xDelayedTaskList2`
- `pxDelayedTaskList`
- `pxOverflowDelayedTaskList`

原因是 `xTickCount` 会溢出。假设当前 tick 已接近 `0xFFFFFFFF`，再加上等待时间后会回绕到较小值。如果仍然放到同一张有序链表里，排序语义就会出错。

因此 FreeRTOS 用两张延时链表区分：

- 当前 tick 周期内会到期的任务
- tick 回绕之后才会到期的任务

当 `xTickCount` 溢出到 0 时，内核交换这两张链表的角色，继续按相同逻辑处理。

## `vTaskDelayUntil()` 为什么更适合周期任务

`vTaskDelayUntil()` 不是基于“现在再等多久”，而是基于“下一次应该在哪个绝对 tick 醒来”。

它会维护一个 `xLastWakeTime`：

```text
xTimeToWake = *pxPreviousWakeTime + xTimeIncrement
```

然后判断当前 tick 是否已经超过这个目标时间：

- 如果还没到，就进入 Blocked 状态
- 如果已经晚了，就直接继续执行，不再额外等待

这能避免下面这种累积漂移：

```text
do_work();
vTaskDelay(500ms);
```

因为 `do_work()` 本身也要耗时，所以循环周期通常会变成：

```text
实际周期 = 执行时间 + 500ms
```

而 `vTaskDelayUntil()` 的周期更接近：

```text
实际周期 ≈ 固定周期
```

这就是它更适合采样任务、控制环、周期心跳任务的原因。

## Tick 中断如何把任务唤醒

等待任务真正被唤醒，不是因为某个任务主动轮询，而是依赖 SysTick 中断驱动：

```text
xPortSysTickHandler()
  -> xTaskIncrementTick()
      -> xTickCount++
      -> 检查 pxDelayedTaskList 头结点是否到期
      -> 到期则移出 delayed list
      -> 放回对应优先级的 ready list
      -> 必要时请求上下文切换
```

为什么只看 delayed list 的头部就够了？因为任务进入 delayed list 时已经按 `xTimeToWake` 排序。一旦头结点还没到期，后面的任务只会更晚到期，不需要继续扫描。

## Blocked 和 Suspended 的区别

这两个状态很容易混淆：

- `Blocked`：任务在等“某个条件”，并且通常带超时。这个条件可以是时间、队列、信号量、事件组、任务通知。
- `Suspended`：任务被显式挂起，不在等超时，不会被 tick 自动唤醒。

本课示例最后会把两个工作任务 `vTaskSuspend()`，用来对比：

- 时间等待使用 delayed list
- 挂起使用 suspended list

这能帮助你把“等待模块”和“挂起模块”彻底区分开。

## 和源码对照时建议先看哪里

建议按下面顺序阅读内核：

1. `FreeRTOS/tasks.c` 中的 `vTaskDelay()`
2. `FreeRTOS/tasks.c` 中的 `vTaskDelayUntil()`
3. `FreeRTOS/tasks.c` 中的 `prvAddCurrentTaskToDelayedList()`
4. `FreeRTOS/tasks.c` 中的 `xTaskIncrementTick()`
5. `FreeRTOS/portable/GCC/ARM_CM3/port.c` 中的 `xPortSysTickHandler()`

这样读会更容易把“API 调用点”和“调度器内部链表操作”串起来。

## 观察示例时看什么

运行示例时，重点看三类输出：

1. `RelativeDelay` 打印当前 tick 和预计唤醒 tick，说明它要被放进 delayed list 多久。
2. `AbsoluteDelay` 打印固定周期目标 tick，说明它按绝对时间调度。
3. `Monitor` 打印 `Ready / Blocked / Suspended`，帮助你把源码里的链表迁移和终端输出对上。

如果你看到某次采样里任务状态是 `Ready`，而下一行马上就是它的业务输出，通常说明：

- 这个任务刚在 `xTaskIncrementTick()` 中被移出 delayed list
- 但监控任务优先级更高，所以先观测到了它
- 监控任务一旦再次延时，该任务就会立刻拿到 CPU

这正是“等待链路”最值得观察的瞬间。