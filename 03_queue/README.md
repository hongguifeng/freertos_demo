# 第3课：队列阻塞与事件链表原理

这一课补的是队列“为什么能等”，不是“怎么调 API”。`xQueueReceive()` 和 `xQueueSend()` 的阻塞能力，本质上来自两套等待链表：

- `xTasksWaitingToReceive`
- `xTasksWaitingToSend`

它们位于 `queue.c` 的队列控制块里，分别保存“因为队列空而等数据”的任务，以及“因为队列满而等空间”的任务。

## 本课目标

- 看懂队列等待不是轮询，而是任务挂到 event list 上
- 理解空队列接收阻塞和满队列发送阻塞的两条路径
- 理解为什么等待队列的任务是按优先级唤醒，而不是按等待顺序唤醒
- 通过可执行示例观察 `Blocked -> Ready -> Running` 的事件驱动切换

## 运行方式

```bash
cd 03_queue
make clean
make
make run
```

示例包含三个任务：

- `Consumer`：优先级 2，启动后立刻对空队列调用 `xQueueReceive(portMAX_DELAY)`
- `Producer`：优先级 1，先延时 350ms，再高速发送消息，使队列在后半段变满
- `Monitor`：优先级 3，每 100ms 采样队列深度以及两个任务状态

这个编排故意制造两个场景：

1. `Consumer` 因为空队列而阻塞在 `xTasksWaitingToReceive`
2. `Producer` 因为满队列而阻塞在 `xTasksWaitingToSend`

## 队列等待的数据结构

在 `queue.c` 中，队列对象除了存储消息缓冲区，还带两张等待链表：

```text
Queue_t
  |- circular buffer / write pointer / read pointer
  |- uxMessagesWaiting
  |- xTasksWaitingToSend
  |- xTasksWaitingToReceive
```

这点很关键：

- 消息本身放在队列缓冲区里
- 等待中的任务不在缓冲区里，而是在 event list 里

所以“队列满/空”是数据面状态，“任务等待/唤醒”是调度面状态，两者是配套但分离的。

## `xQueueReceive()` 为空时怎么阻塞

`xQueueReceive()` 的主线可以概括成：

```text
检查 uxMessagesWaiting
  |- > 0: 直接拷贝数据并返回
  |- == 0 且 xTicksToWait == 0: 立刻返回 errQUEUE_EMPTY
  |- == 0 且允许等待:
       vTaskPlaceOnEventList(&xTasksWaitingToReceive, xTicksToWait)
       -> 当前任务进入 Blocked
```

其中真正把任务挂进等待队列的是：

```text
vTaskPlaceOnEventList()
  -> 把 xEventListItem 插入指定 event list
  -> 调用 prvAddCurrentTaskToDelayedList()
```

也就是说，等待队列对象时，任务会同时具备两层关联：

- 在队列的 `xTasksWaitingToReceive` 中，表示“我在等这个事件”
- 在任务延时/阻塞链表中，表示“我最多还能等多久”

这也是为什么 FreeRTOS 同时支持“等到事件来”与“等到超时”为止。

## `xQueueSend()` 队列满时怎么阻塞

发送路径与接收路径是镜像关系：

```text
检查队列是否已满
  |- 未满: 拷贝数据到缓冲区
  |- 已满且 xTicksToWait == 0: 返回 errQUEUE_FULL
  |- 已满且允许等待:
       vTaskPlaceOnEventList(&xTasksWaitingToSend, xTicksToWait)
```

也就是说：

- 等数据的消费者进 `xTasksWaitingToReceive`
- 等空间的生产者进 `xTasksWaitingToSend`

很多教程只画“生产者-消费者”框图，却没点破这一层。真正决定谁睡眠、谁被唤醒的，是这两张等待链表。

## 为什么等待队列的任务按优先级唤醒

`vTaskPlaceOnEventList()` 在 `tasks.c` 里有一条很关键的注释：event list 不是 FIFO，而是按优先级排序。

原因很直接：当队列从“空变非空”或“满变非满”时，内核希望先唤醒最高优先级的等待者。

对应的唤醒路径是：

```text
xQueueSend()
  -> 队列得到新数据
  -> xTaskRemoveFromEventList(&xTasksWaitingToReceive)

xQueueReceive()
  -> 队列释放出空间
  -> xTaskRemoveFromEventList(&xTasksWaitingToSend)
```

`xTaskRemoveFromEventList()` 会直接取 event list 头节点，而头节点就是当前优先级最高的等待任务。

因此等待队列对象时，FreeRTOS 默认实现的是：

```text
优先级优先
```

而不是：

```text
谁先等谁先醒
```

## `xPendingReadyList` 在这里扮演什么角色

`queue.c` 的发送/接收路径中，会先 `vTaskSuspendAll()`，再给队列加锁。

这样做是为了避免在修改队列结构和等待链表时被并发打断。但这又带来一个问题：如果这时某个事件已经发生，等待任务该放回哪里？

答案是先放到 `xPendingReadyList`。

这条链路在代码里的含义是：

- 调度器暂停期间，不直接碰 ready list
- 先把刚被唤醒的任务挂到 `xPendingReadyList`
- 等 `xTaskResumeAll()` 时再统一并回 ready list

这就是 FreeRTOS 在“逻辑上唤醒了任务”和“调度器真正可安全重排 ready list”之间插入的一层缓冲。

## 本课示例怎么看

运行后重点看三段现象：

1. 初始 350ms 内，`Consumer` 会一直显示为 `Blocked`，且队列深度为 0。
2. 随后 `Producer` 连续发送，`Consumer` 因处理慢而延时，队列逐渐被填满。
3. 队列满后，`Producer` 的一次 `xQueueSend()` 会明显花掉数百个 tick，说明它已经不再只是“发消息”，而是进入了等待空间的阻塞路径。

你会在输出中看到：

- `Consumer wait receive at tick=...`
- `Consumer got seq=... waited=...`
- `Producer seq=... send done ... waited=...`

其中 `waited` 不是“业务处理时间”，而是这次 API 调用从进入到返回跨过了多少 tick。只要这个值明显大于 0，就说明这次调用触发了阻塞/唤醒链路。

## 从源码切入建议

建议按这个顺序对照源码：

1. `FreeRTOS/queue.c` 中的 `xQueueReceive()`
2. `FreeRTOS/queue.c` 中的 `xQueueGenericSend()`
3. `FreeRTOS/tasks.c` 中的 `vTaskPlaceOnEventList()`
4. `FreeRTOS/tasks.c` 中的 `xTaskRemoveFromEventList()`

按这个顺序看，最容易把“队列对象内部等待链表”和“任务调度器里的就绪/阻塞迁移”串起来。