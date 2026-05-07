# 第6课：事件组与多条件等待原理

事件组和 queue/semaphore 最大的区别，不是“一个有 bit，一个有消息”，而是等待条件的表达能力完全不同。

queue/semaphore 的等待条件很单一：

- 队列非空
- 队列非满
- semaphore count > 0

事件组则允许任务表达更复杂的条件：

- 等任意一位
- 等全部位
- 返回时是否自动清位
- 多任务同时等待不同 bit mask

正因为等待条件不再是单一的“有/没有”，FreeRTOS 在事件组里不再使用按优先级排序的 event list，而是改成 unordered event list，再由 `xEventGroupSetBits()` 遍历整张等待链表逐个匹配。

## 本课目标

- 理解事件组为什么需要 unordered event list
- 理解 `xEventGroupWaitBits()` 的 ANY/ALL 和 clear-on-exit 语义
- 理解 `xEventGroupSync()` 为什么本质上是“原子 set + wait-all + clear-on-exit”
- 通过示例观察多个等待者如何因不同 bit 条件被分别唤醒

## 运行方式

```bash
cd 06_event_groups
make clean
make
make run
```

示例分成两个阶段：

- Stage A: `xEventGroupWaitBits()`，同时演示 ANY、ALL、clear-on-exit
- Stage B: `xEventGroupSync()`，演示三任务 rendezvous

## 为什么事件组不用优先级排序的 event list

在 queue/semaphore 中，等待条件很简单，因此事件到来时通常只需要唤醒“最高优先级的那个等待者”。

但事件组不一样。假设同时有三个任务：

- Task1 等 `BIT_A`
- Task2 等 `BIT_B | BIT_C` 的任意一位
- Task3 等 `BIT_A | BIT_C` 的全部位

这时你无法只看“谁优先级最高”就知道该唤醒谁，因为要不要唤醒取决于每个任务自己的 bit mask 和等待模式。

因此 FreeRTOS 采用了另一种方式：

1. 把“这个任务在等哪些 bit，以及是否 `wait-for-all` / `clear-on-exit`”编码到任务的 event list item 里。
2. 把等待任务按插入顺序放进 unordered event list。
3. `xEventGroupSetBits()` 设置新 bit 后，遍历整张等待链表逐个匹配。

这就是为什么源码里调用的是：

```text
vTaskPlaceOnUnorderedEventList()
```

而不是普通 queue/semaphore 使用的 `vTaskPlaceOnEventList()`。

## `xEventGroupWaitBits()` 的内部思路

主线可以概括成：

```text
先检查当前 bit 是否已经满足条件
  |- 满足: 直接返回
  |- 不满足且不允许等待: 直接返回当前 bits
  |- 不满足且允许等待:
       把 wait mask + control bits 存入任务 event item
       进入 unordered event list
       同时进入 delayed list 等待超时
```

这里的 control bits 不是用户 bit，而是内核保留位，用来记录：

- `eventCLEAR_EVENTS_ON_EXIT_BIT`
- `eventWAIT_FOR_ALL_BITS`

这意味着 task 被唤醒后，内核仍然知道这次等待是：

- ANY 还是 ALL
- 返回时要不要清位

## `xEventGroupSetBits()` 为什么可能唤醒多个任务

queue/semaphore 通常一个事件只会释放一个关键资源，因此经常只唤醒一个等待任务。

事件组不一样。同一次 `set bits` 可能同时满足多个任务的等待条件，因此 `xEventGroupSetBits()` 会：

1. 先把新 bit OR 进当前 event bits。
2. 遍历等待链表中的每个任务。
3. 对每个任务单独判断它的等待条件是否已满足。
4. 对满足条件的任务调用 `vTaskRemoveFromUnorderedEventList()`。
5. 如果任务要求 `clear-on-exit`，则记录待清除的 bit，遍历结束后统一清除。

这就是事件组能表达“一次事件同时唤醒多个等待者”的原因。

## clear-on-exit 为什么要延后统一处理

如果在遍历中立刻改写 event bits，后面的等待条件匹配结果就会受到前面任务的影响，行为会变得不稳定。

因此 FreeRTOS 先收集：

```text
哪些 bit 需要 clear
```

最后再统一清掉。这是事件组实现里很关键的一点。

## `xEventGroupSync()` 的本质

`xEventGroupSync()` 可以理解为：

```text
先声明“我到了”
再等待“所有人都到了”
最后自动清除 rendezvous bits
```

在源码里它本质上就是一个原子操作：

- 先 `set bits`
- 再检查 `wait-for-all`
- 如果还没满足就阻塞
- 当全部任务到齐后自动清除同步位

这比“先 set，再单独 wait”更安全，因为中间不会丢掉 rendezvous 条件。

## 本课示例怎么看

### Stage A: WaitBits

有两个等待任务：

- `WaitAny`：等 `NET` 或 `STORAGE` 任意一位，不清位
- `WaitAll`：等 `STORAGE | SENSOR` 全部位，返回时清位

然后 `Setter` 分三次设置：

- `NET`
- `STORAGE`
- `SENSOR`

你会看到：

- `WaitAny` 在 `NET` 设置后提前返回
- `WaitAll` 一直等到 `STORAGE` 和 `SENSOR` 都设置完成才返回
- 因为 `WaitAll` 选择了 clear-on-exit，所以最终 event bits 会把 `STORAGE` 和 `SENSOR` 清掉，只留下 `NET`

### Stage B: Sync

三个任务分别在不同时间点到达 barrier，调用 `xEventGroupSync()`。

你会看到：

- 前两个到达者会阻塞
- 最后一个到达者会让全部同步位满足
- 三个任务一起通过 barrier
- 同步位被自动清空，便于下一轮 rendezvous 再次使用

## 为什么 `xEventGroupSetBitsFromISR()` 不能像 queue give 那样直接做完

因为事件组设置 bit 时可能需要遍历整张等待链表，并且一次唤醒多个任务，这个工作量和路径都比简单 queue/semaphore 更重。

因此 ISR 版本通常不会在中断里直接执行完整逻辑，而是把工作延后到任务上下文处理。

## 和源码对照时建议先看哪里

建议按这个顺序：

1. `FreeRTOS/event_groups.c` 中的 `xEventGroupWaitBits()`
2. `FreeRTOS/tasks.c` 中的 `vTaskPlaceOnUnorderedEventList()`
3. `FreeRTOS/event_groups.c` 中的 `xEventGroupSetBits()`
4. `FreeRTOS/tasks.c` 中的 `vTaskRemoveFromUnorderedEventList()`
5. `FreeRTOS/event_groups.c` 中的 `xEventGroupSync()`

这样最容易看清“等待条件编码 -> 链表阻塞 -> 遍历匹配 -> 多任务唤醒”的整条链路。