CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy

GNU_EFI_LIBDIR ?= $(shell dirname "$$(dpkg -L gnu-efi 2>/dev/null | grep '/elf_x86_64_efi\.lds$$' | head -n1)")
GNU_EFI_LIBDIR := $(if $(GNU_EFI_LIBDIR),$(GNU_EFI_LIBDIR),/usr/lib)

CFLAGS := -I/usr/include/efi -I/usr/include/efi/x86_64 \
          -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
          -fshort-wchar -mno-red-zone -maccumulate-outgoing-args
KERNEL_CFLAGS := -O2 -ffreestanding -fno-stack-protector -fno-stack-check -fno-pie -fno-pic \
                 -mno-red-zone -mcmodel=small -Wall -Wextra -I.
LDFLAGS := -nostdlib -znocombreloc -T $(GNU_EFI_LIBDIR)/elf_x86_64_efi.lds \
           -shared -Bsymbolic -L$(GNU_EFI_LIBDIR) $(GNU_EFI_LIBDIR)/crt0-efi-x86_64.o
OBJCOPY_FLAGS := -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
                 -j .rel -j .rela -j .reloc --target=efi-app-x86_64

CORE_OBJS := build/network.o build/storage.o build/memory.o build/fs.o build/installer.o \
             build/kernel.o build/interrupts.o build/tasks.o build/shell.o build/bootlog.o

all: build/BOOTX64.EFI build/apps/Hello.efi

build/boot.raw: blehhh.png tools/image_to_raw.py
	python3 tools/image_to_raw.py

build/mint_icons.raw: third_party/mint-y-icons/usr/share/icons/Mint-Y/apps/64/browser.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/apps/64/accessories-calculator.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/apps/64/accessories-text-editor.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/places/64/folder.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/apps/64/Terminal.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/apps/64/gnome-system-monitor.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/apps/64/cinnamon-preferences-color.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/apps/64/calendar.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/places/64/user-home.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/places/64/folder-documents.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/places/64/folder-download.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/places/64/folder-pictures.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/places/64/gtk-network.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/apps/64/cinnamon-preferences-desktop-display.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/apps/64/cs-power.png \
	third_party/mint-y-icons/usr/share/icons/Mint-Y/apps/64/hwinfo.png tools/mint_icons_to_raw.py
	python3 tools/mint_icons_to_raw.py

build/mint_icons.raw.o: build/mint_icons.raw
	$(OBJCOPY) --input-target=binary --output-target=elf64-x86-64 \
		--binary-architecture=i386:x86-64 build/mint_icons.raw build/mint_icons.raw.o

build/apps/hello.o: apps/hello.c
	mkdir -p build/apps
	$(CC) $(CFLAGS) -c $< -o $@

build/apps/hello.so: build/apps/hello.o
	$(LD) $(LDFLAGS) $< -o $@ -lefi -lgnuefi

build/apps/Hello.efi: build/apps/hello.so
	$(OBJCOPY) $(OBJCOPY_FLAGS) $< $@

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
	$(CC) $(CFLAGS) -c $< -o $@

build/installer.o: src/installer.c src/installer.h src/fs.h
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel.o: src/kernel.c src/kernel.h src/bootinfo.h
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/interrupts.o: src/interrupts.c src/interrupts.h
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/tasks.o: src/tasks.c src/tasks.h
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/native-kernel-entry.o: kernel/entry.S
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/native-kernel-arch.o: kernel/arch.c kernel/usb.h src/bootinfo.h
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -Dnative_keyboard_read_scancode=native_keyboard_read_scancode_arch -c $< -o $@

build/native-kernel-usb.o: kernel/usb.c kernel/usb.h
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/native-kernel-i2c.o: kernel/i2c.c kernel/i2c.h
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/native-kernel-input.o: kernel/input.c kernel/i2c.h
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/native-kernel-main.o: kernel/main.c kernel/desktop.h src/bootinfo.h build/boot.raw.o
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/native-kernel-desktop.o: kernel/desktop.c kernel/desktop.h src/bootinfo.h build/mint_icons.raw.o
	mkdir -p build
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/native_kernel.elf: build/native-kernel-entry.o build/native-kernel-main.o build/native-kernel-desktop.o build/native-kernel-arch.o build/native-kernel-usb.o build/native-kernel-i2c.o build/native-kernel-input.o build/boot.raw.o build/mint_icons.raw.o
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
