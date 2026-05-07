# 第7课：内存管理 —— 堆分配器内核原理

## 概述

FreeRTOS 提供 5 种堆分配方案 (heap_1 ~ heap_5)，各适用于不同场景。
本课深入剖析 heap_4 (本教程使用的方案) 的数据结构和算法，并对比其他方案。

---

## 1. 五种堆方案对比

| 方案 | 分配 | 释放 | 合并 | 适用场景 |
|------|------|------|------|----------|
| heap_1 | ✓ | ✗ | — | 永不释放的安全关键系统 |
| heap_2 | ✓ | ✓ | ✗ | 固定大小块反复分配/释放 |
| heap_3 | ✓ | ✓ | 取决于库 | 包装标准 malloc/free + 线程安全 |
| heap_4 | ✓ | ✓ | ✓ | **通用推荐**，合并减少碎片 |
| heap_5 | ✓ | ✓ | ✓ | 多个不连续内存区域 |

---

## 2. heap_4 核心数据结构

### BlockLink_t 块头结构

```c
typedef struct A_BLOCK_LINK
{
    struct A_BLOCK_LINK *pxNextFreeBlock;  // 指向下一个空闲块
    size_t xBlockSize;                     // 本块大小 (含块头)
} BlockLink_t;
```

每个内存块（无论已分配还是空闲）前面都有一个 `BlockLink_t` 头部。
对齐后大小 `xHeapStructSize = ALIGN_UP(sizeof(BlockLink_t), portBYTE_ALIGNMENT)`。

### 分配位标记

```c
// xBlockSize 的最高位 (MSB) 用于标记分配状态：
#define heapBLOCK_ALLOCATED_BITMASK  ( 1 << (sizeof(size_t)*8 - 1) )

heapALLOCATE_BLOCK(pxBlock)  // 设置 MSB → 已分配
heapFREE_BLOCK(pxBlock)      // 清除 MSB → 空闲
```

### 空闲链表哨兵

```c
static BlockLink_t xStart;    // 链表头哨兵 (xBlockSize=0)
static BlockLink_t *pxEnd;    // 链表尾哨兵 (xBlockSize=0, 位于堆末尾)
```

空闲块按**内存地址升序**链接，这是合并相邻块的关键。

---

## 3. 堆初始化 (prvHeapInit)

```
ucHeap[configTOTAL_HEAP_SIZE]:
┌──────────────────────────────────────────────┐
│ [对齐填充] [pxFirstFreeBlock (整个堆)] [pxEnd] │
└──────────────────────────────────────────────┘

初始状态：
xStart.pxNextFreeBlock → pxFirstFreeBlock → pxEnd → NULL
xStart.xBlockSize = 0
pxEnd->xBlockSize = 0
pxFirstFreeBlock->xBlockSize = 全部可用空间
```

---

## 4. 分配算法 (pvPortMalloc) — First Fit

```c
void *pvPortMalloc( size_t xWantedSize )
{
    // 1. 调整请求大小 (加 BlockLink_t 头 + 对齐)
    xWantedSize += xHeapStructSize;
    xWantedSize = ALIGN_UP(xWantedSize);

    vTaskSuspendAll();  // 临界区 (禁止调度)
    {
        // 2. 首次适配: 从链表头遍历，找到第一个足够大的空闲块
        pxPreviousBlock = &xStart;
        pxBlock = xStart.pxNextFreeBlock;
        while( pxBlock->xBlockSize < xWantedSize )
        {
            pxPreviousBlock = pxBlock;
            pxBlock = pxBlock->pxNextFreeBlock;
        }
        
        // 3. 从空闲链表摘除
        pxPreviousBlock->pxNextFreeBlock = pxBlock->pxNextFreeBlock;
        
        // 4. 如果剩余空间 > heapMINIMUM_BLOCK_SIZE，则分裂
        if( (pxBlock->xBlockSize - xWantedSize) > heapMINIMUM_BLOCK_SIZE )
        {
            pxNewBlock = (uint8_t*)pxBlock + xWantedSize;
            pxNewBlock->xBlockSize = pxBlock->xBlockSize - xWantedSize;
            pxBlock->xBlockSize = xWantedSize;
            // 将新空闲块插回链表（保持地址序）
            pxNewBlock->pxNextFreeBlock = pxPreviousBlock->pxNextFreeBlock;
            pxPreviousBlock->pxNextFreeBlock = pxNewBlock;
        }
        
        // 5. 标记为已分配
        heapALLOCATE_BLOCK( pxBlock );
        pxBlock->pxNextFreeBlock = NULL;
        
        // 6. 返回块头之后的用户地址
        pvReturn = (uint8_t*)pxBlock + xHeapStructSize;
    }
    xTaskResumeAll();
    return pvReturn;
}
```

### 图解分配过程

```
Before: xStart → [Free: 4096] → pxEnd
Request: 100 bytes (实际需要 100+8=108, 对齐后=112)

After:  xStart → [Free: 3984] → pxEnd
        [Allocated: 112 | user data... ]
         ↑ pvReturn 指向此处 (跳过 BlockLink_t)
```

---

## 5. 释放算法 (vPortFree)

```c
void vPortFree( void *pv )
{
    // 1. 从用户指针回退到 BlockLink_t 头
    pxLink = (uint8_t*)pv - xHeapStructSize;
    
    // 2. 验证确实是已分配块
    configASSERT( heapBLOCK_IS_ALLOCATED(pxLink) );
    
    // 3. 清除分配标记
    heapFREE_BLOCK( pxLink );
    
    vTaskSuspendAll();
    {
        xFreeBytesRemaining += pxLink->xBlockSize;
        // 4. 插入空闲链表并尝试合并
        prvInsertBlockIntoFreeList( pxLink );
    }
    xTaskResumeAll();
}
```

---

## 6. 合并算法 (prvInsertBlockIntoFreeList) — heap_4 的核心

```c
static void prvInsertBlockIntoFreeList( BlockLink_t *pxBlockToInsert )
{
    // 1. 找到插入位置 (按地址升序)
    for( pxIterator = &xStart;
         pxIterator->pxNextFreeBlock < pxBlockToInsert;
         pxIterator = pxIterator->pxNextFreeBlock ) {}
    
    // 2. 尝试与前一个块合并 (向前合并)
    if( (uint8_t*)pxIterator + pxIterator->xBlockSize 
        == (uint8_t*)pxBlockToInsert )
    {
        // 地址连续! 合并
        pxIterator->xBlockSize += pxBlockToInsert->xBlockSize;
        pxBlockToInsert = pxIterator;  // 合并后以前块为主
    }
    
    // 3. 尝试与后一个块合并 (向后合并)
    if( (uint8_t*)pxBlockToInsert + pxBlockToInsert->xBlockSize
        == (uint8_t*)pxIterator->pxNextFreeBlock )
    {
        // 与后块地址连续! 合并
        pxBlockToInsert->xBlockSize += pxIterator->pxNextFreeBlock->xBlockSize;
        pxBlockToInsert->pxNextFreeBlock = 
            pxIterator->pxNextFreeBlock->pxNextFreeBlock;
    }
    
    // 4. 更新链表指针
    if( pxIterator != pxBlockToInsert )
    {
        pxIterator->pxNextFreeBlock = pxBlockToInsert;
    }
}
```

### 合并图解

```
Before free(B):
  xStart → [Free A: 200] → [Free C: 300] → pxEnd
  [...] [Alloc B: 100] [...]
         ↑ 释放这个

Step 1: 找到位置 (A < B < C，按地址)
Step 2: A 的末尾 == B 的起始? → 是! A 吞并 B → [Free A: 300]
Step 3: 新A 的末尾 == C 的起始? → 是! A 吞并 C → [Free A: 600]

After:
  xStart → [Free A: 600] → pxEnd
  (三块合并成一块大块!)
```

---

## 7. heap_4 vs heap_2 的关键区别

**heap_2** 不按地址排序，而是按块大小排序（Best Fit）：
- 优点：找到最合适大小的块更快
- 缺点：**不能合并相邻块** → 碎片永远不会减少

**heap_4** 按地址排序（First Fit + Coalesce）：
- 优点：释放时自动合并相邻块 → 碎片化可恢复
- 缺点：分配时需遍历链表找到足够大的块

---

## 8. 线程安全保证

```c
pvPortMalloc():  vTaskSuspendAll() ... xTaskResumeAll()
vPortFree():     vTaskSuspendAll() ... xTaskResumeAll()
```

使用调度器挂起（而非关中断）保护堆操作：
- ISR 中不能调用 pvPortMalloc / vPortFree
- 多任务并发分配是安全的

---

## 9. 本课演示程序说明

`main.c` 演示以下内核行为：

1. **堆初始化状态** — 观察 xFreeBytesRemaining 初始值
2. **分配与分裂** — 观察 First Fit 找到块并分裂
3. **释放与合并** — 释放相邻块观察 xFreeBytesRemaining 恢复
4. **碎片化场景** — 交错分配/释放制造碎片
5. **合并恢复** — 释放所有块后碎片消除，大块分配成功
6. **heap_2 行为模拟** — 对比不合并时的碎片不可恢复性
7. **最小历史空闲水位** — xPortGetMinimumEverFreeHeapSize 用于检测堆是否够用
