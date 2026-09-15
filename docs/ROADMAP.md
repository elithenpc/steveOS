# steveOS architecture roadmap

## Current boot layer

- UEFI x86_64 boot application
- GOP framebuffer desktop
- Keyboard and simple-pointer mouse input
- Embedded RGBA image resources
- Start menu and installer UI
- UEFI HTTP networking
- UEFI block-device discovery
- UEFI Simple File System file access
- EFI installer service
- UEFI memory-map discovery

## Native kernel transition

The current build deliberately keeps UEFI services underneath the first desktop so the OS remains bootable while the native kernel is developed.

The next native layer is planned as:

1. physical memory manager
2. x86_64 page tables and virtual memory
3. IDT and interrupt handling
4. PIT/APIC timer
5. task scheduler
6. keyboard and mouse drivers
7. PCI enumeration
8. storage drivers
9. FAT32 filesystem
10. network card and Wi-Fi drivers
11. IPv4, ARP, UDP, TCP and DNS
12. TLS
13. window manager and compositor
14. userspace process model

## Internet

A browser is intentionally **not** part of this milestone. Once SteveOS owns its network stack, the browser can be built as a normal userspace application using the same TCP/TLS/HTTP interfaces.

The eventual browser can start small with HTML, CSS, images and links before adding JavaScript.

## Installation safety

The installer service only writes to a target device explicitly supplied by the installer UI. It does not automatically select a disk or repartition storage.
