CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy

GNU_EFI_LIBDIR ?= $(shell dirname "$$(dpkg -L gnu-efi 2>/dev/null | grep '/elf_x86_64_efi\.lds$$' | head -n1)")
GNU_EFI_LIBDIR := $(if $(GNU_EFI_LIBDIR),$(GNU_EFI_LIBDIR),/usr/lib)

CFLAGS := -I/usr/include/efi -I/usr/include/efi/x86_64 \
          -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
          -fshort-wchar -mno-red-zone -maccumulate-outgoing-args
KERNEL_CFLAGS := -ffreestanding -fno-stack-protector -fno-stack-check -fno-pie -fno-pic \
                 -mno-red-zone -mcmodel=small -Wall -Wextra -I.
LDFLAGS := -nostdlib -znocombreloc -T $(GNU_EFI_LIBDIR)/elf_x86_64_efi.lds \
           -shared -Bsymbolic -L$(GNU_EFI_LIBDIR) $(GNU_EFI_LIBDIR)/crt0-efi-x86_64.o
OBJCOPY_FLAGS := -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
                 -j .rel -j .rela -j .reloc --target=efi-app-x86_64

CORE_OBJS := build/network.o build/storage.o build/memory.o build/fs.o build/installer.o \
             build/kernel.o build/interrupts.o build/tasks.o build/shell.o build/bootlog.o

all: build/BOOTX64.EFI

build/boot.raw: blehhh.png tools/image_to_raw.py
	python3 tools/image_to_raw.py

build/boot.raw.o: build/boot.raw
	$(OBJCOPY) --input-target=binary --output-target=elf64-x86-64 \
		--binary-architecture=i386:x86-64 build/boot.raw build/boot.raw.o

build/main.o: src/main.c src/shell.h src/bootlog.h src/kernel.h
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/shell.o: src/shell3.c src/shell.h src/memory.h src/storage.h src/network.h src/kernel.h src/tasks.h src/fs.h
	mkdir -p build
	$(CC) $(CFLAGS) -c src/shell3.c -o $@

build/bootlog.o: src/bootlog.c src/bootlog.h src/memory.h src/storage.h src/network.h src/tasks.h
	mkdir -p build
	$(CC) $(CFLAGS) -c src/bootlog.c -o $@

build/network.o: src/network.c src/network.h
	mkdir -p build
	$(CC) $(CFLAGS) -c src/network.c -o $@

build/storage.o: src/storage.c src/storage.h
	mkdir -p build
	$(CC) $(CFLAGS) -c src/storage.c -o $@

build/memory.o: src/memory.c src/memory.h
	mkdir -p build
	$(CC) $(CFLAGS) -c src/memory.c -o $@

build/fs.o: src/fs.c src/fs.h
	mkdir -p build
	$(CC) $(CFLAGS) -c src/fs.c -o $@

build/installer.o: src/installer.c src/installer.h src/fs.h
	mkdir -p build
	$(CC) $(CFLAGS) -c src/installer.c -o $@

build/kernel.o: src/kernel.c src/kernel.h src/bootinfo.h
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel.c -o $@

build/interrupts.o: src/interrupts.c src/interrupts.h
	mkdir -p build
	$(CC) $(CFLAGS) -c src/interrupts.c -o $@

build/tasks.o: src/tasks.c src/tasks.h
	mkdir -p build
	$(CC) $(CFLAGS) -c src/tasks.c -o $@

build/native-kernel-entry.o: kernel/entry.S
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/native-kernel-arch.o: kernel/arch.c src/bootinfo.h
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -c kernel/arch.c -o $@

build/native-kernel-main.o: kernel/main.c src/bootinfo.h build/boot.raw.o
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -c kernel/main.c -o $@

build/native_kernel.elf: build/native-kernel-entry.o build/native-kernel-main.o build/native-kernel-arch.o build/boot.raw.o
	$(LD) -T kernel/linker.ld -o $@ $^

build/native_kernel.raw: build/native_kernel.elf
	$(OBJCOPY) -O binary $< $@

build/native_kernel_raw.o: build/native_kernel.raw
	$(OBJCOPY) --input-target=binary --output-target=elf64-x86-64 \
		--binary-architecture=i386:x86-64 build/native_kernel.raw build/native_kernel_raw.o

build/boot.so: build/main.o build/boot.raw.o build/native_kernel_raw.o $(CORE_OBJS)
	$(LD) $(LDFLAGS) build/main.o build/boot.raw.o build/native_kernel_raw.o $(CORE_OBJS) -o $@ -lefi -lgnuefi

build/BOOTX64.EFI: build/boot.so
	$(OBJCOPY) $(OBJCOPY_FLAGS) $< $@

clean:
	rm -rf build

.PHONY: all clean
