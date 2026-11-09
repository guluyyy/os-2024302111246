/*
 * Lab3 物理页分配器。
 * 位图让单页回收保持 O(1)，避免 memcap 批量回收时出现 O(n^2)。
 */
#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "course_sid.h"
#include "defs.h"

extern char kernel_end[]; /* 链接脚本导出的内核镜像末尾。 */

#define RAM_PAGE_COUNT ((PHYSTOP - KERNBASE) / PGSIZE)
#define BITMAP_BYTES   ((RAM_PAGE_COUNT + 7) / 8)

static uchar free_bitmap[BITMAP_BYTES]; /* 1 表示页面空闲。 */
static int first_page;                  /* 首个可托管页下标。 */
static int limit_page;                  /* 配额上限的后一页下标。 */
static int alloc_hint;                  /* 下一次扫描起点。 */
static uint64 free_pages;               /* 精确空闲页计数。 */

/* 函数作用：读取位图中的单页空闲标志。 */
static int /* 声明下方函数的返回类型与可见范围。 */
bitmap_get(int index) /* 函数作用：读取指定物理页在空闲位图中的状态。 */
{ /* 开始当前结构或代码块。 */
  return (free_bitmap[index >> 3] >> (index & 7)) & 1; /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：把指定物理页标记为空闲。 */
static void /* 声明下方函数的返回类型与可见范围。 */
bitmap_set(int index) /* 函数作用：把指定物理页标记为空闲。 */
{ /* 开始当前结构或代码块。 */
  free_bitmap[index >> 3] |= (uchar)(1U << (index & 7)); /* 把目标页对应位设为1，同时保留同一字节中的其他页状态。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：把指定物理页标记为占用。 */
static void /* 声明下方函数的返回类型与可见范围。 */
bitmap_clear(int index) /* 函数作用：把指定物理页标记为已分配。 */
{ /* 开始当前结构或代码块。 */
  free_bitmap[index >> 3] &= (uchar)~(1U << (index & 7)); /* 把目标页对应位清零，同时保留同一字节中的其他页状态。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：把 RAM 页下标转换为恒等映射物理地址。 */
static void * /* 声明下方函数的返回类型与可见范围。 */
index_to_page(int index) /* 函数作用：把位图下标换算为物理页地址。 */
{ /* 开始当前结构或代码块。 */
  return (void *)(KERNBASE + (uint64)index * PGSIZE); /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：把 RAM 物理地址转换为位图页下标。 */
static int /* 声明下方函数的返回类型与可见范围。 */
page_to_index(uint64 address) /* 函数作用：把物理页地址换算为位图下标。 */
{ /* 开始当前结构或代码块。 */
  return (int)((address - KERNBASE) / PGSIZE); /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：初始化位图并从物理内存顶端扣除个人配额页。 */
void /* 声明下方函数的返回类型与可见范围。 */
kinit(void) /* 函数作用：初始化物理页位图和个人内存配额。 */
{ /* 开始当前结构或代码块。 */
  int i; /* 声明循环下标。 */
  uint64 first_address = PGROUNDUP((uint64)kernel_end); /* 计算内核镜像后第一个页对齐地址。 */

  memset(free_bitmap, 0, sizeof(free_bitmap)); /* 按指定值初始化目标内存区域。 */
  first_page = page_to_index(first_address); /* 把内核末端物理地址换算为位图管理范围的首下标。 */
  limit_page = (int)RAM_PAGE_COUNT - LAB3_MEMCAP_DELTA; /* 从RAM总页数中扣除个人参数规定的14个保留页。 */
  if (first_page < 0 || first_page >= limit_page) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    panic("kinit: no allocatable pages"); /* 检测到不可恢复的内核状态并停止运行。 */
  for (i = first_page; i < limit_page; i++) /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    bitmap_set(i); /* 函数作用：把指定物理页标记为空闲。 */
  free_pages = (uint64)(limit_page - first_page); /* 记录初始可分配页数，供快速OOM判断和验收观测。 */
#if LAB3_ALLOC_ORDER == 0
  alloc_hint = first_page;             /* 升序从最低页开始。 */
#elif LAB3_ALLOC_ORDER == 1
  alloc_hint = limit_page - 1;         /* 本人参数：降序从最高页开始。 */
#else
  alloc_hint = first_page + (limit_page - first_page) / 2; /* 兼容分配顺序2：把首次搜索位置设在可用区中点。 */
#endif
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：按个人顺序分配并清零一个物理页，耗尽返回 NULL。 */
void * /* 声明下方函数的返回类型与可见范围。 */
kalloc(void) /* 函数作用：按个人顺序分配并清零一个物理页。 */
{ /* 开始当前结构或代码块。 */
  int chosen = -1; /* 保存本次选中的空闲页下标。 */
  int i; /* 声明循环下标。 */

  if (free_pages == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return 0; /* 向调用方报告本次操作成功或返回空值。 */
#if LAB3_ALLOC_ORDER == 0
  for (i = alloc_hint; i < limit_page; i++) /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    if (bitmap_get(i)) { chosen = i; break; } /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
  if (chosen < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    for (i = first_page; i < alloc_hint; i++) /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
      if (bitmap_get(i)) { chosen = i; break; } /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
#elif LAB3_ALLOC_ORDER == 1
  for (i = alloc_hint; i >= first_page; i--) /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    if (bitmap_get(i)) { chosen = i; break; } /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
  if (chosen < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    for (i = limit_page - 1; i > alloc_hint; i--) /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
      if (bitmap_get(i)) { chosen = i; break; } /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
#else
  for (i = 0; i < limit_page - first_page; i++) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    int delta = (i + 1) / 2; /* 计算本轮距离中点的页数，使搜索半径逐步扩大。 */
    int candidate = (i & 1) ? alloc_hint - delta : alloc_hint + delta; /* 交替选择中点左侧和右侧的候选页。 */
    if (candidate >= first_page && candidate < limit_page && bitmap_get(candidate)) { /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      chosen = candidate; /* 记住首个落在合法范围且位图标为空闲的候选页。 */
      break; /* 结束当前循环或switch分支。 */
    } /* 结束当前条件、循环或函数代码块。 */
  } /* 结束当前条件、循环或函数代码块。 */
#endif
  if (chosen < 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    panic("kalloc: bitmap accounting mismatch"); /* 检测到不可恢复的内核状态并停止运行。 */
  bitmap_clear(chosen); /* 函数作用：把指定物理页标记为已分配。 */
  free_pages--; /* 分配成功后同步减少全局空闲页计数。 */
#if LAB3_ALLOC_ORDER == 0
  alloc_hint = chosen + 1; /* 升序策略从本页的下一页继续搜索，减少重复扫描。 */
#elif LAB3_ALLOC_ORDER == 1
  alloc_hint = chosen - 1; /* 本人降序策略从本页的前一页继续向低地址搜索。 */
#endif
  memset(index_to_page(chosen), 0, PGSIZE); /* 清零防止跨进程泄露旧数据。 */
  return index_to_page(chosen); /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：校验并回收一页，同时填充垃圾值暴露悬挂指针。 */
void /* 声明下方函数的返回类型与可见范围。 */
kfree(void *page) /* 函数作用：检查并回收一个物理页。 */
{ /* 开始当前结构或代码块。 */
  uint64 address = (uint64)page; /* 保存待检查的物理或用户地址。 */
  int index; /* 保存物理页对应的位图下标。 */

  if ((address & (PGSIZE - 1)) != 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    panic("kfree: unaligned page"); /* 检测到不可恢复的内核状态并停止运行。 */
  if (address < KERNBASE + (uint64)first_page * PGSIZE || /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      address >= KERNBASE + (uint64)limit_page * PGSIZE) /* 同时拒绝内核镜像页和个人配额之外的顶端保留页。 */
    panic("kfree: page outside allocator"); /* 检测到不可恢复的内核状态并停止运行。 */
  index = page_to_index(address); /* 把通过范围检查的物理地址换成位图下标。 */
  if (bitmap_get(index)) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    panic("kfree: double free"); /* 检测到不可恢复的内核状态并停止运行。 */
  memset(page, 1, PGSIZE);             /* 释放后使用会更快暴露。 */
  bitmap_set(index); /* 函数作用：把指定物理页标记为空闲。 */
  free_pages++; /* 页面重新可分配后同步增加全局空闲页计数。 */
#if LAB3_ALLOC_ORDER == 0
  if (index < alloc_hint) alloc_hint = index; /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
#elif LAB3_ALLOC_ORDER == 1
  if (index > alloc_hint) alloc_hint = index; /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
#endif
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：返回当前可分配物理页数量。 */
uint64 /* 声明下方函数的返回类型与可见范围。 */
kfree_count(void) /* 函数作用：返回当前空闲物理页数量。 */
{ /* 开始当前结构或代码块。 */
  return free_pages; /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */
