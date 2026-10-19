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

#include "course_sid.h"               /* 引入个人初始内核栈大小宏。 */

    .section .text                     # 把后续启动指令放入内核代码段。
    .globl _entry                      # 导出链接脚本 ENTRY 使用的入口符号。
    .align 2                           # 按 2^2=4 字节对齐上电入口。
# 入口作用：完成 C 运行时建立前的最早期硬件线程与栈初始化。
_entry:                                # QEMU 跳转到内核后的第一条执行路径。
    /* mstatus.MIE 是第 3 位。启动阶段必须保持 M 态全局中断关闭。 */
    csrci mstatus, 8                   # 清除 mstatus.MIE，关闭 M 态全局中断。

    /*
     * mtvec 的低两位是模式位，Direct 模式要求入口地址至少四字节对齐。
     * 在完整陷阱系统建立前，任何意外异常都停在 early_trap，便于从
     * QEMU 的 -d int 日志中定位，而不是跳到未定义地址。
     */
    la      t0, early_trap             # 把早期异常自旋入口地址装入 t0。
    csrw    mtvec, t0                  # 让初始化期间的 M 态 trap 落到 early_trap。

    /*
     * 当前实验只有一套启动栈和一条初始化路径。多个 hart 同时继续会
     * 破坏栈与串口输出，因此仅 hart 0 启动，其余 hart 永久停放。
     */
    csrr    t0, mhartid                # 读取当前硬件线程编号。
    bnez    t0, park                   # 非 hart 0 直接跳到永久停放循环。

    /*
     * 链接脚本把 entry.o 放在最前面，所以 bss_start 是整个 .bss 的
     * 起点；kernel_end 是链接脚本给出的内核末地址。这里逐字节清零，
     * 不依赖地址是否按 8 字节对齐，也不需要使用尚未建立的栈。
     */
    la      t0, bss_start              # 令 t0 指向下一个待清零的 .bss 字节。
    la      t1, kernel_end             # 令 t1 指向内核静态区域末尾。
.Lclear_bss:                           # 逐字节清零循环入口。
    bgeu    t0, t1, .Lbss_ready        # t0 到达末尾时结束清零。
    sb      zero, 0(t0)                # 向当前 .bss 地址写入一个零字节。
    addi    t0, t0, 1                  # 前进到下一个待清零字节。
    j       .Lclear_bss                # 返回循环顶部继续比较和清零。

.Lbss_ready:                           # 整段 .bss 已初始化为零后到达此处。
    /* RISC-V ABI 要求函数调用边界上的 sp 保持 16 字节对齐。 */
    la      sp, boot_stack_top         # 把初始栈指针设为 16 字节对齐的栈顶。

    /* start() 在 M 态运行，成功后通过 mret 进入 S 态的 main()。 */
    call    start                      # 使用已建立的栈调用 M 态 C 启动函数。

    /* start() 按设计不会返回；若意外返回，安全地停放当前 hart。 */
    j       park                       # start 异常返回时转入安全停放路径。

/* 非主 hart 和不可继续执行的路径统一停在这里。 */
# 入口作用：永久停放非主 hart 或无法继续启动的执行路径。
park:                                  # 非主核和不可恢复路径的共同停放入口。
    wfi                                # 等待中断以降低永久循环的资源消耗。
    j       park                       # 被唤醒后仍返回 park，不参与内核初始化。

/*
 * M 态和早期 S 态共用的最小陷阱落点。这里故意不尝试恢复执行：
 * Lab1 尚未实现完整 trapframe，停住比破坏现场后继续运行更安全。
 */
    .balign 4                          # 保证 mtvec Direct 模式要求的 4 字节对齐。
    .globl early_trap                  # 导出早期 M/S 态异常落点。
# 入口作用：在完整 trap 系统建立前捕获异常并保留现场供日志诊断。
early_trap:                            # 完整 trap 系统建立前的不可恢复入口。
    j       early_trap                 # 永久自旋，保留 QEMU -d int 中的异常现场。

    .section .bss                      # 把启动栈放入无需存储初值的零初始化段。
    .balign 16                         # 保证随后栈存储区首地址满足 ABI 对齐。

/* .bss 清零范围的起点，由 _entry 在尚未使用栈时处理。 */
    .globl bss_start                   # 导出供清零循环使用的 .bss 起始符号。
bss_start:                             # 标记本文件及后续对象的 .bss 清零起点。

/*
 * 初始内核栈大小来自 course_sid.h。当前学号对应 LAB1_STACK_KB=8，
 * 但源码保留公式，不能把 8192 作为个人参数硬编码到实现逻辑中。
 */
    .globl boot_stack                  # 导出初始栈底符号供调试查看。
boot_stack:                            # 标记 hart 0 初始内核栈的最低地址。
    .space LAB1_STACK_KB * 1024        # 按个人参数在 .bss 中预留栈空间。

    .balign 16                         # 再次确保最终栈顶满足 16 字节对齐。
    .globl boot_stack_top              # 导出 _entry 设置 sp 使用的栈顶符号。
boot_stack_top:                        # 标记向下增长的初始内核栈最高地址。

