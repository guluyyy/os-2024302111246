# Lab3 独立构建文件：只操作chatgpt副本，不覆盖Lab2和课程原始工程。
CC      = riscv64-unknown-elf-gcc       # 把C/汇编源码交叉编译为RISC-V目标文件。
LD      = riscv64-unknown-elf-ld        # 按内核或用户链接脚本生成ELF镜像。
OBJCOPY = riscv64-unknown-elf-objcopy   # 从用户ELF提取可内嵌的平铺二进制。
NM      = riscv64-unknown-elf-nm        # 读取用户链接脚本导出的段边界符号。
QEMU    = qemu-system-riscv64           # 使用QEMU virt机器运行最终裸机内核。

# 内核和用户程序共用裸机编译参数：禁止宿主运行库、PIE和栈保护依赖。
CFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector \
         -fno-pie -I.
UFLAGS = $(CFLAGS)                       # 用户程序沿用相同RISC-V裸机ABI选项。

# 逐项列出内核链接输入；Lab3新增kalloc.o，并保留Lab2控制台、trap和进程模块。
OBJS = \
	kernel/entry.o \
	kernel/start.o \
	kernel/timervec.o \
	kernel/kernelvec.o \
	kernel/trampoline.o \
	kernel/kalloc.o \
	kernel/string.o \
	kernel/console.o \
	kernel/printf.o \
	kernel/plic.o \
	kernel/vm.o \
	kernel/proc.o \
	kernel/syscall.o \
	kernel/trap.o \
	kernel/main.o \
	kernel/userimg.o

# 除Shell与演示程序外，一并内嵌非法访问、跨页复制和内存上限测试。
UPROGS = sh hi spin badecall bufstorm badaccess copycross memcap # 程序名同时作为Shell的exec查找键。

# 重新链接所有内核目标，kernel.ld保证入口与trampoline页对齐。
kernel/kernel: $(OBJS) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -o $@ $(OBJS)

# 任意内核C文件改动后，用统一参数重新生成同路径.o。
%.o: %.c kernel/riscv.h kernel/course_sid.h
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

# 任意内核汇编文件改动后，使用预处理汇编模式生成.o。
%.o: %.S
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

# 课程赠送的 usys.pl 生成用户态系统调用桩。
user/usys.S: user/usys.pl
	perl user/usys.pl > user/usys.S

# 用户程序链接为基址 0 的平铺二进制。普通程序在 user/，官方测试在
# tests/；本规则只选择源文件位置，不改动任何赠送源码。
user-flat/%.bin: user/user.h user/user-lab3.ld user/ulib.c user/printf.c user/usys.S
	mkdir -p user-flat
	$(CC) $(UFLAGS) -Iuser -c user/ulib.c   -o user-flat/ulib.o
	$(CC) $(UFLAGS) -Iuser -c user/printf.c -o user-flat/printf.o
	$(CC) $(UFLAGS) -Iuser -c user/usys.S   -o user-flat/usys.o
	@src=user/$*.c; \
	 if [ ! -f $$src ]; then src=tests/$*.c; fi; \
	 test -f $$src || { echo "missing user source for $*"; exit 1; }; \
	 $(CC) $(UFLAGS) -Iuser -c $$src -o user-flat/$*.o
	$(LD) -T user/user-lab3.ld -o user-flat/$*.elf \
		user-flat/$*.o user-flat/ulib.o user-flat/printf.o user-flat/usys.o
	$(OBJCOPY) -O binary user-flat/$*.elf $@

# 课程约定的内嵌程序表。每项为 start、end、name，程序二进制紧随
# 条目；下一项位于 end 向上按 8 字节对齐的位置，以两个 0 结束。
kernel/userimg.S: Makefile $(addprefix user-flat/,$(UPROGS:=.bin))
	printf '.section .rodata\n .global _uprog_table\n_uprog_table:\n' > $@.tmp
	for p in $(UPROGS); do \
	  data_start=`$(NM) -n user-flat/$$p.elf | awk '$$3 == "__user_data_start" { print "0x" $$1 }'`; \
	  image_end=`$(NM) -n user-flat/$$p.elf | awk '$$3 == "__user_image_end" { print "0x" $$1 }'`; \
	  test -n "$$data_start" -a -n "$$image_end" || { echo "missing lab3 segment symbols for $$p"; exit 1; }; \
	  printf ' .quad _uprog_%s_start\n .quad _uprog_%s_end\n .quad %s\n .quad %s\n .asciz "%s"\n .balign 8\n' \
	    $$p $$p $$data_start $$image_end $$p >> $@.tmp; \
	  printf ' .global _uprog_%s_start\n_uprog_%s_start:\n .incbin "user-flat/%s.bin"\n .global _uprog_%s_end\n_uprog_%s_end:\n .balign 8\n' \
	    $$p $$p $$p $$p $$p >> $@.tmp; \
	done
	printf ' .quad 0\n .quad 0\n' >> $@.tmp
	mv $@.tmp $@

# 验收入口保持 make qemu；单核与 128 MiB 内存使地址布局确定。
# qemu目标固定128MiB与单核，确保个人内存配额和页表输出可重复。
qemu: kernel/kernel
	$(QEMU) -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 -nographic

# clean只删除本副本可重新生成的对象、内核镜像、程序表和用户平铺目录。
clean:
	rm -f kernel/*.o kernel/kernel kernel/userimg.S user/usys.S
	rm -rf user-flat

.PHONY: qemu clean                       # 声明动作目标，避免同名文件阻止执行。
