/* Lab5 综合自测：覆盖数据隔离、并发回收、非法写、个人预算与 exec 参数。 */
#include "kernel/types.h"              /* 引入 uint64 等基本类型。 */
#include "kernel/course_sid.h"         /* 引入本人 LAB5_COW_BUDGET=142。 */
#include "user/user.h"                 /* 引入 fork、wait、sbrk、exec 和 pmc。 */

#define PGSIZE 4096                     /* 用户自测按 4 KiB 物理页处理。 */
#define CONCURRENT_CHILDREN 4           /* 同时存活的子进程数。 */

/* 函数作用：输出失败原因并以非零状态终止自测。 */
static void
fail(const char *reason)
{
  printf("COWTEST FAIL: %s\n", reason); /* 保留便于现场定位的子项名。 */
  exit(1);                              /* 任一核心子项失败即不得报总 PASS。 */
}

/* 函数作用：验证父子反复写同一逻辑页后数据互不污染。 */
static void
test_single_page(void)
{
  char *page = sbrk(PGSIZE);            /* 为父进程申请一个可写堆页。 */
  int status = -1;                      /* 保存子进程退出状态。 */
  int pid;                              /* fork 返回的父子分支标识。 */

  if (page == SBRK_ERROR)
    fail("single-page sbrk");          /* 没有测试页时无法继续。 */
  page[0] = 0x11;                      /* fork 前建立父子共享的初始数据。 */
  pid = fork();                        /* fork 应只建 COW 映射，不立即拷贝数据页。 */
  if (pid < 0)
    fail("single-page fork");          /* 进程或页表资源不足。 */
  if (pid == 0) {
    int round;                          /* 子进程多轮写入下标。 */
    for (round = 0; round < 8; round++)
      page[0] = (char)(0x20 + round);   /* 首次写触发拷贝，之后在私有页上直接写。 */
    if ((unsigned char)page[0] != 0x27)
      exit(2);                          /* 子进程私有页回读失败。 */
    exit(0);                            /* 子进程数据隔离通过。 */
  }
  page[0] = 0x55;                      /* 父进程写同一虚拟地址，应获得自己的页。 */
  if (wait(&status) < 0 || status != 0)
    fail("single-page child");         /* 子进程必须正常结束。 */
  if ((unsigned char)page[0] != 0x55)
    fail("single-page isolation");     /* 子进程的 0x27 不得覆盖父进程的 0x55。 */
  if (sbrk(-PGSIZE) == SBRK_ERROR)
    fail("single-page shrink");        /* 用完后归还测试页。 */
  printf("COWTEST PASS: single-page isolation\n"); /* 报告单页互斥写通过。 */
}

/* 函数作用：验证多个并发子进程退出后共享引用恢复到基线。 */
static void
test_concurrent_reclaim(void)
{
  char *page = sbrk(PGSIZE);            /* 所有子进程将从这一堆页建立 COW。 */
  unsigned long long before;            /* fork 前的全局共享页基线。 */
  int i;                                /* 创建子进程与回收子进程的下标。 */

  if (page == SBRK_ERROR)
    fail("concurrent sbrk");           /* 测试页分配失败。 */
  page[0] = 1;                         /* 使页包含可观测初值。 */
  before = pmc(1);                     /* 记录本测试之外的共享页数。 */
  for (i = 0; i < CONCURRENT_CHILDREN; i++) {
    int pid = fork();                  /* 连续建立多个存活子进程。 */
    if (pid < 0)
      fail("concurrent fork");         /* 任一子进程创建失败即不满足并发覆盖。 */
    if (pid == 0) {
      page[0] = (char)(10 + i);        /* 每个子进程都触发自己的私有拷贝。 */
      if (page[0] != (char)(10 + i))
        exit(3);                        /* 私有页回读必须与当前子进程一致。 */
      exit(0);                          /* 退出路径应递减所有共享引用。 */
    }
  }
  for (i = 0; i < CONCURRENT_CHILDREN; i++) {
    int status = -1;                    /* 逐个检查子进程退出状态。 */
    if (wait(&status) < 0 || status != 0)
      fail("concurrent wait");         /* 不允许子进程丢失或异常终止。 */
  }
  if (pmc(1) != before)
    fail("shared-page leak");          /* 全部子进程回收后 refcnt>1 页数必须回到基线。 */
  if (sbrk(-PGSIZE) == SBRK_ERROR)
    fail("concurrent shrink");         /* 归还父进程剩余的测试页。 */
  printf("COWTEST PASS: concurrent reclaim\n"); /* 报告多子进程引用回收通过。 */
}

/* 函数作用：试图写只读代码页，验证它不被误当成 COW。 */
static void
test_text_write_killed(void)
{
  int pid = fork();                    /* 将危险写入限制在子进程。 */
  int status = 0;                      /* 期望内核以 -1 终止非法写入者。 */

  if (pid < 0)
    fail("text-write fork");           /* 无法创建隔离测试子进程。 */
  if (pid == 0) {
    volatile uint64 text_address = 0;  /* 本程序 VA 0 是 R-XU 代码页。 */
    *(volatile char *)text_address = 1; /* 应产生 store page fault，但 PTE 不带 COW。 */
    exit(4);                            /* 若执行到此处，说明内核错误放行代码写。 */
  }
  if (wait(&status) < 0 || status != -1)
    fail("text-write not killed");     /* 犯错进程必须终止且父进程仍能运行。 */
  printf("COWTEST PASS: illegal text write isolated\n"); /* 报告权限与 COW 区分正确。 */
}

/* 函数作用：制造超过个人阈值的 COW 拷贝，验证提示后仍继续正确运行。 */
static void
test_budget(void)
{
  int pages = LAB5_COW_BUDGET + 2;     /* 明确越过 142 次的个人预算线。 */
  int bytes = pages * PGSIZE;          /* 可写堆区总字节数。 */
  char *area = sbrk(bytes);             /* 在 fork 前一次性建立全部可写页。 */
  unsigned long long before;            /* 记录本子项前的全局拷贝数。 */
  unsigned long long after;             /* 记录子进程写完后的累计值。 */
  int i;                                /* 每页触发一次写故障的下标。 */
  int pid;                              /* 预算测试子进程 PID。 */
  int status = -1;                      /* 子进程退出状态。 */

  if (area == SBRK_ERROR)
    fail("budget sbrk");              /* 未能建立足够 COW 页。 */
  for (i = 0; i < pages; i++)
    area[i * PGSIZE] = (char)i;        /* 在 fork 前实体化并初始化每个页。 */
  before = pmc(0);                     /* 建立本子项的拷贝计数基线。 */
  pid = fork();                        /* 将所有可写页变为父子 COW 共享。 */
  if (pid < 0)
    fail("budget fork");              /* 无法创建故障发生者。 */
  if (pid == 0) {
    for (i = 0; i < pages; i++)
      area[i * PGSIZE] ^= 0x5a;        /* 每页首次写都需真正分配和拷贝。 */
    exit(0);                            /* 即使超预算也应继续至正常退出。 */
  }
  if (wait(&status) < 0 || status != 0)
    fail("budget child");              /* 预算仅降级提示，不得杀掉子进程。 */
  after = pmc(0);                      /* 取得完成后的全局计数。 */
  if (after - before < (unsigned long long)pages)
    fail("budget copy count");         /* 每个堆页都必须至少实际拷贝一次。 */
  if (sbrk(-bytes) == SBRK_ERROR)
    fail("budget shrink");             /* 归还大块测试内存。 */
  printf("COWTEST PASS: budget downgrade copies=%llu\n", after - before); /* 报告预算路径通过。 */
}

/* 函数作用：验证 exec 成功参数栈与非法 argv 指针的安全失败。 */
static void
test_exec_arguments(void)
{
  int pid = fork();                    /* 成功 exec 会替换地址空间，因此在子进程中测试。 */
  int status = -1;                     /* 保存 echo 或非法指针子进程的状态。 */

  if (pid < 0)
    fail("exec fork");                /* 无法创建 exec 测试进程。 */
  if (pid == 0) {
    char *argv[] = { "echo", "cowtest", "argv", 0 }; /* 预期 argc=3 且带 NULL 哨兵。 */
    exec("echo", argv);                /* 成功后由 echo 校验 16B 对齐并退出 0。 */
    exit(5);                            /* exec 返回说明装载或参数栈失败。 */
  }
  if (wait(&status) < 0 || status != 0)
    fail("exec argv");                 /* echo 必须报 T5-3 PASS 并正常退出。 */

  pid = fork();                        /* 再建一个子进程验证非法 argv。 */
  if (pid < 0)
    fail("bad argv fork");            /* 无法建立非法指针隔离测试。 */
  if (pid == 0) {
    char **bad = (char **)(uint64)0x4000000000ULL; /* MAXVA 处不属于用户 SV39 映射。 */
    if (exec("echo", bad) == -1)
      exit(0);                          /* 内核经 copyin 拒绝非法 argv 即通过。 */
    exit(6);                            /* 非法指针不得被接受。 */
  }
  if (wait(&status) < 0 || status != 0)
    fail("invalid argv accepted");     /* 非法 argv 子进程必须以0报告安全拒绝。 */
  printf("COWTEST PASS: exec argv and invalid pointer\n"); /* 报告 exec 两类路径通过。 */
}

/* 函数作用：按实验手册顺序运行全部 Lab5 自测子项。 */
int
main(void)
{
  printf("COWTEST begin\n");           /* 标记综合自测开始。 */
  test_single_page();                  /* 单页多轮写与数据隔离。 */
  test_concurrent_reclaim();           /* 多子进程共享与引用回收。 */
  test_text_write_killed();            /* 只读代码写必须终止当前进程。 */
  test_budget();                       /* 个人 142 次预算达线后继续执行。 */
  test_exec_arguments();               /* exec argc/argv 对齐与非法指针。 */
  printf("COWTEST ALL PASS\n");        /* 只在所有实验要求都通过时输出。 */
  exit(0);                              /* 返回 Shell，证明内核仍然存活。 */
}
