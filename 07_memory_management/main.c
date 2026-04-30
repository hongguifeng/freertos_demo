/*
 * FreeRTOS 教程 - 第7课: 内存管理
 *
 * 知识点:
 * 1. FreeRTOS 内存管理方案 (heap_1 ~ heap_5)
 *    - heap_1: 只分配不释放 (最简单, 确定性最强)
 *    - heap_2: 允许释放但不合并 (可能碎片化)
 *    - heap_3: 包装标准 malloc/free (线程安全)
 *    - heap_4: 合并相邻空闲块 (推荐, 本教程使用)
 *    - heap_5: 跨多个不连续内存区域
 * 2. pvPortMalloc() / vPortFree() - FreeRTOS内存分配接口
 * 3. xPortGetFreeHeapSize() - 查询剩余堆空间
 * 4. xPortGetMinimumEverFreeHeapSize() - 历史最小剩余堆空间
 * 5. vApplicationMallocFailedHook() - 分配失败钩子
 *
 * 本课演示 heap_4 的动态分配和释放
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

extern void uart_init(void);

/*-----------------------------------------------------------
 * 内存管理演示任务
 *-----------------------------------------------------------*/
void vMemoryDemo(void *pvParameters)
{
    (void)pvParameters;
    size_t freeHeap, minFreeHeap;
    void *ptr1, *ptr2, *ptr3;

    printf("--- Heap Status at Start ---\n");
    freeHeap = xPortGetFreeHeapSize();
    minFreeHeap = xPortGetMinimumEverFreeHeapSize();
    printf("[Mem] Total configured heap: %u bytes\n", configTOTAL_HEAP_SIZE);
    printf("[Mem] Free heap: %u bytes\n", (unsigned)freeHeap);
    printf("[Mem] Min ever free: %u bytes\n\n", (unsigned)minFreeHeap);

    /* 分配内存 */
    printf("--- Allocating Memory ---\n");
    ptr1 = pvPortMalloc(1024);
    printf("[Mem] Allocated 1024 bytes at %p, free: %u\n",
           ptr1, (unsigned)xPortGetFreeHeapSize());

    ptr2 = pvPortMalloc(2048);
    printf("[Mem] Allocated 2048 bytes at %p, free: %u\n",
           ptr2, (unsigned)xPortGetFreeHeapSize());

    ptr3 = pvPortMalloc(512);
    printf("[Mem] Allocated  512 bytes at %p, free: %u\n\n",
           ptr3, (unsigned)xPortGetFreeHeapSize());

    /* 使用分配的内存 */
    if (ptr1) memset(ptr1, 0xAA, 1024);
    if (ptr2) memset(ptr2, 0xBB, 2048);
    if (ptr3) memset(ptr3, 0xCC, 512);

    /* 释放内存 (heap_4 支持释放和合并) */
    printf("--- Freeing Memory ---\n");
    vPortFree(ptr2);  /* 先释放中间块 */
    printf("[Mem] Freed ptr2(2048), free: %u\n",
           (unsigned)xPortGetFreeHeapSize());

    vPortFree(ptr1);  /* 释放第一块 */
    printf("[Mem] Freed ptr1(1024), free: %u\n",
           (unsigned)xPortGetFreeHeapSize());

    vPortFree(ptr3);  /* 释放最后一块 */
    printf("[Mem] Freed ptr3(512),  free: %u\n\n",
           (unsigned)xPortGetFreeHeapSize());

    /* heap_4 合并演示 */
    printf("--- Fragmentation Test (heap_4 merges adjacent free blocks) ---\n");
    void *blocks[8];
    int i;

    /* 分配8个256字节块 */
    for (i = 0; i < 8; i++) {
        blocks[i] = pvPortMalloc(256);
    }
    printf("[Mem] Allocated 8 x 256 bytes, free: %u\n",
           (unsigned)xPortGetFreeHeapSize());

    /* 释放偶数索引块 (形成碎片) */
    for (i = 0; i < 8; i += 2) {
        vPortFree(blocks[i]);
    }
    printf("[Mem] Freed blocks 0,2,4,6, free: %u\n",
           (unsigned)xPortGetFreeHeapSize());

    /* 释放奇数索引块 (heap_4 会合并相邻块) */
    for (i = 1; i < 8; i += 2) {
        vPortFree(blocks[i]);
    }
    printf("[Mem] Freed blocks 1,3,5,7, free: %u (merged!)\n",
           (unsigned)xPortGetFreeHeapSize());

    /* 尝试分配大块 (如果合并成功，应该可以分配) */
    void *big = pvPortMalloc(2000);
    printf("[Mem] Alloc 2000 bytes after merge: %s\n\n",
           big ? "SUCCESS" : "FAILED");
    if (big) vPortFree(big);

    /* 最终状态 */
    printf("--- Final Heap Status ---\n");
    printf("[Mem] Free heap: %u bytes\n", (unsigned)xPortGetFreeHeapSize());
    printf("[Mem] Min ever free: %u bytes\n",
           (unsigned)xPortGetMinimumEverFreeHeapSize());

    printf("\n[Mem] Demo complete!\n");
    vTaskEndScheduler();
    for(;;);
}

int main(void)
{
    uart_init();

    printf("========================================\n");
    printf("  FreeRTOS Lesson 07: Memory Management\n");
    printf("  Platform: QEMU MPS2-AN385 (Cortex-M3)\n");
    printf("========================================\n\n");

    xTaskCreate(vMemoryDemo, "MemDemo", 512, NULL, 1, NULL);

    vTaskStartScheduler();
    for (;;);
    return 0;
}
