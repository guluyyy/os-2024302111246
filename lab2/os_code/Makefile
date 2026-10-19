# Lab2 独立构建文件。
CC      = riscv64-unknown-elf-gcc
LD      = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
QEMU    = qemu-system-riscv64

CFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector \
         -fno-pie -I.
UFLAGS = $(CFLAGS)

OBJS = \
	kernel/entry.o \
	kernel/start.o \
	kernel/timervec.o \
	kernel/kernelvec.o \
	kernel/trampoline.o \
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

# 除 Shell 与两个演示程序外，一并内嵌课程官方测试。
UPROGS = sh hi spin badecall bufstorm

kernel/kernel: $(OBJS) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -o $@ $(OBJS)

%.o: %.c kernel/riscv.h kernel/course_sid.h
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

# 课程赠送的 usys.pl 生成用户态系统调用桩。
user/usys.S: user/usys.pl
	perl user/usys.pl > user/usys.S

# 用户程序链接为基址 0 的平铺二进制。普通程序在 user/，官方测试在
# tests/；本规则只选择源文件位置，不改动任何赠送源码。
user-flat/%.bin: user/user.h user/ulib.c user/printf.c user/usys.S
	mkdir -p user-flat
	$(CC) $(UFLAGS) -Iuser -c user/ulib.c   -o user-flat/ulib.o
	$(CC) $(UFLAGS) -Iuser -c user/printf.c -o user-flat/printf.o
	$(CC) $(UFLAGS) -Iuser -c user/usys.S   -o user-flat/usys.o
	@src=user/$*.c; \
	 if [ ! -f $$src ]; then src=tests/$*.c; fi; \
	 test -f $$src || { echo "missing user source for $*"; exit 1; }; \
	 $(CC) $(UFLAGS) -Iuser -c $$src -o user-flat/$*.o
	$(LD) -T user/user.ld -o user-flat/$*.elf \
		user-flat/$*.o user-flat/ulib.o user-flat/printf.o user-flat/usys.o
	$(OBJCOPY) -O binary user-flat/$*.elf $@

# 课程约定的内嵌程序表。每项为 start、end、name，程序二进制紧随
# 条目；下一项位于 end 向上按 8 字节对齐的位置，以两个 0 结束。
kernel/userimg.S: Makefile $(addprefix user-flat/,$(UPROGS:=.bin))
	printf '.section .rodata\n .global _uprog_table\n_uprog_table:\n' > $@.tmp
	for p in $(UPROGS); do \
	  printf ' .quad _uprog_%s_start\n .quad _uprog_%s_end\n .asciz "%s"\n .balign 8\n' \
	    $$p $$p $$p >> $@.tmp; \
	  printf ' .global _uprog_%s_start\n_uprog_%s_start:\n .incbin "user-flat/%s.bin"\n .global _uprog_%s_end\n_uprog_%s_end:\n .balign 8\n' \
	    $$p $$p $$p $$p $$p >> $@.tmp; \
	done
	printf ' .quad 0\n .quad 0\n' >> $@.tmp
	mv $@.tmp $@

# 验收入口保持 make qemu；单核与 128 MiB 内存使地址布局确定。
qemu: kernel/kernel
	$(QEMU) -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 -nographic

clean:
	rm -f kernel/*.o kernel/kernel kernel/userimg.S user/usys.S
	rm -rf user-flat

.PHONY: qemu clean
