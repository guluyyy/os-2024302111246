        #
        # 兼容旧 QEMU 的 M 态 CLINT 时钟入口。
        # 不使用 sp；mscratch 指向 start.c 预留的五个 64 位槽：前三个保存
        # a1-a3，后两个分别保存 mtimecmp 地址与下一次中断间隔。
        #
        .section .text                 # 把定时器入口放入内核可执行代码段。
        .align 4                       # 按 2^4=16 字节对齐入口，满足 mtvec 直接模式要求。
        .globl timervec                # 导出符号，供 start.c 写入 mtvec。
        # 入口作用：重设下一次 CLINT 时钟，并把计时事件转发为 S 态软件中断。
timervec:                              # M 态机器时钟中断从这里开始执行。
        csrrw a0, mscratch, a0         # 交换 a0 与 mscratch，使 a0 指向五槽临时保存区。
        sd a1, 0(a0)                   # 把被中断代码原来的 a1 保存到临时槽 0。
        sd a2, 8(a0)                   # 把被中断代码原来的 a2 保存到临时槽 1。
        sd a3, 16(a0)                  # 把被中断代码原来的 a3 保存到临时槽 2。

        ld a1, 24(a0)                  # 从临时槽 3 取出 CLINT mtimecmp 寄存器地址。
        ld a2, 32(a0)                  # 从临时槽 4 取出相邻两次中断的时间间隔。
        ld a3, 0(a1)                   # 读取当前 mtimecmp 比较值作为本次调度基准。
        add a3, a3, a2                 # 把比较值推进一个固定间隔，计算下次到期时刻。
        sd a3, 0(a1)                   # 写回 mtimecmp，清除本次机器时钟条件并预约下一次中断。

        # 置起 S 态软件中断，真正的 ticks 维护在 trap.c 完成。
        li a1, 2                       # 构造 sip.SSIP 位掩码，即 1 左移 1 位。
        csrw sip, a1                   # 置起 S 态软件中断，把计时工作转交给 trap.c。

        ld a1, 0(a0)                   # 从临时槽 0 恢复被中断代码原来的 a1。
        ld a2, 8(a0)                   # 从临时槽 1 恢复被中断代码原来的 a2。
        ld a3, 16(a0)                  # 从临时槽 2 恢复被中断代码原来的 a3。
        csrrw a0, mscratch, a0         # 再次交换，恢复原 a0 并让 mscratch 重新保存临时区地址。
        mret                            # 恢复机器中断前的特权级与 PC，返回被中断代码。
