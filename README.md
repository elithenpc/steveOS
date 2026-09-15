# steveOS

A tiny x86-64 operating system project built as a UEFI-first desktop experiment.

## Current milestone: 0.2

steveOS boots as a UEFI application, selects a high-resolution GOP mode, runs a safe boot diagnostics screen, and launches a graphical desktop with mouse and keyboard input.

The current desktop includes:

- Terminal and basic system commands
- Task manager foundation
- UEFI filesystem browser
- System information
- BLEHHH image viewer
- Settings with light/dark theme and mouse scaling
- SteveOS installer for copying the bootloader to another EFI system partition
- A small firmware-backed web browser that can enter a URL, issue an HTTP GET through UEFI HTTP services, and display extracted page text

Networking is deliberately not probed during early boot. Web requests are on-demand from the desktop so a firmware network driver cannot stall normal startup.

The GitHub Actions workflow builds `BOOTX64.EFI` and a 64 MiB FAT32 `steveOS.img` USB image automatically.

## Current architecture

```text
UEFI firmware
    ↓
EFI/BOOT/BOOTX64.EFI
    ↓
GOP graphics + UEFI boot services
    ↓
SteveOS desktop shell
    ├── Window manager
    ├── Terminal
    ├── Filesystem / installer
    ├── Task manager
    ├── Image viewer
    └── Web browser → UEFI HTTP services
```

This is still a UEFI application rather than a native post-`ExitBootServices()` kernel. The kernel, interrupt controller, device drivers, TCP/IP stack, and a fully standards-compliant browser engine remain future work.

## Testing

Download the `steveOS-boot` artifact from a successful GitHub Actions run. The `steveOS.img` file can be tested in QEMU or written to a USB drive.

On real hardware, networking depends on the firmware exposing the UEFI HTTP service and having usable network configuration. Unsupported firmware will leave the browser functional as a UI but unable to fetch pages.
