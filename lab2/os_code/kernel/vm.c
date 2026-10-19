/*
 * Lab2 最小 Sv39 用户页表。
 *
 * 内核继续在 satp=0 的 Bare 模式运行；只有返回用户态时切换到本页表。
 * 它映射三类地址：用户虚拟地址 0、活动 trapframe，以及正在执行的
 * trampoline 物理页。没有通用分配器，页表页来自固定静态池。
 */
#include "types.h"      /* 引入 uint64 等页表代码使用的基本类型。 */
#include "riscv.h"      /* 引入 Sv39 页表项类型、标志位和地址转换宏。 */
#include "memlayout.h"  /* 引入用户执行区、trapframe 与 trampoline 地址。 */
#include "defs.h"       /* 引入 memset 和 panic 的函数原型。 */

#define PT_POOL_PAGES 16 /* 固定预留 16 个页表页，避免 Lab2 引入通用分配器。 */

static __attribute__((aligned(PGSIZE))) pte_t pt_pool[PT_POOL_PAGES][512]; /* 建立按 4KB 对齐的静态页表页池。 */
static int pt_used;                  /* 记录页表池中已经分配的页面数量。 */
static pagetable_t root;             /* 保存当前唯一用户地址空间的 Sv39 根页表。 */

extern char trampoline[];            /* 引用链接脚本放置的 trampoline 物理起始符号。 */

/* 函数作用：从固定静态页表池分配并清零一个 4KB 页表页。 */
static pagetable_t                    /* 返回一个可作为任意级页表的 4KB 页面。 */
alloc_pt_page(void)                   /* 从固定静态池顺序分配并清零一页。 */
{                                     /* 开始执行页表页分配。 */
  pagetable_t page;                   /* 暂存本次选中的页表页首地址。 */
  if (pt_used >= PT_POOL_PAGES)       /* 检查固定页表池是否已经耗尽。 */
    panic("page-table pool exhausted"); /* 页表不足无法安全继续，因此打印错误并停机。 */
  page = pt_pool[pt_used++];          /* 取出下一空闲页，同时推进已用页计数。 */
  memset(page, 0, PGSIZE);            /* 清除旧页表项，保证所有条目初始无效。 */
  return page;                        /* 返回已经清零且正确对齐的页表页。 */
}                                     /* 结束页表页分配。 */

/* 函数作用：逐级遍历 Sv39 页表，并按需建立缺失的中间页表。 */
static pte_t *                                        /* 返回虚拟地址对应的第 0 级叶子页表项地址。 */
walk_create(pagetable_t pagetable, uint64 va)         /* 逐级遍历 Sv39，并按需创建中间页表。 */
{                                                     /* 开始三级页表遍历。 */
  int level;                                          /* 保存当前正在处理的 Sv39 级别。 */
  for (level = 2; level > 0; level--) {               /* 从根级 2 依次走到中间级 1。 */
    pte_t *pte = &pagetable[PX(level, va)];            /* 用本级虚拟页号索引得到页表项。 */
    if (*pte & PTE_V) {                               /* 有效位为 1 表示下一级页表已经存在。 */
      pagetable = (pagetable_t)PTE2PA(*pte);           /* 从页表项提取下一级页表物理地址。 */
    } else {                                          /* 当前路径尚未建立，需要创建下一级页表。 */
      pagetable_t next = alloc_pt_page();              /* 从静态池申请并清零一个页表页。 */
      *pte = PA2PTE(next) | PTE_V;                     /* 把下一级地址编码进非叶子有效页表项。 */
      pagetable = next;                                /* 继续在新建的下一级页表中遍历。 */
    }                                                 /* 结束当前级是否已存在的分支。 */
  }                                                   /* 完成级 2 和级 1 的遍历。 */
  return &pagetable[PX(0, va)];                       /* 返回最终第 0 级页表中的目标叶子条目。 */
}                                                     /* 结束 Sv39 页表遍历。 */

/* 函数作用：把一个虚拟页映射到指定物理页并设置页表权限。 */
static void                                           /* 单页映射只修改页表，不返回结果。 */
map_page(uint64 va, uint64 pa, uint64 flags)          /* 把一个虚拟页映射到物理页并附加权限。 */
{                                                     /* 开始建立一条第 0 级叶子映射。 */
  pte_t *pte = walk_create(root, va);                 /* 找到或创建虚拟地址对应的叶子页表项。 */
  if (*pte & PTE_V)                                   /* 有效叶子已经存在说明映射逻辑发生冲突。 */
    panic("duplicate user mapping");                  /* 拒绝静默覆盖原映射并停机定位错误。 */
  *pte = PA2PTE(pa) | flags | PTE_V | PTE_A | PTE_D; /* 编码物理页号、权限、有效/访问/脏位。 */
}                                                     /* 结束单页映射。 */

/* 函数作用：建立用户执行区、活动 trapframe 和 trampoline 的最小页表。 */
void                                                  /* 初始化函数建立全局页表，不返回结果。 */
user_pagetable_init(void)                             /* 创建用户执行区、trapframe 和跳板映射。 */
{                                                     /* 开始构造最小用户地址空间。 */
  uint64 offset;                                      /* 遍历用户执行区时保存当前页内偏移。 */
  uint64 trampoline_pa = PGROUNDDOWN((uint64)trampoline); /* 把跳板符号向下对齐到物理页首。 */

  pt_used = 0;                                        /* 重置静态页表池分配游标。 */
  root = alloc_pt_page();                             /* 分配并清零 Sv39 根页表。 */

  /* 用户代码、数据、堆栈暂时共享 RWX 权限，权限细分留给 lab3。 */
  for (offset = 0; offset < USER_MEM_SIZE; offset += PGSIZE) /* 按 4KB 页面遍历整个固定用户区。 */
    map_page(offset, USER_BASE + offset, PTE_R | PTE_W | PTE_X | PTE_U); /* 映射 VA offset 到执行区 PA。 */

  /* uservec 用高地址访问 trapframe，但 U 态本身没有 PTE_U 权限。 */
  map_page(TRAPFRAME, ACTIVE_TRAPFRAME_PA, PTE_R | PTE_W); /* 仅允许 S 态读写活动 trapframe。 */

  /*
   * userret 在物理地址开始执行后才写 satp；继续取指需要用户页表也按
   * 原地址映射这一页。PTE_U 清零，用户代码不能直接跳入该页。
   */
  map_page(trampoline_pa, trampoline_pa, PTE_R | PTE_X); /* 在用户页表中恒等映射跳板代码页。 */
}                                                       /* 完成最小用户页表构造。 */

/* 函数作用：把当前用户根页表地址编码成可写入 satp 的 Sv39 值。 */
uint64                                                  /* 返回可直接写入 satp 的 64 位编码。 */
user_satp(void)                                         /* 把根页表物理地址编码成 Sv39 satp。 */
{                                                       /* 开始生成用户页表控制值。 */
  return MAKE_SATP(root);                               /* 设置 MODE=Sv39 并填入根页表物理页号。 */
}                                                       /* 结束 satp 值生成。 */
