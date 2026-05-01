# 第1课：任务基础与等待原理

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