# steveOS

A growing x86-64 operating system project with a native framebuffer desktop, real firmware-backed networking, hardware input drivers, persistent settings, and a Linux Mint-inspired desktop experience.

## Current milestone: native desktop 0.9

## Current milestone: 0.2

steveOS boots as a UEFI application, selects a high-resolution GOP mode, runs a safe boot diagnostics screen, and launches a graphical desktop with mouse and keyboard input.

The native desktop now includes:

- Mint-style application menu and panel/taskbar
- Real Mint-Y application icons packed into the native kernel image
- Web browser with on-demand UEFI HTTP fetches, HTML text extraction, links, scrolling, and back/forward history
- Calculator with integer expression parsing, parentheses, operator precedence, and unary minus
- Persistent text editor / notepad backed by UEFI NVRAM
- Recursive boot-volume file manager with text/image loading
- BMP and embedded-image viewer
- Settings with persistent theme and pointer scaling
- Task manager with native subsystem visibility and memory reporting
- Calendar and live firmware clock
- Hardware Control Center
- Native terminal with application/network/system commands
- Global close button, taskbar launchers, keyboard shortcuts, and start menu



Networking is deliberately not probed during early boot. Web requests are on-demand from the desktop so firmware networking is only entered when the user asks the browser to load a page.

The GitHub Actions workflow builds `BOOTX64.EFI` and a 64 MiB FAT32 `steveOS.img` USB image automatically.

## Mint integration

SteveOS pins the Linux Mint Mint-Y icon theme as a third-party submodule and converts a small selected set of icons into a compact native resource pack during CI. The desktop organisation and visual language are inspired by Cinnamon's traditional panel/menu model, while the freestanding renderer and application code remain SteveOS code. See [THIRD_PARTY.md](THIRD_PARTY.md).

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
