# Lab2 实现与运行说明

## 目录用途

本目录是独立的 Lab2 代码树，不会修改 `lab2`、`chatgpt/os_code` 或
`xv6-riscv` 中的原始文件。课程赠送的 `trampoline.S`、用户库、用户
程序、系统调用号和测试源码均保持原样。

## 已实现内容

- 保留 Lab1 个性化 Banner 和 printf 边界自检。
- 使用课程 trampoline ABI 完成 U 态陷入、trapframe 保存、系统调用
  处理和 `sret` 返回。
- 建立最小 Sv39 用户页表，将用户虚拟地址 0 映射到固定物理执行区，
  同时映射活动 trapframe 和 trampoline。
- 实现 `fork`、`exec`、`wait`、`exit`、`read`、`write`、`getpid`、
  `pause` 和 `uptime`；未知或未实现系统调用稳定返回 `-1`。
- 实现 UART 接收中断、PLIC、96 字节环形输入缓冲和个人参数指定的
  行缓冲语义。
- 使用兼容旧版 QEMU 的 CLINT M 态定时器，并向 S 态转发软件中断。
- 从 `_uprog_table` 装载 `sh`、`hi`、`spin`、`badecall`、`bufstorm`。

## 编译与运行

在 WSL 中执行：

```bash
cd "/mnt/c/Users/19288/Desktop/OS实践/chatgpt/lab2/os_code"
make clean
make qemu
```

看到 `sh>` 后可依次输入：

```text
hi
badecall
bufstorm
spin
```

退出 QEMU 使用 `Ctrl+A`，松开后按 `X`。

## 自动测试

```bash
python3 "/mnt/c/Users/19288/Desktop/OS实践/chatgpt/code/lab2/run_lab2_tests.py" \
  --tree "/mnt/c/Users/19288/Desktop/OS实践/chatgpt/lab2/os_code"
```

脚本会检查 Shell 启动、`hi` 的 fork/exec/wait 往返、非法调用号、四行
输入边界、`spin` 运行以及忙循环期间的 UART 中断回显。

## 当前阶段的明确限制

- 只有一个 CPU，尚无通用抢占式调度器。
- 进程切换通过固定用户执行区和整段内存快照完成，适用于本实验的小型
  用户程序，不是高性能方案。
- `exec` 暂时忽略 argv。
- 输入为空时 `read` 忙等，Lab4 应改为 sleep/wakeup。
- 用户页统一使用 RWX 权限，细粒度代码/数据/栈权限留给 Lab3。

## 源码注释范围

- 我编写的内核行为源码已按有效代码行补充中文注释，包括启动、trap、
  系统调用、进程、控制台、PLIC、定时器、页表、字符串和内核打印代码。
- 课程明确标为赠送件的 `trampoline.S`、用户库、用户程序、固定接口头
  和官方测试保持原样，不因注释要求修改。
- `user/usys.S`、`kernel/userimg.S`、`user-flat/`、`kernel/*.o` 和最终
  `kernel/kernel` 属于生成文件或构建产物，不人工维护逐行注释。
- 可在 Windows PowerShell 中运行
  `chatgpt/code/lab2/check_line_comments.ps1` 复核自编源码注释覆盖。
