# 第2课：任务管理 —— 内核调度原理

## 概述

本课深入剖析 FreeRTOS 任务调度的核心实现：就绪列表（Ready List）、最高优先级选择、
时间片轮转（Time Slicing）、以及任务挂起/恢复/删除的内部机制。

---

## 1. 就绪列表 (pxReadyTasksLists)

### 数据结构

```c
/* tasks.c */
PRIVILEGED_DATA static List_t pxReadyTasksLists[ configMAX_PRIORITIES ];
```

这是一个链表数组，每个优先级对应一条链表。所有处于 Ready 状态的任务 TCB 
通过 `xStateListItem` 挂接在其优先级对应的链表上。

```
pxReadyTasksLists[0] → [IdleTask]
pxReadyTasksLists[1] → [TaskA] ↔ [TaskB] ↔ [TaskC]   (同优先级轮转)
pxReadyTasksLists[2] → [TaskD]
...
pxReadyTasksLists[configMAX_PRIORITIES-1] → (空)
```

### 入列宏 prvAddTaskToReadyList

```c
#define prvAddTaskToReadyList( pxTCB )                                           \
    taskRECORD_READY_PRIORITY( (pxTCB)->uxPriority );                            \
    listINSERT_END( &(pxReadyTasksLists[(pxTCB)->uxPriority]),                   \
                    &((pxTCB)->xStateListItem) );
```

- `taskRECORD_READY_PRIORITY` 更新 `uxTopReadyPriority`（记录当前最高就绪优先级）
- `listINSERT_END` 将 TCB 插入链表**尾部**（保证 FIFO 顺序）

---

## 2. 最高优先级任务选择

### 通用方法 (configUSE_PORT_OPTIMISED_TASK_SELECTION == 0)

```c
#define taskSELECT_HIGHEST_PRIORITY_TASK()                               \
    UBaseType_t uxTopPriority = uxTopReadyPriority;                      \
    while( listLIST_IS_EMPTY(&(pxReadyTasksLists[uxTopPriority])) )      \
    {                                                                    \
        --uxTopPriority;  /* 从高向低扫描 */                              \
    }                                                                    \
    listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB,                           \
                                 &(pxReadyTasksLists[uxTopPriority]) );   \
    uxTopReadyPriority = uxTopPriority;
```

**关键点**：
1. 从 `uxTopReadyPriority` 开始向下扫描，找到第一个非空链表
2. `listGET_OWNER_OF_NEXT_ENTRY` 推进 `pxIndex` 指针到下一个节点
   → 同优先级多个任务时实现**轮转**

### 硬件优化方法 (Cortex-M CLZ 指令)

```c
portGET_HIGHEST_PRIORITY( uxTopPriority, uxTopReadyPriority );
// 使用 __clz() (Count Leading Zeros) 在 O(1) 时间找到最高位
```

`uxTopReadyPriority` 被当作位图使用，每一位代表对应优先级是否有就绪任务。

---

## 3. 时间片轮转 (Time Slicing)

### SysTick 中断中的判定

在 `xTaskIncrementTick()` 中：

```c
#if ( configUSE_PREEMPTION == 1 ) && ( configUSE_TIME_SLICING == 1 )
    if( listCURRENT_LIST_LENGTH(
            &(pxReadyTasksLists[pxCurrentTCB->uxPriority]) ) > 1U )
    {
        xSwitchRequired = pdTRUE;  // 触发上下文切换
    }
#endif
```

**原理**：如果当前任务所在优先级的就绪链表中有超过 1 个任务，
则每个 tick 都会触发一次上下文切换，`listGET_OWNER_OF_NEXT_ENTRY` 
会自动选择链表中的下一个任务 → 实现 Round-Robin。

### 轮转过程图解

```
Tick N:   pxIndex → TaskA → TaskB → TaskC → (end marker) → TaskA
          pxCurrentTCB = TaskA

Tick N+1: pxIndex → TaskB → TaskC → TaskA → (end marker) → TaskB
          pxCurrentTCB = TaskB

Tick N+2: pxIndex → TaskC → TaskA → TaskB → (end marker) → TaskC
          pxCurrentTCB = TaskC
```

每个任务获得 1 tick (= 1ms @ configTICK_RATE_HZ=1000) 的 CPU 时间。

---

## 4. 任务挂起 (vTaskSuspend) 内部实现

```c
void vTaskSuspend( TaskHandle_t xTaskToSuspend )
{
    taskENTER_CRITICAL();
    {
        pxTCB = prvGetTCBFromHandle( xTaskToSuspend );
        
        // 1. 从当前所在链表中移除（可能是 Ready 或 Delayed）
        if( uxListRemove(&(pxTCB->xStateListItem)) == 0 )
        {
            taskRESET_READY_PRIORITY( pxTCB->uxPriority );
            // 如果该优先级链表为空，重置优先级位图
        }
        
        // 2. 如果在等待事件，也从事件列表移除
        if( listLIST_ITEM_CONTAINER(&(pxTCB->xEventListItem)) != NULL )
        {
            uxListRemove( &(pxTCB->xEventListItem) );
        }
        
        // 3. 放入挂起列表
        vListInsertEnd( &xSuspendedTaskList, &(pxTCB->xStateListItem) );
    }
    taskEXIT_CRITICAL();
    
    // 4. 如果挂起的是当前任务，立即触发调度
    if( pxTCB == pxCurrentTCB ) { portYIELD_WITHIN_API(); }
}
```

---

## 5. 任务恢复 (vTaskResume) 内部实现

```c
void vTaskResume( TaskHandle_t xTaskToResume )
{
    TCB_t * const pxTCB = xTaskToResume;
    
    taskENTER_CRITICAL();
    {
        if( prvTaskIsTaskSuspended(pxTCB) != pdFALSE )
        {
            // 1. 从 xSuspendedTaskList 移除
            uxListRemove( &(pxTCB->xStateListItem) );
            
            // 2. 重新加入就绪列表
            prvAddTaskToReadyList( pxTCB );
            
            // 3. 如果恢复的任务优先级更高，触发抢占
            taskYIELD_ANY_CORE_IF_USING_PREEMPTION( pxTCB );
        }
    }
    taskEXIT_CRITICAL();
}
```

---

## 6. 任务删除 (vTaskDelete) 与 Idle 回收

### 删除流程

```c
void vTaskDelete( TaskHandle_t xTaskToDelete )
{
    // 1. 从 Ready/Delayed 列表移除
    uxListRemove( &(pxTCB->xStateListItem) );
    taskRESET_READY_PRIORITY( pxTCB->uxPriority );
    
    // 2. 从事件列表移除（如果有）
    uxListRemove( &(pxTCB->xEventListItem) );
    
    // 3. 如果删除的是正在运行的任务（自删除）：
    //    不能立即释放内存（栈还在使用），放入终止等待列表
    vListInsertEnd( &xTasksWaitingTermination, &(pxTCB->xStateListItem) );
    ++uxDeletedTasksWaitingCleanUp;
}
```

### Idle 任务回收

```c
// 在 Idle 任务主循环中调用：
static void prvCheckTasksWaitingTermination( void )
{
    while( uxDeletedTasksWaitingCleanUp > 0 )
    {
        pxTCB = listGET_OWNER_OF_HEAD_ENTRY( &xTasksWaitingTermination );
        uxListRemove( &(pxTCB->xStateListItem) );
        --uxCurrentNumberOfTasks;
        --uxDeletedTasksWaitingCleanUp;
        
        prvDeleteTCB( pxTCB );  // 释放 TCB + 栈内存
    }
}
```

**为什么不立即释放？**  
删除自身时，CPU 的 SP 仍指向该任务的栈，必须先切换到其他任务（Idle），
再由 Idle 安全地释放内存。

---

## 7. 动态优先级修改 (vTaskPrioritySet)

```c
// 简化流程：
1. 如果新优先级与旧优先级不同：
2.   如果目标任务在就绪列表中：
       uxListRemove() 从旧优先级链表移除
       taskRESET_READY_PRIORITY() 如果旧链表为空
       pxTCB->uxPriority = uxNewPriority
       prvAddTaskToReadyList( pxTCB )  // 插入新优先级链表
3.   如果新优先级 > 当前运行任务优先级 → 触发抢占
```

---

## 8. 本课演示程序说明

`main.c` 演示以下内核行为：

1. **时间片轮转可视化**：3 个同优先级任务，观察每 tick 的切换
2. **优先级抢占**：高优先级任务就绪后立即抢占
3. **挂起/恢复的就绪列表变化**：观察任务从 Ready → Suspended → Ready 的迁移
4. **动态优先级修改**：观察任务在不同优先级链表间的移动
5. **任务删除与 Idle 回收**：观察 uxDeletedTasksWaitingCleanUp 计数
