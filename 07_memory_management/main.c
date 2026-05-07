/*
 * FreeRTOS 教程 - 第7课: 内存管理 —— heap_4 内核原理演示
 *
 * 本示例深入展示 heap_4 分配器的内部行为：
 *   阶段1: 堆初始化状态 - 观察初始空闲空间
 *   阶段2: First Fit 分配与分裂 - 观察块如何被切割
 *   阶段3: 释放与合并 (Coalesce) - 相邻块自动合并
 *   阶段4: 碎片化场景 - 交错释放制造碎片
 *   阶段5: 合并恢复 - 释放全部后碎片消除
 *   阶段6: 水位线检测 - xPortGetMinimumEverFreeHeapSize
 *
 * 原理说明见 README.md
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

extern void uart_init(void);

/*-----------------------------------------------------------
 * 辅助函数：打印堆状态
 *-----------------------------------------------------------*/
static void prvPrintHeapStatus(const char *pcLabel)
{
    printf("  [Heap] %-30s free=%5u  min_ever=%5u\n",
           pcLabel,
           (unsigned)xPortGetFreeHeapSize(),
           (unsigned)xPortGetMinimumEverFreeHeapSize());
}

/*-----------------------------------------------------------
 * 内存管理原理演示任务
 *-----------------------------------------------------------*/
void vMemoryInternalsDemo(void *pvParameters)
{
    (void)pvParameters;
    size_t xInitialFree;

    /* ============ 阶段1: 堆初始化状态 ============ */
    printf("\n=== Phase 1: Heap Initialization State ===\n");
    printf("  configTOTAL_HEAP_SIZE = %u bytes\n", (unsigned)configTOTAL_HEAP_SIZE);
    printf("  After scheduler + this task created:\n");
    xInitialFree = xPortGetFreeHeapSize();
    prvPrintHeapStatus("Initial state");
    printf("  Overhead = %u bytes (TCBs + stacks + timer task + idle)\n\n",
           (unsigned)(configTOTAL_HEAP_SIZE - xInitialFree));

    /* ============ 阶段2: First Fit 分配与分裂 ============ */
    printf("=== Phase 2: First Fit Allocation & Block Splitting ===\n");
    printf("  heap_4 traverses free list from low addr, picks first fit.\n");
    printf("  If block > request + heapMINIMUM_BLOCK_SIZE, it splits.\n\n");

    void *p1, *p2, *p3, *p4;
    size_t before, after;

    before = xPortGetFreeHeapSize();
    p1 = pvPortMalloc(100);
    after = xPortGetFreeHeapSize();
    printf("  pvPortMalloc(100):  addr=%p  consumed=%u bytes\n",
           p1, (unsigned)(before - after));
    printf("    (100 + BlockLink_t header + alignment → actual block size)\n");

    before = after;
    p2 = pvPortMalloc(200);
    after = xPortGetFreeHeapSize();
    printf("  pvPortMalloc(200):  addr=%p  consumed=%u bytes\n",
           p2, (unsigned)(before - after));

    before = after;
    p3 = pvPortMalloc(64);
    after = xPortGetFreeHeapSize();
    printf("  pvPortMalloc(64):   addr=%p  consumed=%u bytes\n",
           p3, (unsigned)(before - after));

    before = after;
    p4 = pvPortMalloc(500);
    after = xPortGetFreeHeapSize();
    printf("  pvPortMalloc(500):  addr=%p  consumed=%u bytes\n",
           p4, (unsigned)(before - after));

    prvPrintHeapStatus("After 4 allocations");
    printf("  Note: addresses are sequential (first fit from start)\n\n");

    /* ============ 阶段3: 释放与合并 ============ */
    printf("=== Phase 3: Free & Coalesce (Adjacent Block Merging) ===\n");
    printf("  prvInsertBlockIntoFreeList() merges blocks if contiguous.\n\n");

    /* 先释放 p2 (中间块) - 不能与两侧合并因为 p1, p3 还在 */
    before = xPortGetFreeHeapSize();
    vPortFree(p2);
    after = xPortGetFreeHeapSize();
    printf("  vPortFree(p2=200): freed=%u bytes  (isolated, no merge)\n",
           (unsigned)(after - before));
    prvPrintHeapStatus("After free p2");

    /* 释放 p1 (与 p2 的空闲块相邻) → 触发向后合并 */
    before = after;
    vPortFree(p1);
    after = xPortGetFreeHeapSize();
    printf("  vPortFree(p1=100): freed=%u bytes  (merges with p2's free block!)\n",
           (unsigned)(after - before));
    prvPrintHeapStatus("After free p1 (merged)");

    /* 释放 p3 → 与前面合并后的大块再合并 */
    before = after;
    vPortFree(p3);
    after = xPortGetFreeHeapSize();
    printf("  vPortFree(p3=64):  freed=%u bytes  (merges into larger block!)\n",
           (unsigned)(after - before));
    prvPrintHeapStatus("After free p3 (merged)");

    /* 释放 p4 → 全部合并成一个大空闲块 */
    before = after;
    vPortFree(p4);
    after = xPortGetFreeHeapSize();
    printf("  vPortFree(p4=500): freed=%u bytes  (all merged into one block)\n",
           (unsigned)(after - before));
    prvPrintHeapStatus("After free p4 (all merged)");

    printf("  Free heap is back to initial = %s\n\n",
           (xPortGetFreeHeapSize() == xInitialFree) ? "YES (no fragmentation!)" : "NO");

    /* ============ 阶段4: 碎片化场景 ============ */
    printf("=== Phase 4: Fragmentation Scenario ===\n");
    printf("  Allocate 8 blocks, free even-indexed ones → holes.\n");
    printf("  Then try to allocate a large block that doesn't fit in holes.\n\n");

    void *blocks[8];
    int i;
    size_t blockSizes[8] = {128, 256, 128, 256, 128, 256, 128, 256};

    for (i = 0; i < 8; i++)
    {
        blocks[i] = pvPortMalloc(blockSizes[i]);
    }
    prvPrintHeapStatus("After alloc 8 blocks");

    /* 释放偶数索引 (128-byte blocks) → 产生小空洞 */
    for (i = 0; i < 8; i += 2)
    {
        vPortFree(blocks[i]);
        blocks[i] = NULL;
    }
    prvPrintHeapStatus("After free 0,2,4,6 (128B)");

    /* 尝试分配 400 字节 - 每个空洞只有约 128 字节，不够 */
    void *pBig = pvPortMalloc(400);
    printf("  pvPortMalloc(400) in fragmented heap: %s\n",
           pBig ? "SUCCESS (found space after last block)" : "FAILED (no contiguous space)");
    if (pBig)
    {
        printf("    (Succeeded because free space at end of heap is still large)\n");
        vPortFree(pBig);
    }

    /* 尝试一个真正超大的分配 */
    size_t xFreeNow = xPortGetFreeHeapSize();
    /* 请求比当前空闲少一点但比最大连续块大 */
    void *pHuge = pvPortMalloc(xFreeNow - 100);
    printf("  pvPortMalloc(%u) nearly all free: %s\n",
           (unsigned)(xFreeNow - 100),
           pHuge ? "SUCCESS" : "FAILED (fragmented!)");
    if (pHuge) vPortFree(pHuge);
    printf("\n");

    /* ============ 阶段5: 合并恢复 ============ */
    printf("=== Phase 5: Coalesce Recovery ===\n");
    printf("  Free remaining odd-indexed blocks → all merge!\n\n");

    for (i = 1; i < 8; i += 2)
    {
        vPortFree(blocks[i]);
        blocks[i] = NULL;
    }
    prvPrintHeapStatus("After free all blocks");
    printf("  Free heap restored = %s\n",
           (xPortGetFreeHeapSize() == xInitialFree) ? "YES" : "NO");

    /* 现在大分配应该成功 */
    pHuge = pvPortMalloc(xFreeNow - 100);
    printf("  pvPortMalloc(%u) after coalesce: %s\n\n",
           (unsigned)(xFreeNow - 100),
           pHuge ? "SUCCESS! (fragmentation eliminated)" : "FAILED");
    if (pHuge) vPortFree(pHuge);

    /* ============ 阶段6: 水位线与内存规划 ============ */
    printf("=== Phase 6: High Water Mark (Min-Ever-Free) ===\n");
    printf("  xPortGetMinimumEverFreeHeapSize() tracks the lowest\n");
    printf("  free-heap value ever seen → helps size configTOTAL_HEAP_SIZE.\n\n");

    size_t xMinEver = xPortGetMinimumEverFreeHeapSize();
    printf("  Current min-ever-free: %u bytes\n", (unsigned)xMinEver);
    printf("  Peak usage = configTOTAL_HEAP_SIZE - min_ever = %u bytes\n",
           (unsigned)(configTOTAL_HEAP_SIZE - xMinEver));

    /* 制造一个峰值 */
    void *peak[10];
    for (i = 0; i < 10; i++)
    {
        peak[i] = pvPortMalloc(1024);
    }
    printf("  After allocating 10x1024:\n");
    prvPrintHeapStatus("Peak allocation");

    for (i = 0; i < 10; i++)
    {
        if (peak[i]) vPortFree(peak[i]);
    }
    printf("  After freeing all:\n");
    prvPrintHeapStatus("Post-peak (min_ever unchanged!)");
    printf("  min_ever stays at lowest point → use it to tune heap size.\n");

    printf("\n=== All phases complete! ===\n");

    /* 挂起自己，让 QEMU timeout 结束 */
    vTaskSuspend(NULL);
    for (;;) {}
}

int main(void)
{
    uart_init();

    printf("========================================\n");
    printf("  FreeRTOS Lesson 07: Memory Management\n");
    printf("  heap_4 Internals Deep Dive\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("========================================\n");

    xTaskCreate(vMemoryInternalsDemo, "MemDemo", 1024, NULL, 2, NULL);

    vTaskStartScheduler();
    for (;;);
    return 0;
}
