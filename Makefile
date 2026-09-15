CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy

CFLAGS := -I/usr/include/efi -I/usr/include/efi/x86_64 \
          -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
          -fshort-wchar -mno-red-zone -maccumulate-outgoing-args
LDFLAGS := -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds \
           -shared -Bsymbolic -L/usr/lib /usr/lib/crt0-efi-x86_64.o
OBJCOPY_FLAGS := -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
                 -j .rel -j .rela -j .reloc --target=efi-app-x86_64

all: build/BOOTX64.EFI

build/boot.raw: blehhh.png tools/image_to_raw.py
	python3 tools/image_to_raw.py

build/boot.raw.o: build/boot.raw
	$(OBJCOPY) --input-target=binary --output-target=elf64-x86-64 \
		--binary-architecture=i386:x86-64 build/boot.raw build/boot.raw.o

build/main.o: src/main.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/boot.so: build/main.o build/boot.raw.o
	$(LD) $(LDFLAGS) build/main.o build/boot.raw.o -o $@ -lefi

build/BOOTX64.EFI: build/boot.so
	$(OBJCOPY) $(OBJCOPY_FLAGS) $< $@

clean:
	rm -rf build

.PHONY: all clean
