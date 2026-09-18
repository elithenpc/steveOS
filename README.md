# steveOS

A growing x86-64 operating system project with a native framebuffer desktop, real firmware-backed networking, hardware input drivers, persistent settings, and a Linux Mint-inspired desktop experience.

## Current milestone: native desktop + server foundation 1.0


steveOS boots through UEFI, selects a GOP graphics mode, prepares the native kernel environment, and launches the native framebuffer desktop while deliberately retaining the UEFI services needed by selected compatibility bridges.

The native desktop includes:

- Mint-style application menu and panel/taskbar
- Real Mint-Y application icons packed into the native kernel image
- Disk installer that writes SteveOS to an existing EFI filesystem and registers a UEFI boot option
- Native EFI App Store with local package installation, online package downloads, and UEFI app launching
- Server Manager for Discord Bot and Tailscale service configuration intent
- Web browser with on-demand UEFI HTTP/HTTPS fetches, HTML text extraction, links, scrolling, back/forward history, tabs, bookmarks, page saving, local HTML, and BMP image previews
- Calculator with integer expression parsing, parentheses, operator precedence, and unary minus
- Persistent text editor / notepad backed by UEFI NVRAM
- Recursive boot-volume file manager with filters, keyboard selection, text/image loading, and writable editor notes
- BMP and embedded-image viewer
- Settings with persistent theme, pointer scaling, accent presets, service startup flags, logging, and boot delay controls
- Task manager with native subsystem visibility and memory reporting
- Calendar and live firmware clock
- Hardware Control Center
- Native terminal with application, network, disk, package, service, and system commands
- Global close button, taskbar launchers, power controls, keyboard shortcuts, Alt+Tab, and start menu



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
    ├── Desktop shell / window controls
    ├── Terminal
    ├── Filesystem / installer
    ├── Task manager
    ├── Image viewer
    └── Web browser → UEFI HTTP services
```

SteveOS is currently a hybrid native environment. Its desktop and hardware input paths run in the native kernel, while UEFI Boot/Runtime Services remain available for filesystem snapshots, firmware time, NVRAM, and the HTTP bridge. Native storage drivers, a native IP stack, TLS, WebSocket, process isolation, a POSIX-compatible userspace, and full browser layout remain future work.

## Installation

From the live USB, open **Installer**. SteveOS lists other EFI filesystem volumes, lets you explicitly select one, arms the install as a separate step, writes `EFI/BOOT/BOOTX64.EFI`, and attempts to register a persistent `SteveOS` UEFI boot option. It does not repartition or format the selected volume.

## Apps

Place UEFI `.EFI` applications under `\\Apps` on the boot volume to make them appear in App Store. App Store can install them to `\\SteveOS\\Apps`. It can also download an EFI application directly from a URL. Only launch EFI applications you trust because they execute with firmware-level privileges.

## Server

See [docs/SERVER.md](docs/SERVER.md) for the Discord and Tailscale runtime plan. The current Server Manager records startup intent and network state, but native Discord/Tailscale execution requires the future userspace and network stack.

## Testing

Download the `steveOS-boot` artifact from a successful GitHub Actions run. The `steveOS.img` file can be tested in QEMU or written to a USB drive. The native desktop is intended for real x86-64 UEFI hardware as well as emulators.

On real hardware, networking depends on the firmware exposing the UEFI HTTP service and having usable network configuration. Unsupported firmware will leave the browser functional as a UI but unable to fetch pages.
