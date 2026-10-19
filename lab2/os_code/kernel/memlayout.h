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

/* 用户程序的实际物理执行区；进程切换时由 proc.c 保存/恢复快照。 */
#define USER_BASE     0x81000000L
#define USER_MEM_SIZE (256 * 1024L)

/*
 * 用户程序按虚拟地址 0 链接。最小 Sv39 页表把 [0, USER_MEM_SIZE) 映射
 * 到 USER_BASE，并把高地址 TRAPFRAME 映射到下面的活动物理页。
 */
#define ACTIVE_TRAPFRAME_PA (USER_BASE + USER_MEM_SIZE)
#define TRAMPOLINE          (MAXVA - PGSIZE)
#define TRAPFRAME           (TRAMPOLINE - PGSIZE)
