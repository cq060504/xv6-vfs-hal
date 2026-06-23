K=kernel
U=user

# Target architecture, default riscv, optional loongarch
ARCH ?= riscv
HAL = hal
VIRTIO_NDISK ?= 1

# Platform-independent kernel objects
K_OBJS = \
  $K/console.o \
  $K/printf.o \
  $K/kalloc.o \
  $K/spinlock.o \
  $K/string.o \
  $K/main.o \
  $K/vm.o \
  $K/proc.o \
  $K/trap.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/bio.o \
  $K/fs.o \
  $K/log.o \
  $K/sleeplock.o \
  $K/file.o \
  $K/pipe.o \
  $K/exec.o \
  $K/sysfile.o \
  $K/vfs.o \
  $K/xv6fs.o \
  $K/ext2.o

# Platform-specific objects (compiled from hal/$(ARCH)/)
ARCH_OBJS = \
  $(HAL)/$(ARCH)/hal_entry.o \
  $(HAL)/$(ARCH)/hal_start.o \
  $(HAL)/$(ARCH)/hal_swtch.o \
  $(HAL)/$(ARCH)/hal_tramp.o \
  $(HAL)/$(ARCH)/hal_kvec.o \
  $(HAL)/$(ARCH)/hal_plic.o \
  $(HAL)/$(ARCH)/hal_uart.o \
  $(HAL)/$(ARCH)/hal_virtio.o

# LoongArch overrides: hal_intr replaces hal_plic, add TLB refill handler
ifeq ($(ARCH),loongarch)
ARCH_OBJS = \
  $(HAL)/$(ARCH)/hal_entry.o \
  $(HAL)/$(ARCH)/hal_start.o \
  $(HAL)/$(ARCH)/hal_swtch.o \
  $(HAL)/$(ARCH)/hal_tramp.o \
  $(HAL)/$(ARCH)/hal_kvec.o \
  $(HAL)/$(ARCH)/hal_intr.o \
  $(HAL)/$(ARCH)/hal_uart.o \
  $(HAL)/$(ARCH)/hal_virtio.o \
  $(HAL)/$(ARCH)/hal_tlbrefill.o
endif

OBJS = $(K_OBJS) $(ARCH_OBJS)

# ============================================================
# LoongArch toolchain and QEMU (must be set before RISC-V auto-detection)
# ============================================================
ifeq ($(ARCH),loongarch)
TOOLPREFIX = loongarch64-linux-gnu-
QEMU = qemu-system-loongarch64
MIN_QEMU_VERSION = 7.1
# User programs: LoongArch syscall instruction and linker script
USERLD = $(HAL)/$(ARCH)/user.ld
else
USERLD = $U/user.ld
endif

# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
#TOOLPREFIX =

# Try to infer the correct TOOLPREFIX if not set (RISC-V only)
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-elf-'; \
	elif riscv64-none-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-none-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

ifndef QEMU
QEMU = qemu-system-riscv64
endif
ifndef MIN_QEMU_VERSION
MIN_QEMU_VERSION = 7.2
endif

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

CFLAGS = -Wall -Werror -Wno-unknown-attributes -O -fno-omit-frame-pointer -ggdb -gdwarf-2
ifeq ($(ARCH),loongarch)
CFLAGS += -march=loongarch64 -mcmodel=normal
else
CFLAGS += -march=rv64gc
CFLAGS += -mcmodel=medany
endif
CFLAGS += -MD
CFLAGS += -ffreestanding
CFLAGS += -fno-common -nostdlib
CFLAGS += -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset
CFLAGS += -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero
CFLAGS += -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc
CFLAGS += -fno-builtin-free
CFLAGS += -fno-builtin-memcpy -Wno-main
CFLAGS += -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf
CFLAGS += -I.
CFLAGS += -I$K
CFLAGS += -I$(HAL)
CFLAGS += -I$(HAL)/$(ARCH)
CFLAGS += -DARCH_$(ARCH)
CFLAGS += -DVIRTIO_NDISK=$(VIRTIO_NDISK)
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

LDFLAGS = -z max-page-size=4096

LOONGARCH_FS_OBJS = $(if $(filter loongarch,$(ARCH)),$K/fs_img.o $(if $(filter 2,$(VIRTIO_NDISK)),$K/fs1_img.o))

$K/kernel: $(OBJS) $(HAL)/$(ARCH)/kernel.ld $(LOONGARCH_FS_OBJS)
	$(LD) $(LDFLAGS) -T $(HAL)/$(ARCH)/kernel.ld -o $K/kernel $(OBJS) $(LOONGARCH_FS_OBJS)
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym

# LoongArch: embed filesystem image as a raw binary object linked into the kernel.
# This creates symbols _binary_fs_img_start, _binary_fs_img_end, _binary_fs_img_size.
ifeq ($(ARCH),loongarch)
$K/fs_img.o: fs.img
	$(OBJCOPY) -I binary -O elf64-loongarch -B loongarch64 $< $@

$K/fs1_img.o: fs1.img
	$(OBJCOPY) -I binary -O elf64-loongarch -B loongarch64 $< $@
endif

# Generic rules: compile kernel/ .c -> .o
$K/%.o: $K/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Generic rules: compile kernel/ .S -> .o
$K/%.o: $K/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

# HAL platform objects: compile hal/$(ARCH)/ .c -> .o
$(HAL)/$(ARCH)/%.o: $(HAL)/$(ARCH)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# HAL platform objects: compile hal/$(ARCH)/ .S -> .o
$(HAL)/$(ARCH)/%.o: $(HAL)/$(ARCH)/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

tags: $(OBJS)
	etags kernel/*.S kernel/*.c hal/*/*.S hal/*/*.c

ULIB = $U/ulib.o $U/usys.o $U/printf.o $U/umalloc.o

_%: %.o $(ULIB) $(USERLD)
	$(LD) $(LDFLAGS) -T $(USERLD) -e main -o $@ $< $(ULIB)
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

$U/usys.S : $U/usys.pl
	perl $U/usys.pl > $U/usys.S
ifeq ($(ARCH),loongarch)
	# Convert RISC-V instructions/registers to LoongArch.
	# "li a7, N"  -> "ori $$a7, $$r0, N"  (N fits 12 bits for syscall numbers)
	# "ecall"     -> "syscall 0"
	# "ret"       -> "jirl $$r0, $$ra, 0"
	sed -i 's/li a7, /ori $$a7, $$r0, /' $U/usys.S
	sed -i 's/ecall/syscall 0/' $U/usys.S
	sed -i 's/ret/jirl $$r0, $$ra, 0/' $U/usys.S
endif

$U/usys.o : $U/usys.S
	$(CC) $(CFLAGS) -c -o $U/usys.o $U/usys.S

$U/_forktest: $U/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
ifeq ($(ARCH),loongarch)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0x2000 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
else
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
endif
	$(OBJDUMP) -S $U/_forktest > $U/forktest.asm

mkfs/mkfs: mkfs/mkfs.c $K/fs.h $K/param.h
	gcc -Wno-unknown-attributes -I. -o mkfs/mkfs mkfs/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o

UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_ext2test1\
	$U/_ext2test2\
	$U/_ext2test3\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_logstress\
	$U/_forphan\
	$U/_dorphan\
	$U/_mount\
	$U/_umount\

fs.img: mkfs/mkfs README $(UPROGS) ext2.img
	mkfs/mkfs fs.img README $(UPROGS)
	cat ext2.img >> fs.img

-include kernel/*.d user/*.d hal/*/*.d

fs1.img: fs.img
	cp fs.img fs1.img

ext2.img:
ifeq ($(ARCH),loongarch)
	dd if=/dev/zero of=ext2.img bs=1M count=1
else
	dd if=/dev/zero of=ext2.img bs=1M count=10
endif
	mkfs.ext2 -F -b 1024 ext2.img

clean:
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	hal/*/*.o hal/*/*.d hal/*/*.asm hal/*/*.sym \
	$K/kernel fs.img fs1.img ext2.img \
	mkfs/mkfs .gdbinit \
        $U/usys.S \
	$(UPROGS)

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 3
endif

ifeq ($(ARCH),loongarch)
# LoongArch QEMU: -bios with raw binary (not -kernel), requires >= 1G RAM
QEMUOPTS = -machine virt -cpu la464 -m 1G -smp $(CPUS) -nographic
QEMUOPTS += -nic none
else
QEMUOPTS = -machine virt -bios none -kernel $K/kernel -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
ifeq ($(VIRTIO_NDISK),2)
QEMUOPTS += -drive file=fs1.img,if=none,format=raw,id=x1
QEMUOPTS += -device virtio-blk-device,drive=x1,bus=virtio-mmio-bus.1
endif
endif

qemu: check-qemu-version $K/kernel $(if $(filter riscv,$(ARCH)),fs.img $(if $(filter 2,$(VIRTIO_NDISK)),fs1.img))
ifeq ($(ARCH),loongarch)
	$(OBJCOPY) -O binary $K/kernel $K/kernel.bin
	$(QEMU) $(QEMUOPTS) -bios $K/kernel.bin
else
	$(QEMU) $(QEMUOPTS)
endif

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: $K/kernel .gdbinit $(if $(filter riscv,$(ARCH)),fs.img $(if $(filter 2,$(VIRTIO_NDISK)),fs1.img))
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

print-gdbport:
	@echo $(GDBPORT)

QEMU_VERSION := $(shell $(QEMU) --version | head -n 1 | sed -E 's/^QEMU emulator version ([0-9]+\.[0-9]+)\..*/\1/')
check-qemu-version:
	@qemu_major=$$(echo $(QEMU_VERSION) | cut -d. -f1); \
	 qemu_minor=$$(echo $(QEMU_VERSION) | cut -d. -f2); \
	 min_major=$$(echo $(MIN_QEMU_VERSION) | cut -d. -f1); \
	 min_minor=$$(echo $(MIN_QEMU_VERSION) | cut -d. -f2); \
	 if [ "$$qemu_major" -lt "$$min_major" ] || \
	    { [ "$$qemu_major" -eq "$$min_major" ] && [ "$$qemu_minor" -lt "$$min_minor" ]; }; then \
		echo "ERROR: Need qemu version >= $(MIN_QEMU_VERSION), found $(QEMU_VERSION)"; \
		exit 1; \
	fi
