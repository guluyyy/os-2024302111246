/*
 * Lab1 最早期启动入口。
 *
 * QEMU virt 机器会把内核加载到物理地址 0x80000000，然后跳到链接脚本
 * 指定的 _entry。此时 CPU 仍处于 M 态，没有 C 运行时，也不能假设栈和
 * .bss 已经准备完成。因此，本文件只做进入 C 代码前不可缺少的工作：
 *
 *   1. 关闭 M 态全局中断，避免初始化过程被异步事件打断；
 *   2. 安装一个四字节对齐的早期陷阱入口；
 *   3. 只允许 hart 0 启动，其他 hart 停在 park；
 *   4. 清零 .bss；
 *   5. 建立按学号参数确定大小的初始内核栈；
 *   6. 调用 start() 完成 M 态到 S 态的切换。
 */

#include "course_sid.h"

    .section .text
    .globl _entry
    .align 2
_entry:
    /* mstatus.MIE 是第 3 位。启动阶段必须保持 M 态全局中断关闭。 */
    csrci mstatus, 8

    /*
     * mtvec 的低两位是模式位，Direct 模式要求入口地址至少四字节对齐。
     * 在完整陷阱系统建立前，任何意外异常都停在 early_trap，便于从
     * QEMU 的 -d int 日志中定位，而不是跳到未定义地址。
     */
    la      t0, early_trap
    csrw    mtvec, t0

    /*
     * 当前实验只有一套启动栈和一条初始化路径。多个 hart 同时继续会
     * 破坏栈与串口输出，因此仅 hart 0 启动，其余 hart 永久停放。
     */
    csrr    t0, mhartid
    bnez    t0, park

    /*
     * 链接脚本把 entry.o 放在最前面，所以 bss_start 是整个 .bss 的
     * 起点；kernel_end 是链接脚本给出的内核末地址。这里逐字节清零，
     * 不依赖地址是否按 8 字节对齐，也不需要使用尚未建立的栈。
     */
    la      t0, bss_start
    la      t1, kernel_end
.Lclear_bss:
    bgeu    t0, t1, .Lbss_ready
    sb      zero, 0(t0)
    addi    t0, t0, 1
    j       .Lclear_bss

.Lbss_ready:
    /* RISC-V ABI 要求函数调用边界上的 sp 保持 16 字节对齐。 */
    la      sp, boot_stack_top

    /* start() 在 M 态运行，成功后通过 mret 进入 S 态的 main()。 */
    call    start

    /* start() 按设计不会返回；若意外返回，安全地停放当前 hart。 */
    j       park

/* 非主 hart 和不可继续执行的路径统一停在这里。 */
park:
    wfi
    j       park

/*
 * M 态和早期 S 态共用的最小陷阱落点。这里故意不尝试恢复执行：
 * Lab1 尚未实现完整 trapframe，停住比破坏现场后继续运行更安全。
 */
    .balign 4
    .globl early_trap
early_trap:
    j       early_trap

    .section .bss
    .balign 16

/* .bss 清零范围的起点，由 _entry 在尚未使用栈时处理。 */
    .globl bss_start
bss_start:

/*
 * 初始内核栈大小来自 course_sid.h。当前学号对应 LAB1_STACK_KB=8，
 * 但源码保留公式，不能把 8192 作为个人参数硬编码到实现逻辑中。
 */
    .globl boot_stack
boot_stack:
    .space LAB1_STACK_KB * 1024

    .balign 16
    .globl boot_stack_top
boot_stack_top:

