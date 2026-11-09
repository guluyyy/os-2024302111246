/* ============ 预置支撑代码 · 请勿修改 ============
 * 说明: QEMU virt 机器地址地图(环境事实)
 * 修改该支撑代码可能破坏统一接口规范；若遇到问题建议通过 git diff 核对本段代码。
 * ======================================== */
// Physical memory layout

// qemu -machine virt is set up like this,
// based on qemu's hw/riscv/virt.c:
//
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- CLINT
// 0C000000 -- PLIC
// 10000000 -- uart0
// 10001000 -- virtio disk
// 80000000 -- qemu's boot ROM loads the kernel here,
//             then jumps here.
// unused RAM after 80000000.

// the kernel uses physical memory thus:
// 80000000 -- entry.S, then kernel text and data
// end -- start of kernel page allocation area
// PHYSTOP -- end RAM used by the kernel

// qemu puts UART registers here in physical memory.
#define UART0     0x10000000L
#define UART0_IRQ 10

// virtio mmio interface
#define VIRTIO0     0x10001000
#define VIRTIO0_IRQ 1

// core-local interrupt controller (CLINT)
#define CLINT_BASE  0x02000000L
#define CLINT(hart) (CLINT_BASE + (hart) * 4)
#define CLINT_MTIMECMP(hart) (CLINT_BASE + 0x4000 + (hart) * 8)
#define CLINT_MTIME           (CLINT_BASE + 0xbff8)

// qemu puts platform-level interrupt controller (PLIC) here.
#define PLIC                 0x0c000000L
#define PLIC_PRIORITY        (PLIC + 0x0)
#define PLIC_PENDING         (PLIC + 0x1000)
#define PLIC_SENABLE(hart)   (PLIC + 0x2080 + (hart) * 0x100)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart) * 0x2000)
#define PLIC_SCLAIM(hart)    (PLIC + 0x201004 + (hart) * 0x2000)

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x80000000 to PHYSTOP.
#define KERNBASE 0x80000000L
#define PHYSTOP  (KERNBASE + 128 * 1024 * 1024)

/* PLIC 的 S 态上下文寄存器位于基址后约 2MB，本实验统一映射 4MB。 */
#define PLIC_SIZE 0x400000L /* 覆盖优先级、使能和S态claim/complete寄存器范围。 */

/*
 * Lab3 用户地址空间布局：
 *
 *   0x00000000  代码与只读数据（R-XU）
 *                已初始化数据与 BSS（RW-U）
 *                LAB3_GUARD_PAGES 个未映射保护页
 *                1 个用户栈页（RW-U，向低地址增长）
 *                堆起点，sbrk 从这里向高地址增长
 *   ...
 *   TRAPFRAME    每进程独立陷入帧（RW--）
 *   TRAMPOLINE   用户与内核页表同址映射的跳板页（R-X-）
 *
 * 保护页不分配物理页，也不建立 PTE；用户栈向下越界时由硬件产生页故障。
 */
#define USER_STACK_PAGES 1L                  /* 为每个用户程序建立一个4KiB初始栈页。 */
#define TRAMPOLINE       (MAXVA - PGSIZE)    /* 把跳板固定在用户地址空间最高页。 */
#define TRAPFRAME        (TRAMPOLINE - PGSIZE) /* 把每进程陷阱帧固定在跳板下一页。 */

/* Lab0 图纸使用的高地址内核栈位置；本实现保留宏但采用 PCB 内静态栈。 */
#define KSTACK(index) (TRAPFRAME - ((index) + 1) * 2 * PGSIZE) /* 计算带保护间隔的候选高地址内核栈。 */
