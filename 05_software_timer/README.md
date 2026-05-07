# 第5课：软件定时器守护任务与命令队列原理

软件定时器最容易被误解的一点，是“定时器回调不是在触发它的任务里执行的，也不是在 SysTick 中断里执行的”。

在 FreeRTOS 中，软件定时器真正的执行者是专门的守护任务：

```text
Timer Service Task / Daemon Task
```

所有 `xTimerStart()`、`xTimerStop()`、`xTimerReset()`、`xTimerChangePeriod()`，以及 `xTimerPendFunctionCall()`，最终都只是往同一条命令队列发消息，由 daemon task 串行处理。

## 本课目标

- 理解软件定时器为什么本质上是“守护任务 + 命令队列 + 到期链表”
- 理解 timer callback 不在调用者上下文，而是在 daemon task 上下文中执行
- 理解 `xTimerPendFunctionCall()` 为什么和 timer callback 走同一条执行通道
- 通过示例观察 start/change/reset/stop 命令和到期回调的关系

## 运行方式

```bash
cd 05_software_timer
make clean
make
make run
```

示例包含：

- `Periodic`：周期定时器，先 250ms，后改成 400ms
- `OneShot`：单次定时器，可重启
- `Controller`：发送各种 timer 命令并排队 pended function

## 软件定时器的核心结构

在 `timers.c` 里，有三个非常关键的全局对象：

- `xTimerQueue`
- `xActiveTimerList1`
- `xActiveTimerList2`

对应关系是：

- `xTimerQueue`：接收所有 timer 命令和 pended function call
- `xActiveTimerList1/2`：保存已经启动、且按到期时间排序的活跃定时器

这和任务延时模块很像。任务等待使用 delayed list，软件定时器使用 active timer list，本质上都是“按唤醒/到期时间排序”的链表模型。

## `prvTimerTask()` 的主循环

daemon task 的核心主线非常简洁：

```text
for(;;)
  1. 看下一只定时器何时到期
  2. 如果已到期就处理到期定时器
  3. 否则阻塞，等待“到期”或“命令到达”二者之一
  4. 清空 xTimerQueue 中收到的命令
```

也就是说，这个任务既像：

- 一个“定时调度器”

又像：

- 一个“命令处理器”

## 为什么 timer API 只是“发命令”

例如：

```text
xTimerStart()
xTimerStop()
xTimerReset()
xTimerChangePeriod()
```

这些 API 并不直接去改 active timer list，而是把命令封装成消息发到 `xTimerQueue`。真正修改链表的是 daemon task。

这带来两个重要结果：

1. timer 数据结构只被一个上下文集中维护，减少并发复杂度。
2. 调用 timer API 的任务不会直接执行回调逻辑，回调总在 daemon task 中串行执行。

这也是为什么示例里 callback 打印出的当前任务优先级会等于 `configTIMER_TASK_PRIORITY`。

## 为什么有两张 active timer list

和任务延时链表一样，定时器的到期 tick 也会遇到 tick 回绕问题。

因此 FreeRTOS 也给软件定时器准备了两张链表：

- 当前 tick 周期内到期的 timer
- tick 回绕之后才到期的 timer

当 tick 溢出时，内核会切换两张链表的角色。

## callback 为什么不能阻塞

因为所有 timer callback 都运行在同一个 daemon task 里。

如果某个 callback 阻塞了：

- 其他 timer callback 都会被拖住
- timer 命令队列也无法及时清空
- `xTimerPendFunctionCall()` 的延后执行也会一起被拖住

所以 timer callback 的设计原则是：

- 快速
- 非阻塞
- 尽量只做状态推进或投递工作

## `xTimerPendFunctionCall()` 和普通定时器的关系

很多人把它当成“额外功能”，但从 `timers.c` 看，它和 timer 命令本质上走的是同一条 daemon task 队列。

区别只是：

- timer 命令是正向 message id
- pended function call 是负向 message id

daemon task 在取出消息后会分支判断：

- 是 timer 命令，就处理定时器状态
- 是 pended function，就直接执行该函数

所以它本质上是“借用 timer daemon task 做延后执行”。

## 本课示例怎么看

运行时重点看三类输出：

1. `Controller` 在某个 tick 发出命令
2. timer callback 在后续 tick 由 daemon task 执行
3. pended function 也在 daemon task 上下文执行

你会发现：

- callback 打印的当前优先级是 `configTIMER_TASK_PRIORITY`
- `xTimerChangePeriod()` 之后，周期 timer 的触发间隔会改变
- `xTimerPendFunctionCall()` 的输出与 timer callback 处于同一个执行上下文模型

## 和源码对照时建议先看哪里

建议按这个顺序阅读：

1. `FreeRTOS/timers.c` 中的 `prvTimerTask()`
2. `FreeRTOS/timers.c` 中的 `prvProcessTimerOrBlockTask()`
3. `FreeRTOS/timers.c` 中的 `prvProcessReceivedCommands()`
4. `FreeRTOS/timers.c` 中的 `prvInsertTimerInActiveList()`
5. `FreeRTOS/timers.c` 中的 `xTimerPendFunctionCall()`

这样最容易把“timer 命令入队 -> daemon task 醒来 -> 修改链表/执行回调”的链路串起来。