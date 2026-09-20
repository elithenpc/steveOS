# steveOS

A growing x86-64 operating system project with a native framebuffer desktop, real firmware-backed networking, hardware input drivers, persistent settings, and a Linux Mint-inspired desktop experience.

## Current milestone: native desktop + server foundation 1.1

steveOS boots through UEFI, selects a GOP graphics mode, prepares the native kernel environment, and launches the native framebuffer desktop while deliberately retaining the UEFI services needed by selected compatibility bridges.

The native desktop includes:

- Mint-style application menu and panel/taskbar
- Real Mint-Y application icons packed into the native kernel image
- Disk installer that writes SteveOS to an existing EFI filesystem and registers a UEFI boot option
- Native EFI App Store with local package installation, online package downloads, and UEFI app launching
- Server Manager for Discord Bot, Tailscale and Windows EXE runtime
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
- Compatibility Hub for Linux, Windows, Flatpak and macOS application handoff
- Chrome Remote Desktop launcher for connecting to Windows PCs through the real Chromium/WebRTC client in Server Mode
- Global close button, taskbar launchers, power controls, keyboard shortcuts, Alt+Tab, and start menu

Networking is deliberately not probed during early boot. Web requests are on-demand from the desktop so firmware networking is only entered when the user asks the browser to load a page.

GitHub Actions builds `BOOTX64.EFI`, a 512 MiB FAT32 `steveOS.img`, and a rolling `latest` GitHub release used by the native Update Centre.

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
    ├── Compatibility Hub
    └── Web browser → UEFI HTTP services
```

SteveOS is currently a hybrid native environment. Its desktop and hardware input paths run in the native kernel, while UEFI Boot/Runtime Services remain available for filesystem snapshots, firmware time, NVRAM, and the HTTP bridge. Native storage drivers, a native IP stack, TLS, WebSocket, process isolation, a POSIX-compatible userspace, and full browser layout remain future work.

## Installation

From the live USB, open **Installer**. SteveOS lists other EFI filesystem volumes, lets you explicitly select one, arms the install as a separate step, writes `EFI/BOOT/BOOTX64.EFI`, and attempts to register a persistent `SteveOS` UEFI boot option. It does not repartition or format the selected volume.

## Apps

Place UEFI `.EFI` or Windows `.EXE` applications under `\\Apps` on the boot volume to make them appear in App Store. App Store can install them to `\\SteveOS\\Apps`. EFI packages launch directly from firmware; EXE packages boot Server Mode and run through Wine. EXE compatibility varies by application and its Windows dependencies.

## Chrome Remote Desktop

The Compatibility Hub now includes **Chrome Remote Desktop**. The native desktop requests Server Mode and launches Chromium at `https://remotedesktop.google.com/access`, where Google provides the JavaScript/WebRTC Chrome Remote Desktop client. Chrome Remote Desktop officially supports accessing Windows computers from a computer through the web client. citeturn0search0turn0search4

The Windows computer must already have Chrome Remote Desktop remote access configured. In Google's documented flow, the client signs in, selects the configured computer and enters its PIN. Sessions use Google's encrypted remote-desktop service and WebRTC connectivity. citeturn0search4turn0search3

SteveOS's small native framebuffer browser is intentionally not used for the CRD session because it is a lightweight HTML/text browser rather than a full JavaScript/WebRTC engine. Chromium is therefore part of Server Mode for this feature.

## Server

See [docs/SERVER.md](docs/SERVER.md) for the Discord, Tailscale, Windows runtime, Flatpak and Chrome Remote Desktop details. Server Mode supplies the Linux userspace used for these compatibility services; the native SteveOS desktop does not directly execute Windows PE files.

## Testing

Download the `steveOS-boot` artifact from a successful GitHub Actions run. The `steveOS.img` file can be tested in QEMU or written to a USB drive. The native desktop is intended for real x86-64 UEFI hardware as well as emulators.

On real hardware, networking depends on the firmware exposing the UEFI HTTP service and having usable network configuration. Unsupported firmware will leave the browser functional as a UI but unable to fetch pages.

## Hardware and compatibility

Server Mode now uses Alpine Linux 3.24.2 with the LTS kernel and exposes the host device tree to its Linux userspace. The runtime includes broad command-line tooling, Intel firmware, networking, Wine/Xvfb for Windows `.EXE` programs, Chromium for JavaScript/WebRTC applications, FFmpeg, GStreamer, ALSA, PipeWire and V4L2 tooling for audio/video devices. Camera devices appear as `/dev/video*` and audio devices as `/dev/snd` when the Linux kernel and hardware expose them. These are Server Mode capabilities, not yet native SteveOS kernel drivers.

## Updates

SteveOS checks the public VERSION file on GitHub after startup and can show an UPDATE READY notification. Advanced Settings and the terminal commands UPDATE, UPDATE CHECK and UPDATE INSTALL can check or install the rolling latest release. Installing an update replaces the native EFI loader and Server Mode runtime files; reboot after a successful install.

## Windows EXE compatibility

SteveOS can launch Windows `.exe` programs through Wine inside Server Mode. The native desktop detects EXE files and can launch them through the compatibility runtime.

The Terminal supports `EXES` and `RUNEXE filename.exe`. Server Mode uses Wine with Xvfb for compatibility display.

Only run EXEs you trust.
