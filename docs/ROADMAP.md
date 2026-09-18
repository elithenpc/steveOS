# steveOS architecture roadmap

## Current native layer

- UEFI x86_64 boot and GOP graphics
- Hybrid native kernel handoff with firmware runtime and boot services retained for explicit compatibility bridges
- Identity-mapped x86-64 paging
- Native keyboard input
- PS/2 fallback mouse input
- xHCI USB HID mouse input
- Experimental HID-over-I2C touchpad input
- Native framebuffer backbuffer renderer
- Mint-Y icon resource pipeline
- Traditional panel, application menu, launcher and window shell
- Persistent NVRAM settings and notes
- Recursive boot-volume snapshot and file viewer
- Task manager, calendar, control center and terminal
- Firmware-backed HTTP browser

## Browser roadmap

The current browser is a real network client, but intentionally lightweight. It can issue HTTP requests through the UEFI HTTP service, extract readable HTML text, decode a small set of entities, discover basic links, resolve common relative URLs, scroll, and navigate history.

The major missing browser layers are a native network stack, TLS ownership, a richer HTML/CSS layout engine, image decoding, forms, downloads, cookies, caching, tabs, and JavaScript.

## Native system roadmap

1. Interrupt-safe firmware compatibility boundary
2. Physical-page allocator and safer heap
3. PCI device manager
4. Native storage controller drivers
5. FAT32 read/write filesystem
6. Native network device drivers
7. IPv4, ARP, UDP, TCP and DNS
8. Native TLS
9. Process and address-space isolation
10. Scheduler and timers
11. Window manager and real compositing
12. Audio stack
13. Power management and ACPI device/resource enumeration
14. USB keyboard and broader HID support
15. Application packaging and installation
16. Shell scripting and richer terminal services

## Desktop roadmap

- Multi-window workspace management
- Notifications and system tray
- File operations and writable user directories
- Search and application discovery
- Text editor tabs and larger buffers
- Image formats beyond BMP
- More hardware information and diagnostics
- Accessibility controls
- Theme/accent editor
- Boot splash and recovery tools

## Installation safety

The installer service only writes to a target device explicitly supplied by the installer UI. It does not automatically select a disk or repartition storage.


## Server runtime

The native Server Manager is now part of the desktop. It tracks startup intent for Discord and Tailscale and exposes the firmware network state.

Discord bot execution requires a userspace runtime plus DNS, TCP, TLS and WebSocket support before a Node.js bot can run directly on SteveOS.

Tailscale execution requires userspace daemon support plus UDP/IP and a TUN-equivalent interface with WireGuard packet handling.

The immediate implementation order is:
1. Native NIC RX/TX
2. IPv4, ARP, UDP and TCP
3. DNS and TLS
4. WebSocket support
5. ELF64 userspace/process runtime
6. Service supervisor
7. Discord bot runtime integration
8. Tailscale/WireGuard runtime integration
