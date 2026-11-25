/*
 * Lab5 物理页分配器与 COW 引用计数。位图记录页能否重新分配，
 * refcnt 记录持有该页的映射数；两者共用 kmem.lock 保持一致。
 */
#include "types.h"                    /* uchar、uint16 和 uint64。 */
#include "riscv.h"                    /* PGSIZE 和页对齐宏。 */
#include "memlayout.h"                /* KERNBASE 和 PHYSTOP。 */
#include "course_sid.h"               /* 个人内存配额与 Lab5 参数。 */
#include "spinlock.h"                 /* kmem 自旋锁的完整类型。 */
#include "defs.h"                     /* memset、printf 和 panic。 */

extern char kernel_end[];              /* 链接脚本导出的内核镜像末尾。 */

#define RAM_PAGE_COUNT ((PHYSTOP - KERNBASE) / PGSIZE) /* RAM 物理页总数。 */
#define BITMAP_BYTES   ((RAM_PAGE_COUNT + 7) / 8)       /* 空闲位图字节数。 */

#if LAB5_REFCNT_SCHEME != 1            /* 本人参数必须使用全局引用数组。 */
#error "LAB5_REFCNT_SCHEME must be 1"
#endif

static struct {                        /* COW 共享的全局内存状态。 */
  struct spinlock lock;                /* 同时保护位图、引用数与统计量。 */
  uint16 refcnt[RAM_PAGE_COUNT];       /* 每个物理页当前的映射引用数。 */
  uint64 cow_copies;                   /* 全局真正分配并拷贝的 COW 次数。 */
} kmem;

static uchar free_bitmap[BITMAP_BYTES]; /* 1=空闲，0=已分配。 */
static int first_page;                  /* 内核镜像后的首个可托管页。 */
static int limit_page;                  /* 扣除个人配额后的上界。 */
static int alloc_hint;                  /* 下次位图搜索的起点。 */
static uint64 free_pages;               /* 当前可分配页数。 */

/* 函数作用：读取页的空闲位；调用者持有 kmem.lock。 */
static int bitmap_get(int index)
{
  return (free_bitmap[index >> 3] >> (index & 7)) & 1; /* 提取指定二进制位。 */
}

/* 函数作用：将页标记为空闲；调用者持有 kmem.lock。 */
static void bitmap_set(int index)
{
  free_bitmap[index >> 3] |= (uchar)(1U << (index & 7)); /* 设置目标位。 */
}

/* 函数作用：将页标记为占用；调用者持有 kmem.lock。 */
static void bitmap_clear(int index)
{
  free_bitmap[index >> 3] &= (uchar)~(1U << (index & 7)); /* 清除目标位。 */
}

/* 函数作用：把 RAM 页下标换算为物理页地址。 */
static void *index_to_page(int index)
{
  return (void *)(KERNBASE + (uint64)index * PGSIZE); /* 内核使用 RAM 恒等映射。 */
}

/* 函数作用：把 RAM 物理页地址换算为数组下标。 */
static int page_to_index(uint64 address)
{
  return (int)((address - KERNBASE) / PGSIZE); /* KERNBASE 对应第 0 页。 */
}

/* 函数作用：校验地址对齐且位于分配器托管区间。 */
static int checked_index(uint64 address)
{
  if ((address & (PGSIZE - 1)) != 0)   /* 引用计数只能以整页为单位。 */
    panic("kref: unaligned page");
  if (address < KERNBASE + (uint64)first_page * PGSIZE ||
      address >= KERNBASE + (uint64)limit_page * PGSIZE) /* 排除内核镜像与保留页。 */
    panic("kref: page outside allocator");
  return page_to_index(address);        /* 返回经过边界检查的下标。 */
}

/* 函数作用：初始化位图、引用数组、锁与个人内存配额。 */
void kinit(void)
{
  int i;                                /* 托管页遍历下标。 */
  uint64 first_address = PGROUNDUP((uint64)kernel_end); /* 跳过内核镜像。 */

  initlock(&kmem.lock, "kmem");        /* 任何内存操作前初始化锁。 */
  memset(free_bitmap, 0, sizeof(free_bitmap)); /* 先把所有页标成不可分配。 */
  memset(kmem.refcnt, 0, sizeof(kmem.refcnt)); /* 初始没有已分配页引用。 */
  kmem.cow_copies = 0;                 /* COW 累计值从零开始。 */
  first_page = page_to_index(first_address); /* 定位首个可分配页。 */
  limit_page = (int)RAM_PAGE_COUNT - LAB3_MEMCAP_DELTA; /* 扣除个人保留页。 */
  if (first_page < 0 || first_page >= limit_page)
    panic("kinit: no allocatable pages"); /* 没有可用页时无法继续启动。 */
  for (i = first_page; i < limit_page; i++)
    bitmap_set(i);                     /* 只公布内核之后、配额之内的页。 */
  free_pages = (uint64)(limit_page - first_page); /* 建立 O(1) 空闲页计数。 */
#if LAB3_ALLOC_ORDER == 0
  alloc_hint = first_page;             /* 升序方案从低地址开始。 */
#elif LAB3_ALLOC_ORDER == 1
  alloc_hint = limit_page - 1;         /* 本人降序方案从高地址开始。 */
#else
  alloc_hint = first_page + (limit_page - first_page) / 2; /* 兼容中点方案。 */
#endif
}

/* 函数作用：分配并清零一个物理页，初始引用数为 1。 */
void *kalloc(void)
{
  int chosen = -1;                    /* 本轮选中的页下标。 */
  int i;                               /* 位图扫描下标。 */

  acquire(&kmem.lock);                 /* 位图、计数与提示位置原子更新。 */
  if (free_pages == 0) {
    release(&kmem.lock);               /* OOM 返回前交还锁。 */
    return 0;                          /* NULL 表示物理内存耗尽。 */
  }
#if LAB3_ALLOC_ORDER == 0
  for (i = alloc_hint; i < limit_page; i++)
    if (bitmap_get(i)) { chosen = i; break; } /* 从提示点向高地址查找。 */
  if (chosen < 0)
    for (i = first_page; i < alloc_hint; i++)
      if (bitmap_get(i)) { chosen = i; break; } /* 到顶后从底部回绕。 */
#elif LAB3_ALLOC_ORDER == 1
  for (i = alloc_hint; i >= first_page; i--)
    if (bitmap_get(i)) { chosen = i; break; } /* 本人方案向低地址查找。 */
  if (chosen < 0)
    for (i = limit_page - 1; i > alloc_hint; i--)
      if (bitmap_get(i)) { chosen = i; break; } /* 到底后从顶部回绕。 */
#else
  for (i = 0; i < limit_page - first_page; i++) {
    int delta = (i + 1) / 2;           /* 计算与中点的距离。 */
    int candidate = (i & 1) ? alloc_hint - delta : alloc_hint + delta; /* 交替左右搜索。 */
    if (candidate >= first_page && candidate < limit_page && bitmap_get(candidate)) {
      chosen = candidate;              /* 选中首个合法空闲页。 */
      break;
    }
  }
#endif
  if (chosen < 0)
    panic("kalloc: bitmap accounting mismatch"); /* 计数与位图不一致。 */
  bitmap_clear(chosen);                /* 先将页改为占用。 */
  kmem.refcnt[chosen] = 1;             /* 分配返回的唯一持有者计为 1。 */
  free_pages--;                        /* 同步更新空闲数。 */
#if LAB3_ALLOC_ORDER == 0
  alloc_hint = chosen + 1;             /* 下次从后一页继续。 */
#elif LAB3_ALLOC_ORDER == 1
  alloc_hint = chosen - 1;             /* 本人方案下次从前一页继续。 */
#endif
  release(&kmem.lock);                 /* 所有权已转交调用者。 */
  memset(index_to_page(chosen), 0, PGSIZE); /* 清除旧数据防止泄漏。 */
  return index_to_page(chosen);         /* 返回内核可访问的恒等映射地址。 */
}

/* 函数作用：为新页表映射增加一个物理页引用。 */
void kref_inc(uint64 address)
{
  int index = checked_index(address);  /* 校验并转换物理页地址。 */
  acquire(&kmem.lock);                 /* 避免与最后一次 kfree 竞争。 */
  if (bitmap_get(index) || kmem.refcnt[index] == 0)
    panic("kref_inc: free page");      /* 空闲页不能直接被共享。 */
  if (kmem.refcnt[index] == 0xffffU)
    panic("kref_inc: overflow");       /* 拒绝 uint16 静默溢出。 */
  kmem.refcnt[index]++;                /* 记录新映射的持有。 */
  release(&kmem.lock);                 /* 新计数已对全局可见。 */
}

/* 函数作用：返回物理页当前的引用数快照。 */
uint64 kref_get(uint64 address)
{
  int index = checked_index(address);  /* 得到可信的引用数下标。 */
  uint64 value;                        /* 保存持锁时的快照。 */
  acquire(&kmem.lock);                 /* 与引用数增减互斥。 */
  value = kmem.refcnt[index];          /* 复制而不暴露数组本身。 */
  release(&kmem.lock);                 /* 快照取得后交还锁。 */
  return value;
}

/* 函数作用：减少一个映射引用，仅在计数归零时真正回收页。 */
void kfree(void *page)
{
  uint64 address = (uint64)page;       /* 待释放物理页地址。 */
  int index = checked_index(address);  /* 校验并定位计数项。 */

  acquire(&kmem.lock);                 /* 减计数与位图归还不可分割。 */
  if (bitmap_get(index) || kmem.refcnt[index] == 0)
    panic("kfree: double free");       /* 防止同一页被重复分配。 */
  kmem.refcnt[index]--;                /* 去掉当前所有者的一个引用。 */
  if (kmem.refcnt[index] != 0) {
    release(&kmem.lock);               /* 仍有其他 COW 映射时不回收。 */
    return;                            /* 不填充页，保护其他映射的数据。 */
  }
  memset(page, 0x5a, PGSIZE);          /* 最后引用消失后填充固定值。 */
  bitmap_set(index);                   /* 只有零引用页才能重新分配。 */
  free_pages++;                        /* 同步增加空闲页数。 */
#if LAB3_ALLOC_ORDER == 0
  if (index < alloc_hint)
    alloc_hint = index;                /* 升序方案优先重用更低空闲页。 */
#elif LAB3_ALLOC_ORDER == 1
  if (index > alloc_hint)
    alloc_hint = index;                /* 本人降序方案优先重用高地址页。 */
#endif
  release(&kmem.lock);                 /* 页内容和位图都完成后交还锁。 */
}

/* 函数作用：返回当前可分配物理页数。 */
uint64 kfree_count(void)
{
  uint64 value;                        /* 持锁时的稳定快照。 */
  acquire(&kmem.lock);
  value = free_pages;                  /* 与位图保持一致的 O(1) 计数。 */
  release(&kmem.lock);
  return value;
}

/* 函数作用：统计引用数大于 1 的物理页数。 */
uint64 kshared_count(void)
{
  uint64 shared = 0;                   /* 正在被多个映射持有的页数。 */
  int i;                               /* 托管页遍历下标。 */
  acquire(&kmem.lock);                 /* 保证遍历期间计数项有效。 */
  for (i = first_page; i < limit_page; i++)
    if (kmem.refcnt[i] > 1)            /* pmc(1) 按物理页而非映射数计数。 */
      shared++;
  release(&kmem.lock);
  return shared;
}

/* 函数作用：记录一次真正发生的 COW 分配和拷贝。 */
void kcow_record_copy(void)
{
  acquire(&kmem.lock);                 /* 防止并发更新丢失。 */
  kmem.cow_copies++;                   /* refcnt==1 的原地恢复不计数。 */
  release(&kmem.lock);
}

/* 函数作用：返回启动至今的实际 COW 拷贝次数。 */
uint64 kcow_copy_count(void)
{
  uint64 value;                        /* 保存持锁时的快照。 */
  acquire(&kmem.lock);
  value = kmem.cow_copies;             /* pmc(0) 的全局单调计数。 */
  release(&kmem.lock);
  return value;
}

/* 函数作用：启动时验证 1→2→1→0 引用变化与最终回收。 */
void kref_selftest(void)
{
  uint64 before = kfree_count();       /* 自测前的空闲页数。 */
  void *page = kalloc();               /* 创建引用数为 1 的测试页。 */
  if (page == 0 || kref_get((uint64)page) != 1)
    panic("kref selftest: alloc");     /* 验证 kalloc 初始引用。 */
  kref_inc((uint64)page);              /* 模拟 fork 新增共享映射。 */
  if (kref_get((uint64)page) != 2)
    panic("kref selftest: share");     /* 共享后必须为 2。 */
  kfree(page);                         /* 释放一个持有者，不应回收页。 */
  if (kref_get((uint64)page) != 1 || kfree_count() != before - 1)
    panic("kref selftest: release one"); /* 验证不提前回收。 */
  kfree(page);                         /* 最后引用消失，真正归还页。 */
  if (kfree_count() != before)
    panic("kref selftest: final free"); /* 自测不允许泄漏。 */
  printf("lab5: kref selftest PASS\n"); /* 显式报告引用计数通过。 */
}
