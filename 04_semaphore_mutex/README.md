# 第4课：信号量、互斥锁与优先级继承原理

这一课要补清楚两件事：

1. `Semaphore` 并不是独立内核对象，它本质上就是“item size 为 0 的 queue”。
2. `Mutex` 也建立在 queue 之上，但额外保存了“持有者”和“优先级继承”信息，因此它不只是一个特殊名字的 semaphore。

## 本课目标

- 理解二值信号量、计数信号量、互斥锁在内核里的共性与差异
- 理解为什么 semaphore 更适合做同步，mutex 更适合保护共享资源
- 看懂优先级反转和 FreeRTOS 的优先级继承是如何落到代码里的
- 通过示例观察 `Blocked -> Ready -> Running`、资源计数变化、互斥锁持有者优先级提升

## 运行方式

```bash
cd 04_semaphore_mutex
make clean
make
make run
```

示例分成三个阶段：

- Stage A: 二值信号量，同步型等待
- Stage B: 计数信号量，资源池等待
- Stage C: 互斥锁，优先级继承

## 信号量为什么本质上是 queue

在 `queue.c` 中，`xQueueSemaphoreTake()` 的注释直接说明了核心事实：

```text
Semaphores are queues with an item size of 0,
and uxMessagesWaiting is the semaphore count.
```

这句话决定了后面的全部行为：

- 二值信号量：`uxMessagesWaiting` 只会在 0 和 1 之间变化
- 计数信号量：`uxMessagesWaiting` 可以在 `0..maxCount` 之间变化
- 等待者仍然走 queue 的等待链表和 event list 机制

也就是说，`xSemaphoreTake()` 最终调用的其实是：

```text
xQueueSemaphoreTake()
```

当计数为 0 时，等待任务会像等空队列一样被挂到 `xTasksWaitingToReceive`。

## 二值信号量和计数信号量的区别

它们的等待路径基本相同，主要区别在“计数语义”：

- 二值信号量：更像事件标志，只关心“有没有发生过一次”
- 计数信号量：更像资源令牌池，关心“还剩多少个可用资源”

### 二值信号量

适合做：

- 任务同步
- ISR 到任务的事件通知
- 单次事件触发

它没有“持有者”概念，因此：

- 谁 `give` 不必是刚才 `take` 的那个任务
- 内核不会做优先级继承

### 计数信号量

适合做：

- N 个等价资源的分配
- DMA buffer 池
- 连接池/通道池

示例里的资源池就是用 `xSemaphoreCreateCounting(2, 2)` 建出来的，表示最多 2 个资源，初始也有 2 个资源。

## Mutex 为什么不是“二值信号量换个名字”

`Mutex` 也是通过 queue 创建的，但 `xQueueCreateMutex()` 会额外调用初始化逻辑，把它标记为 mutex 类型，并维护持有者信息：

- 队列长度仍然是 1
- item size 仍然是 0
- 但会记录 `xMutexHolder`
- 并在 `take/give` 路径上接入优先级继承/解除继承

这意味着 mutex 和 binary semaphore 虽然外观看起来都像“1 个 token”，但语义不同：

- semaphore 只有计数，没有所有权
- mutex 有所有权，只能由持有者释放
- mutex 才会处理优先级反转

## 优先级反转是怎么发生的

经典场景是三任务：

- `Low`：低优先级，先拿到 mutex
- `High`：高优先级，后来需要同一个 mutex，被阻塞
- `Medium`：中优先级，不关心 mutex，但会抢占 `Low`

如果没有优先级继承，会发生：

```text
Low 持锁
High 阻塞等锁
Medium 一直抢占 Low
High 间接被 Medium 拖住
```

这就是优先级反转。

## FreeRTOS 如何做优先级继承

在 `xQueueSemaphoreTake()` 中，如果发现当前等待的是 mutex，FreeRTOS 会在阻塞前调用：

```text
xTaskPriorityInherit( mutex holder )
```

它做的事情不是“打标记”，而是真正把持锁任务的当前优先级抬高到等待者的优先级，并在必要时把它从旧 ready list 挪到新的高优先级 ready list。

当持锁任务释放 mutex 时，则会走：

```text
xTaskPriorityDisinherit( mutex holder )
```

把优先级恢复回 base priority。示例里你会直接看到 `Low` 的优先级从 1 提升到 3，然后在释放 mutex 后恢复。

## 为什么 semaphore 不解决优先级反转

因为 semaphore 没有 owner。内核不知道“当前是谁在持有这个同步对象”，自然也就无法把某个任务临时提优。

这也是工程里非常重要的一条经验：

- 做事件同步，用 semaphore
- 做共享资源保护，用 mutex

如果用 binary semaphore 去保护共享资源，功能上也许能跑，但你会失去 owner 约束和优先级继承。

## 本课示例怎么看

### Stage A: Binary Semaphore

看三类输出：

- `Waiter wait event...`
- `Giver give event...`
- `Coord/Binary count=... waiter=Blocked/Ready`

这说明：

- token 为 0 时，等待者进入 Blocked
- `give` 后等待者被唤醒
- 这里没有 owner，也没有优先级继承

### Stage B: Counting Semaphore

看资源数和第三个 worker 的状态：

- 前两个 worker 先拿走两个 token
- 第三个 worker 会阻塞
- 直到前面某个 worker 归还 token，第 3 个 worker 才会继续

这个阶段最适合理解“计数信号量是资源池”的含义。

### Stage C: Mutex + Priority Inheritance

重点看：

- `Low` 先拿锁，初始优先级是 1
- `High` 开始等待 mutex 后，`Low` 的当前优先级升到 3
- `Low` 释放 mutex 后，优先级回落到 1

这是本课最核心的内核行为。

## 和源码对照时建议先看哪里

建议按这个顺序：

1. `FreeRTOS/include/semphr.h` 里的 `xSemaphoreTake`/`xSemaphoreGive` 宏
2. `FreeRTOS/queue.c` 里的 `xQueueSemaphoreTake()`
3. `FreeRTOS/queue.c` 里的 `xQueueCreateMutex()`
4. `FreeRTOS/tasks.c` 里的 `xTaskPriorityInherit()`
5. `FreeRTOS/tasks.c` 里的 `xTaskPriorityDisinherit()`

这样最容易把“同步原语 API”一路追到“任务优先级和 ready list 真正被修改的地方”。