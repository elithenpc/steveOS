$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/native-kernel-desktop.o: kernel/desktop.c kernel/desktop.h src/bootinfo.h build/mint_icons.raw.o tools/fix_desktop_decls.py tools/prepare_desktop.py
	mkdir -p build
	python3 tools/prepare_desktop.py
	python3 tools/fix_desktop_decls.py
	$(CC) $(KERNEL_CFLAGS) -c kernel/desktop.c -o $@

build/native_kernel.elf: build/native-kernel-entry.o build/native-kernel-main.o build/native-kernel-desktop.o build/native-kernel-arch.o build/native-kernel-usb.o build/native-kernel-i2c.o build/native-kernel-input.o build/boot.raw.o build/mint_icons.raw.o
	$(LD) -T kernel/linker.ld -o $@ $^
