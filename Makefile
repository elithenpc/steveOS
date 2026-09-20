CC ?= x86_64-w64-mingw32-gcc
LD ?= x86_64-w64-mingw32-ld
OBJCOPY ?= x86_64-w64-mingw32-objcopy
PYTHON ?= python3

CFLAGS := -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie -mno-red-zone -mno-sse -mno-mmx -mno-80387 -m64 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
KERNEL_CFLAGS := $(CFLAGS) -mno-red-zone -fshort-wchar -mgeneral-regs-only -ffreestanding -fno-stack-protector -fno-pie -fno-pic

all: build/BOOTX64.EFI build/steveOS.img

build:
	mkdir -p build

build/mint_icons.raw.o: tools/pack_mint_icons.py third_party/mint-y-icons
	mkdir -p build
	$(PYTHON) tools/pack_mint_icons.py
	$(OBJCOPY) --input-target=binary --output-target=elf64-x86-64 --binary-architecture=i386:x86-64 build/mint_icons.raw build/mint_icons.raw.o

build/boot.raw.o: build/boot.raw
	$(OBJCOPY) --input-target=binary --output-target=elf64-x86-64 --binary-architecture=i386:x86-64 $< $@

build/boot.raw: src/boot.c src/bootinfo.h
	mkdir -p build
	$(CC) $(CFLAGS) -c src/boot.c -o build/boot.o
	$(LD) -r -o $@ build/boot.o

build/BOOTX64.EFI: build/native_kernel.raw.o build/boot.raw.o
	mkdir -p build
	$(LD) -T src/linker.ld -o build/BOOTX64.EFI build/native_kernel.raw.o build/boot.raw.o

build/native-kernel-entry.o: kernel/entry.S
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/native-kernel-main.o: kernel/main.c kernel/desktop.h src/bootinfo.h build/boot.raw.o
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/native-kernel-desktop.o: kernel/desktop.c kernel/desktop.h src/bootinfo.h build/mint_icons.raw.o tools/prepare_desktop.py tools/fix_desktop_decls.py tools/add_crd_gui.py
	mkdir -p build
	$(PYTHON) tools/prepare_desktop.py
	$(PYTHON) tools/fix_desktop_decls.py
	$(PYTHON) tools/add_crd_gui.py
	$(CC) $(KERNEL_CFLAGS) -c kernel/desktop.c -o $@

build/native_kernel.elf: build/native-kernel-entry.o build/native-kernel-main.o build/native-kernel-desktop.o
	$(LD) -T kernel/linker.ld -o $@ $^

build/native_kernel.raw: build/native_kernel.elf
	$(OBJCOPY) -O binary $< $@

build/native_kernel.raw.o: build/native_kernel.raw
	$(OBJCOPY) --input-target=binary --output-target=elf64-x86-64 --binary-architecture=i386:x86-64 $< $@

build/steveOS.img: build/BOOTX64.EFI
	mkdir -p build/image/EFI/BOOT
	cp build/BOOTX64.EFI build/image/EFI/BOOT/BOOTX64.EFI
	truncate -s 512M $@
	mkfs.vfat -F 32 $@
	mcopy -i $@ -s build/image/* ::/

.PHONY: all clean
clean:
	rm -rf build
