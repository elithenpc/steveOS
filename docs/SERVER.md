# SteveOS Server Mode

SteveOS can already use firmware-provided networking for outbound HTTP/HTTPS requests, and the native desktop can inspect the firmware network adapter.

## Discord bot hosting

A normal Discord bot uses the Discord API and maintains network connections for its chosen API features. Native SteveOS still lacks a full POSIX userspace and native TCP/TLS/DNS/WebSocket stack, so Server Mode supplies an Alpine Linux userspace for the practical implementation.

The intended path is:

1. Native NIC driver and packet RX/TX
2. ARP and IPv4
3. UDP, TCP and DNS
4. TLS
5. WebSocket
6. ELF64 userspace process loader
7. POSIX-compatible syscall layer
8. Node.js-compatible runtime or a native bot runtime
9. Service supervisor with automatic restart and logs

## Tailscale hosting

Tailscale's Linux client normally runs `tailscaled`, and Linux uses a TUN device for the VPN interface. Tailscale also documents a userspace-networking mode for environments without TUN support.

The intended path is:

1. Native UDP/IP networking
2. TUN-equivalent virtual network interface
3. WireGuard cryptography and packet handling
4. Userspace daemon/process execution
5. Persistent Tailscale state
6. Service supervision and automatic startup

## Windows EXE compatibility

Server Mode includes Wine and Xvfb. Windows `.EXE` files placed under `\\SteveOS\\Apps` can be launched from the native App Store or with the native terminal command `RUNEXE NAME.EXE`.

The native desktop hands the selected executable to Server Mode, which runs it through Wine. x86_64 Alpine documents Wine support including WoW64 for many 32-bit applications, but application compatibility and required Windows dependencies vary.

GUI EXEs currently start inside Xvfb. They are therefore executable in the server environment, but their windows are not yet embedded into the native SteveOS desktop. A future display bridge can provide local or remote interactive Windows application windows.

Once Server Mode is open, the Linux shell also provides `runexe /efi/SteveOS/Apps/program.exe` for manually launching an executable.

## Current service manager

The native Server Manager stores explicit startup intent for Discord and Tailscale and reports the current firmware network state. Windows EXE execution is handled by the installed Server Mode runtime.

## Internet access today

The browser and App Store can use the UEFI HTTP service when firmware networking is configured. The terminal command `NETTEST` performs an HTTP connectivity check.

The next major networking milestone is native packet networking. Keeping UEFI HTTP as a compatibility bridge during development lets SteveOS retain useful internet access while the native stack is built.

## Windows compatibility

Windows EXE files can be placed in the Apps directory and launched through Server Mode. The runtime uses Wine with Xvfb, so console and service programs can run directly and GUI programs can run on a virtual X display. A GUI program does not automatically appear inside the native SteveOS framebuffer yet.
\n## Multimedia and hardware compatibility\n\nThe Server Mode image now uses Alpine Linux 3.24.2 and the LTS kernel rather than the minimal virtual kernel. It includes FFmpeg, GStreamer base/good/bad/ugly plugins, ALSA, PipeWire, V4L2 utilities, Intel firmware and common Linux hardware tools. The init script attempts common network, graphics, camera, audio, storage and Bluetooth modules. Host device nodes are exposed to the runtime, so camera devices can appear as /dev/video* and audio devices as /dev/snd.
\nThese are Linux compatibility facilities. Native SteveOS still needs dedicated kernel drivers and device APIs before cameras, microphones, hardware-accelerated multimedia and Windows compatibility are first-class native desktop features.
\n## Automatic updates\n\nGitHub Actions publishes a rolling latest release containing the native EFI loader, Server Mode bootloader, LTS kernel, initramfs and full USB image. The native Update Centre checks the repository VERSION file and downloads the release assets through the firmware HTTP client.


## Cross-platform application runtime

Server Mode now provides an application dispatcher for Linux ELF binaries, AppImages, scripts, Windows EXE/COM files, Flatpak bundles, and macOS application formats. Linux programs execute in the Alpine Linux userspace; Windows programs are routed through Wine with Xvfb; Flatpak is configured for Flathub. macOS APP/DMG/PKG files are routed through Darling when Darling is installed. Darling is a Linux compatibility layer for macOS software, and its GUI support is still experimental, so macOS application compatibility is not universal.

The native Compatibility Hub can hand an application selected in File Manager to Server Mode and can open Flathub in the native browser. The native UEFI kernel does not directly execute ELF, PE, Mach-O or Flatpak payloads. Server Mode supplies the userspace compatibility layer.

## Flatpak Store

The App Store/Compatibility Hub path uses Flathub as the Flatpak catalogue. Flatpak is configured with the Flathub remote inside Server Mode. Flatpak applications retain Flatpak's sandbox model and can be installed per-user or system-wide.

## Linux Mint visual assets

The native desktop continues to use bundled Mint-Y icons for its application and system UI. The Mint-Y icon theme remains a third-party asset rather than being copied into the SteveOS kernel source.
