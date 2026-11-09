/* Lab3 SV39 页表、受控拷贝与页表观测实现。 */
#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "defs.h"

extern char trampoline[];           /* 预置跳板页的内核镜像地址。 */
static pagetable_t kernel_pagetable; /* 全系统共享的内核根页表。 */

/* 函数作用：判断 PTE 是否为带访问权限的叶子映射。 */
static int /* 声明下方函数的返回类型与可见范围。 */
pte_is_leaf(pte_t pte) /* 函数作用：判断页表项是否具有叶子权限。 */
{ /* 开始当前结构或代码块。 */
  return (pte & (PTE_R | PTE_W | PTE_X)) != 0; /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：递归回收已经不承载任何映射的中间页表。 */
static int /* 声明下方函数的返回类型与可见范围。 */
prune_empty(pagetable_t pagetable, int level) /* 函数作用：递归释放已经变空的中间页表。 */
{ /* 开始当前结构或代码块。 */
  int i; /* 声明循环下标。 */
  int empty = 1; /* 记录当前页表是否仍为空。 */

  for (i = 0; i < 512; i++) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    pte_t pte = pagetable[i]; /* 保存当前虚拟地址对应的页表项。 */
    if ((pte & PTE_V) == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      continue; /* 跳过当前空洞或无效条目并检查下一项。 */
    if (level > 0 && !pte_is_leaf(pte)) { /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      pagetable_t child = (pagetable_t)PTE2PA(pte); /* 保存下一级页表或新建子进程。 */
      if (prune_empty(child, level - 1)) { /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
        pagetable[i] = 0; /* 先断开父表到空子表的引用，避免保留悬挂PTE。 */
        kfree(child); /* 函数作用：检查并回收一个物理页。 */
      } else { /* 前一条件不成立时进入备用处理分支。 */
        empty = 0; /* 子树仍有有效映射，因此当前表不能被上级回收。 */
      } /* 结束当前条件、循环或函数代码块。 */
    } else { /* 前一条件不成立时进入备用处理分支。 */
      empty = 0; /* 发现叶映射或无法继续递归的有效项，标记当前表非空。 */
    } /* 结束当前条件、循环或函数代码块。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return empty; /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：遍历三级页表，按需建立中间页并返回 L0 PTE 指针。 */
pte_t * /* 声明下方函数的返回类型与可见范围。 */
walk(pagetable_t pagetable, uint64 va, int alloc) /* 函数作用：遍历SV39三级页表并返回L0页表项。 */
{ /* 开始当前结构或代码块。 */
  int level; /* 保存当前遍历的SV39页表层级。 */

  if (va >= MAXVA) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return 0; /* 向调用方报告本次操作成功或返回空值。 */
  for (level = 2; level > 0; level--) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    pte_t *pte = &pagetable[PX(level, va)]; /* 保存当前虚拟地址对应的页表项。 */
    if (*pte & PTE_V) { /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      if (pte_is_leaf(*pte)) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
        return 0;                    /* 本实验不使用中间层大页映射。 */
      pagetable = (pagetable_t)PTE2PA(*pte); /* 沿非叶PTE保存的物理地址进入下一级页表。 */
    } else { /* 前一条件不成立时进入备用处理分支。 */
      pagetable_t child; /* 保存下一级页表或新建子进程。 */
      if (!alloc) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
        return 0; /* 向调用方报告本次操作成功或返回空值。 */
      child = (pagetable_t)kalloc(); /* 向物理页分配器申请一个页面。 */
      if (child == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
        return 0; /* 向调用方报告本次操作成功或返回空值。 */
      *pte = PA2PTE(child) | PTE_V;  /* 非叶 PTE 只设置有效位。 */
      pagetable = child; /* 新子表挂接后继续在其中处理下一层索引。 */
    } /* 结束当前条件、循环或函数代码块。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return &pagetable[PX(0, va)]; /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：映射连续区间，失败时撤销本次叶项并回收空中间页。 */
int /* 声明下方函数的返回类型与可见范围。 */
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm) /* 函数作用：建立连续虚拟页到物理页的映射。 */
{ /* 开始当前结构或代码块。 */
  uint64 first; /* 保存映射区间的首个页地址。 */
  uint64 last; /* 保存映射区间的最后一个页地址。 */
  uint64 current; /* 保存当前遍历地址或活动进程。 */
  uint64 mapped = 0; /* 统计本轮已成功建立的叶映射数量。 */

  if (size == 0 || va + size < va) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  first = PGROUNDDOWN(va); /* 把虚拟区间起点向下对齐到首个4KiB页面。 */
  last = PGROUNDDOWN(va + size - 1); /* 计算含末字节的最后一个虚拟页首地址。 */
  pa = PGROUNDDOWN(pa); /* 物理映射起点同样按页边界规范化。 */
  for (current = first;; current += PGSIZE, pa += PGSIZE) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    pte_t *pte = walk(pagetable, current, 1); /* 保存当前虚拟地址对应的页表项。 */
    if (pte == 0 || (*pte & PTE_V) != 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      break; /* 结束当前循环或switch分支。 */
    *pte = PA2PTE(pa) | perm | PTE_V; /* 编码物理页号、调用方权限和有效位，形成叶PTE。 */
    mapped++; /* 记录本调用新建的叶项数量，失败时只撤销这些项。 */
    if (current == last) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      return 0; /* 向调用方报告本次操作成功或返回空值。 */
  } /* 结束当前条件、循环或函数代码块。 */
  for (current = first; mapped > 0; current += PGSIZE, mapped--) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    pte_t *pte = walk(pagetable, current, 0); /* 保存当前虚拟地址对应的页表项。 */
    if (pte != 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      *pte = 0;                      /* mappages 不拥有调用方传入的物理页。 */
  } /* 结束当前条件、循环或函数代码块。 */
  prune_empty(pagetable, 2); /* 函数作用：递归释放已经变空的中间页表。 */
  return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：解除连续页映射，可选择同步释放叶物理页。 */
void /* 声明下方函数的返回类型与可见范围。 */
uvmunmap(pagetable_t pagetable, uint64 va, uint64 pages, int free_leaf_pages) /* 函数作用：解除用户页映射并按需释放物理页。 */
{ /* 开始当前结构或代码块。 */
  uint64 i; /* 声明循环下标。 */

  if ((va & (PGSIZE - 1)) != 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    panic("uvmunmap: unaligned va"); /* 检测到不可恢复的内核状态并停止运行。 */
  for (i = 0; i < pages; i++, va += PGSIZE) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    pte_t *pte = walk(pagetable, va, 0); /* 保存当前虚拟地址对应的页表项。 */
    uint64 pa; /* 保存翻译得到的物理页首地址。 */
    if (pte == 0 || (*pte & PTE_V) == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      continue;                      /* guard 空洞属于合法布局。 */
    if (!pte_is_leaf(*pte)) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      panic("uvmunmap: non-leaf at level zero"); /* 检测到不可恢复的内核状态并停止运行。 */
    pa = PTE2PA(*pte); /* 在清除PTE前保存其叶物理页地址，供可选释放使用。 */
    *pte = 0;                        /* 必须先使映射失效，再释放底层页。 */
    if (free_leaf_pages) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      kfree((void *)pa); /* 函数作用：检查并回收一个物理页。 */
  } /* 结束当前条件、循环或函数代码块。 */
  prune_empty(pagetable, 2); /* 函数作用：递归释放已经变空的中间页表。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：递归释放用户叶页及所有下级页表页。 */
static void /* 声明下方函数的返回类型与可见范围。 */
free_user_tree(pagetable_t pagetable, int level) /* 函数作用：递归释放用户叶页和中间页表。 */
{ /* 开始当前结构或代码块。 */
  int i; /* 声明循环下标。 */

  for (i = 0; i < 512; i++) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    pte_t pte = pagetable[i]; /* 保存当前虚拟地址对应的页表项。 */
    if ((pte & PTE_V) == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      continue; /* 跳过当前空洞或无效条目并检查下一项。 */
    if (level > 0 && !pte_is_leaf(pte)) { /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      pagetable_t child = (pagetable_t)PTE2PA(pte); /* 保存下一级页表或新建子进程。 */
      pagetable[i] = 0; /* 先从父表断开子表，再递归释放子树。 */
      free_user_tree(child, level - 1); /* 函数作用：递归释放用户叶页和中间页表。 */
      kfree(child); /* 函数作用：检查并回收一个物理页。 */
    } else { /* 前一条件不成立时进入备用处理分支。 */
      pagetable[i] = 0; /* 清除叶项；共享特殊页随后只取消映射而不释放。 */
      if (pte & PTE_U) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
        kfree((void *)PTE2PA(pte));  /* trampoline 等 S 态页不归用户空间所有。 */
    } /* 结束当前条件、循环或函数代码块。 */
  } /* 结束当前条件、循环或函数代码块。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：创建仅含共享 trampoline 映射的新用户根页表。 */
pagetable_t /* 声明下方函数的返回类型与可见范围。 */
uvmcreate(void) /* 函数作用：创建带trampoline映射的用户根页表。 */
{ /* 开始当前结构或代码块。 */
  pagetable_t pagetable = (pagetable_t)kalloc(); /* 申请并取得已经清零的SV39根页表页面。 */

  if (pagetable == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return 0; /* 向调用方报告本次操作成功或返回空值。 */
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
               (uint64)trampoline, PTE_R | PTE_X) < 0) { /* 把共享跳板映射为S态只读可执行且不设置PTE_U。 */
    kfree(pagetable); /* 函数作用：检查并回收一个物理页。 */
    return 0; /* 向调用方报告本次操作成功或返回空值。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return pagetable; /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：释放完整用户地址空间和独立 trapframe 页。 */
void /* 声明下方函数的返回类型与可见范围。 */
uvmfree(pagetable_t pagetable, uint64 trapframe_pa) /* 函数作用：释放完整用户地址空间。 */
{ /* 开始当前结构或代码块。 */
  pte_t *pte; /* 保存当前虚拟地址对应的页表项。 */

  if (pagetable == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return; /* 空页表指针没有任何所有权需要释放，直接结束。 */
  pte = walk(pagetable, TRAPFRAME, 0); /* 查询目标虚拟地址对应的页表项。 */
  if (pte != 0 && (*pte & PTE_V)) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    *pte = 0; /* 取消固定高地址trapframe映射，避免递归函数把它当用户页。 */
  if (trapframe_pa != 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    kfree((void *)trapframe_pa); /* 函数作用：检查并回收一个物理页。 */
  free_user_tree(pagetable, 2);      /* 用户页释放；共享 trampoline 只清项。 */
  kfree(pagetable); /* 函数作用：检查并回收一个物理页。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：分配并映射用户区间，任何失败均回滚本次全部页面。 */
int /* 声明下方函数的返回类型与可见范围。 */
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int perm) /* 函数作用：为用户虚拟区间分配并映射物理页。 */
{ /* 开始当前结构或代码块。 */
  uint64 start = PGROUNDUP(oldsz); /* 保存区间起点、计时起点或镜像起点。 */
  uint64 end = PGROUNDUP(newsz); /* 保存页对齐后的区间终点。 */
  uint64 va; /* 保存当前处理的用户虚拟页地址。 */

  if (newsz < oldsz || newsz >= TRAPFRAME) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  for (va = start; va < end; va += PGSIZE) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    void *page = kalloc(); /* 向物理页分配器申请一个页面。 */
    if (page == 0 || mappages(pagetable, va, PGSIZE, /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
                              (uint64)page, perm) < 0) { /* 任一物理页或中间页表失败都进入本轮统一回滚。 */
      if (page != 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
        kfree(page); /* 函数作用：检查并回收一个物理页。 */
      if (va > start) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
        uvmunmap(pagetable, start, (va - start) / PGSIZE, 1); /* 函数作用：解除用户页映射并按需释放物理页。 */
      return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
    } /* 结束当前条件、循环或函数代码块。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return 0; /* 向调用方报告本次操作成功或返回空值。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：收缩地址空间并释放完全位于新边界以上的页面。 */
uint64 /* 声明下方函数的返回类型与可见范围。 */
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) /* 函数作用：收缩用户区间并释放多余页面。 */
{ /* 开始当前结构或代码块。 */
  uint64 first; /* 保存映射区间的首个页地址。 */
  uint64 end; /* 保存页对齐后的区间终点。 */

  if (newsz >= oldsz) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return oldsz; /* 把当前计算结果或对象地址返回调用方。 */
  first = PGROUNDUP(newsz); /* 保留包含新break的部分页，从下一完整页开始释放。 */
  end = PGROUNDUP(oldsz); /* 把旧break向上对齐，得到原映射区间的页后边界。 */
  if (end > first) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    uvmunmap(pagetable, first, (end - first) / PGSIZE, 1); /* 函数作用：解除用户页映射并按需释放物理页。 */
  return newsz; /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：装载器跨页写入用户镜像，不受最终代码页 W 位限制。 */
int /* 声明下方函数的返回类型与可见范围。 */
uvmwrite(pagetable_t pagetable, uint64 dst, const void *src, uint64 len) /* 函数作用：把内嵌程序镜像跨页写入用户空间。 */
{ /* 开始当前结构或代码块。 */
  const uchar *source = (const uchar *)src; /* 保存当前尚未复制的源缓冲区指针。 */

  while (len > 0) { /* 在剩余数据或条件有效时继续循环处理。 */
    uint64 va0 = PGROUNDDOWN(dst); /* 取得当前目标地址所在用户页的页首虚拟地址。 */
    pte_t *pte = walk(pagetable, va0, 0); /* 保存当前虚拟地址对应的页表项。 */
    uint64 amount; /* 保存本轮不跨页的复制字节数。 */
    uint64 pa; /* 保存翻译得到的物理页首地址。 */
    if (pte == 0 || (*pte & (PTE_V | PTE_U)) != (PTE_V | PTE_U)) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
    pa = PTE2PA(*pte); /* 从通过有效性检查的PTE提取目标物理页首。 */
    amount = PGSIZE - (dst - va0); /* 本轮最多复制到当前页末，禁止跨物理页直接访问。 */
    if (amount > len) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      amount = len; /* 剩余数据不足一页时仅复制实际剩余长度。 */
    memmove((void *)(pa + dst - va0), source, amount); /* 复制可能跨页或重叠的内存内容。 */
    len -= amount; /* 扣除本轮已经成功写入用户页的字节数。 */
    source += amount; /* 推进内核镜像源指针到尚未装载的数据。 */
    dst += amount; /* 推进用户目标地址；跨页后下一轮会重新walk。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return 0; /* 向调用方报告本次操作成功或返回空值。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：深拷贝父进程全部用户叶页，保护页空洞直接跳过。 */
int /* 声明下方函数的返回类型与可见范围。 */
uvmcopy(pagetable_t old, pagetable_t newpt, uint64 sz) /* 函数作用：逐页深拷贝父进程用户地址空间。 */
{ /* 开始当前结构或代码块。 */
  uint64 va; /* 保存当前处理的用户虚拟页地址。 */

  for (va = 0; va < PGROUNDUP(sz); va += PGSIZE) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    pte_t *pte = walk(old, va, 0); /* 保存当前虚拟地址对应的页表项。 */
    void *page; /* 保存为当前父进程叶页申请的子进程物理副本。 */
    int flags; /* 保存父PTE除有效位以外的R/W/X/U等权限。 */
    if (pte == 0 || (*pte & PTE_V) == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      continue; /* 跳过当前空洞或无效条目并检查下一项。 */
    if (!pte_is_leaf(*pte) || (*pte & PTE_U) == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      goto fail; /* 转入统一失败回滚路径，避免遗漏资源释放。 */
    page = kalloc(); /* 向物理页分配器申请一个页面。 */
    if (page == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      goto fail; /* 转入统一失败回滚路径，避免遗漏资源释放。 */
    memmove(page, (void *)PTE2PA(*pte), PGSIZE); /* 复制可能跨页或重叠的内存内容。 */
    flags = (int)(PTE_FLAGS(*pte) & ~PTE_V); /* 交给mappages重新添加有效位，同时保持原叶权限。 */
    if (mappages(newpt, va, PGSIZE, (uint64)page, flags) < 0) { /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      kfree(page); /* 函数作用：检查并回收一个物理页。 */
      goto fail; /* 转入统一失败回滚路径，避免遗漏资源释放。 */
    } /* 结束当前条件、循环或函数代码块。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return 0; /* 向调用方报告本次操作成功或返回空值。 */

fail: /* 定义失败回滚或分支跳转目标。 */
  uvmunmap(newpt, 0, PGROUNDUP(sz) / PGSIZE, 1); /* 函数作用：解除用户页映射并按需释放物理页。 */
  return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：检查 U 与指定访问权限并返回页首物理地址。 */
static uint64 /* 声明下方函数的返回类型与可见范围。 */
walk_user_address(pagetable_t pagetable, uint64 va, int required) /* 函数作用：校验用户权限并取得物理页地址。 */
{ /* 开始当前结构或代码块。 */
  pte_t *pte; /* 保存当前虚拟地址对应的页表项。 */

  if (va >= MAXVA) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return 0; /* 向调用方报告本次操作成功或返回空值。 */
  pte = walk(pagetable, va, 0); /* 查询目标虚拟地址对应的页表项。 */
  if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 || /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      (*pte & required) != (uint64)required) /* 同时拒绝无效页、内核专用页和缺少要求权限的页。 */
    return 0; /* 向调用方报告本次操作成功或返回空值。 */
  return PTE2PA(*pte); /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：逐页校验 R+U 后把用户数据安全复制到内核。 */
int /* 声明下方函数的返回类型与可见范围。 */
copyin(pagetable_t pagetable, void *dst, uint64 src, uint64 len) /* 函数作用：把用户缓冲区逐页复制到内核。 */
{ /* 开始当前结构或代码块。 */
  uchar *target = (uchar *)dst; /* 保存当前尚未写入的目标缓冲区指针。 */

  if (src + len < src) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  while (len > 0) { /* 在剩余数据或条件有效时继续循环处理。 */
    uint64 va0 = PGROUNDDOWN(src); /* 取得当前用户源地址所在页的页首虚拟地址。 */
    uint64 pa = walk_user_address(pagetable, va0, PTE_R); /* 保存翻译得到的物理页首地址。 */
    uint64 amount = PGSIZE - (src - va0); /* 保存本轮不跨页的复制字节数。 */
    if (pa == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
    if (amount > len) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      amount = len; /* 剩余数据不足当前页容量时仅复制剩余长度。 */
    memmove(target, (void *)(pa + src - va0), amount); /* 复制可能跨页或重叠的内存内容。 */
    len -= amount; /* 扣除本轮已经安全读入内核的字节数。 */
    target += amount; /* 推进内核目标指针到下一段空闲位置。 */
    src += amount; /* 推进用户源地址；跨页后下一轮重新校验PTE。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return 0; /* 向调用方报告本次操作成功或返回空值。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：逐页校验 W+U 后把内核数据安全复制到用户空间。 */
int /* 声明下方函数的返回类型与可见范围。 */
copyout(pagetable_t pagetable, uint64 dst, const void *src, uint64 len) /* 函数作用：把内核缓冲区逐页复制到用户空间。 */
{ /* 开始当前结构或代码块。 */
  const uchar *source = (const uchar *)src; /* 保存当前尚未复制的源缓冲区指针。 */

  if (dst + len < dst) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
  while (len > 0) { /* 在剩余数据或条件有效时继续循环处理。 */
    uint64 va0 = PGROUNDDOWN(dst); /* 取得当前用户目标地址所在页的页首虚拟地址。 */
    uint64 pa = walk_user_address(pagetable, va0, PTE_W); /* 保存翻译得到的物理页首地址。 */
    uint64 amount = PGSIZE - (dst - va0); /* 保存本轮不跨页的复制字节数。 */
    if (pa == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
    if (amount > len) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      amount = len; /* 剩余数据不足当前页容量时仅复制剩余长度。 */
    memmove((void *)(pa + dst - va0), source, amount); /* 复制可能跨页或重叠的内存内容。 */
    len -= amount; /* 扣除本轮已经安全写入用户页的字节数。 */
    source += amount; /* 推进内核源指针到尚未写出的数据。 */
    dst += amount; /* 推进用户目标地址；跨页后下一轮重新校验PTE。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return 0; /* 向调用方报告本次操作成功或返回空值。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：跨页安全复制 NUL 结尾用户字符串。 */
int /* 声明下方函数的返回类型与可见范围。 */
copyinstr(pagetable_t pagetable, char *dst, uint64 src, uint64 max) /* 函数作用：跨页复制NUL结尾的用户字符串。 */
{ /* 开始当前结构或代码块。 */
  uint64 copied = 0; /* 累计已经复制的字节数。 */

  while (copied < max) { /* 在剩余数据或条件有效时继续循环处理。 */
    uint64 va0 = PGROUNDDOWN(src); /* 取得当前字符串字符所在用户页的页首虚拟地址。 */
    uint64 pa = walk_user_address(pagetable, va0, PTE_R); /* 保存翻译得到的物理页首地址。 */
    uint64 available = PGSIZE - (src - va0); /* 保存当前页中仍可检查的字节数。 */
    if (pa == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
    while (available-- > 0 && copied < max) { /* 在剩余数据或条件有效时继续循环处理。 */
      char c = *(char *)(pa + src - va0); /* 仅解引用已验证的内核恒等映射。 */
      dst[copied++] = c; /* 保存当前字符，并同步增加内核目标缓冲区已用长度。 */
      src++; /* 推进用户字符串地址到下一个字符。 */
      if (c == '\0') /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
        return 0; /* 向调用方报告本次操作成功或返回空值。 */
    } /* 结束当前条件、循环或函数代码块。 */
  } /* 结束当前条件、循环或函数代码块。 */
  return -1; /* 向调用方报告参数、权限或资源不足导致的失败。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：构建 UART、PLIC、全部 RAM 与跳板页的内核页表。 */
void /* 声明下方函数的返回类型与可见范围。 */
kvminit(void) /* 函数作用：建立内核SV39页表及设备映射。 */
{ /* 开始当前结构或代码块。 */
  kernel_pagetable = (pagetable_t)kalloc(); /* 向物理页分配器申请一个页面。 */
  if (kernel_pagetable == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
    panic("kvminit: no root page"); /* 检测到不可恢复的内核状态并停止运行。 */
  if (mappages(kernel_pagetable, UART0, PGSIZE, UART0, /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
               PTE_R | PTE_W) < 0) /* UART MMIO只需S态读写，不允许执行或用户访问。 */
    panic("kvminit: uart map failed"); /* 检测到不可恢复的内核状态并停止运行。 */
  if (mappages(kernel_pagetable, PLIC, PLIC_SIZE, PLIC, /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
               PTE_R | PTE_W) < 0) /* PLIC MMIO只需S态读写，映射范围覆盖上下文寄存器。 */
    panic("kvminit: plic map failed"); /* 检测到不可恢复的内核状态并停止运行。 */
  if (mappages(kernel_pagetable, KERNBASE, PHYSTOP - KERNBASE, KERNBASE, /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
               PTE_R | PTE_W | PTE_X) < 0) /* 整段RAM恒等映射；本阶段因无etext边界统一使用RWX。 */
    panic("kvminit: ram map failed"); /* kernel.ld 无 etext，阶段性统一 RWX。 */
  if (mappages(kernel_pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
               PTE_R | PTE_X) < 0) /* 高地址跳板映射为S态只读可执行，和用户页表物理页一致。 */
    panic("kvminit: trampoline map failed"); /* 检测到不可恢复的内核状态并停止运行。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：在当前 hart 启用内核 SV39 页表并刷新 TLB。 */
void /* 声明下方函数的返回类型与可见范围。 */
kvminithart(void) /* 函数作用：在当前hart启用内核页表。 */
{ /* 开始当前结构或代码块。 */
  sfence_vma(); /* 刷新TLB中的旧地址翻译缓存。 */
  w_satp(kernel_satp()); /* 安装新的SV39根页表。 */
  sfence_vma(); /* 刷新TLB中的旧地址翻译缓存。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：编码内核页表的 SV39 satp 值。 */
uint64 /* 声明下方函数的返回类型与可见范围。 */
kernel_satp(void) /* 函数作用：编码内核页表的satp值。 */
{ /* 开始当前结构或代码块。 */
  return MAKE_SATP(kernel_pagetable); /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：编码指定用户页表的 SV39 satp 值。 */
uint64 /* 声明下方函数的返回类型与可见范围。 */
user_satp(pagetable_t pagetable) /* 函数作用：编码用户页表的satp值。 */
{ /* 开始当前结构或代码块。 */
  return MAKE_SATP(pagetable); /* 把当前计算结果或对象地址返回调用方。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：递归按 VA 升序输出有效非叶项与叶项。 */
static void /* 声明下方函数的返回类型与可见范围。 */
dump_level(pagetable_t pagetable, int level, uint64 base) /* 函数作用：递归打印一级页表中的有效表项。 */
{ /* 开始当前结构或代码块。 */
  int i; /* 声明循环下标。 */
  uint64 span = 1ULL << PXSHIFT(level); /* 计算当前页表层级单个表项覆盖的地址跨度。 */

  for (i = 0; i < 512; i++) { /* 按指定边界逐项遍历，保证每一页或每一条目都被处理。 */
    pte_t pte = pagetable[i]; /* 保存当前虚拟地址对应的页表项。 */
    uint64 va = base + (uint64)i * span; /* 保存当前处理的用户虚拟页地址。 */
    uint64 pa; /* 保存翻译得到的物理页首地址。 */
    char perm[5]; /* 保存R、W、X、U四位权限显示字符串。 */
    if ((pte & PTE_V) == 0) /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      continue; /* 跳过当前空洞或无效条目并检查下一项。 */
    pa = PTE2PA(pte); /* 提取下一级页表或叶页面的物理页首地址。 */
    perm[0] = (pte & PTE_R) ? 'r' : '-'; /* 第一字符按PTE_R显示读权限。 */
    perm[1] = (pte & PTE_W) ? 'w' : '-'; /* 第二字符按PTE_W显示写权限。 */
    perm[2] = (pte & PTE_X) ? 'x' : '-'; /* 第三字符按PTE_X显示执行权限。 */
    perm[3] = (pte & PTE_U) ? 'u' : '-'; /* 第四字符按PTE_U显示用户态可访问性。 */
    perm[4] = '\0'; /* 添加字符串终止符，保证printf只输出四位权限。 */
    if (level > 0 && !pte_is_leaf(pte)) { /* 检查当前前置条件，异常或特殊情况进入紧随其后的分支。 */
      printf("L%d va=%p pa=%p perm=----\n", level, va, pa); /* 向串口输出页表或测试诊断信息。 */
      dump_level((pagetable_t)pa, level - 1, va); /* 函数作用：递归打印一级页表中的有效表项。 */
    } else { /* 前一条件不成立时进入备用处理分支。 */
      printf("LEAF va=%p pa=%p perm=%s\n", va, pa, perm); /* 向串口输出页表或测试诊断信息。 */
    } /* 结束当前条件、循环或函数代码块。 */
  } /* 结束当前条件、循环或函数代码块。 */
} /* 结束当前条件、循环或函数代码块。 */

/* 函数作用：按课程 ABI 输出指定页表观测块。 */
void /* 声明下方函数的返回类型与可见范围。 */
dump_pagetable(pagetable_t pagetable) /* 函数作用：按照课程ABI打印完整页表快照。 */
{ /* 开始当前结构或代码块。 */
  printf("DUMP-PAGETABLE begin\n"); /* 向串口输出页表或测试诊断信息。 */
  dump_level(pagetable, 2, 0); /* 函数作用：递归打印一级页表中的有效表项。 */
  printf("DUMP-PAGETABLE end\n"); /* 向串口输出页表或测试诊断信息。 */
} /* 结束当前条件、循环或函数代码块。 */
