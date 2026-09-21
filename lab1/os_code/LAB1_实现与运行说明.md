# Lab1 实现与运行说明

## 目录性质

本目录是从个人基线复制出的独立实现，不会修改 `baselines/2024302111246-kernel`。核心实现位于 `kernel` 下的五个文件：

- `entry.S`：M 态最早期入口、多 hart 筛选、清零 `.bss`、建立 8 KB 栈和设置 `mtvec`。
- `start.c`：设置 `mstatus`、`mepc`、异常委托、PMP 和 Bare 模式，执行 `mret` 进入 S 态。
- `console.c`：初始化 UART 16550、轮询 THRE、实现个人协议 1 和按学号派生的节流。
- `printf.c`：实现 `%d`、`%x`、`%s`、`%c` 和 `%%`，覆盖零、负数和空字符串边界。
- `main.c`：输出个人 Banner，执行 printf 边界自检，然后稳定停在 `wfi`。

`expect_banner.txt` 是 QEMU 串口输出的逐字节期望文件，由 `chatgpt/lab1_tools/generate_expect.py` 根据 `course_sid.h` 生成。

## 编译和运行位置

请在 WSL 终端进入本目录，而不是进入 `kernel` 子目录：

```bash
cd "/mnt/c/Users/19288/Desktop/OS实践/chatgpt/os_code"
make clean
make
make qemu
```

运行时按 `Ctrl+A`，再按 `X` 退出 QEMU。

## 完整自动验收

测试脚本在独立中间目录编译，不会向本目录写入 `.o` 或内核 ELF：

```bash
bash "/mnt/c/Users/19288/Desktop/OS实践/chatgpt/lab1_tools/run_lab1_tests.sh"
```

该脚本依次执行：

1. 重新生成 `expect_banner.txt`；
2. 复制源码到 `chatgpt/lab1_intermediate/build_tree`；
3. 使用 RISC-V GCC 在 `-Wall -Werror` 下编译；
4. 启动 QEMU 并捕获 UART 原始字节；
5. 逐字节比较实际输出和期望输出；
6. 检查 `-d int` 日志为空；
7. 运行课程自带的 `check_expect.py`；
8. 测试成功后删除临时构建树，只保留日志和检查结果。

## 不得修改的预置文件

以下文件保持个人基线原样：

- `Makefile`
- `kernel/kernel.ld`
- `kernel/riscv.h`
- `kernel/types.h`
- `kernel/memlayout.h`
- `kernel/course_sid.h`

