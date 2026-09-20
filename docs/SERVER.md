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

## Windows EXE compatibility

Server Mode includes Wine and Xvfb. Windows `.EXE` files placed under `\\SteveOS\\Apps` can be launched from the native App Store or with the native terminal command `RUNEXE NAME.EXE`.

The native desktop hands the selected executable to Server Mode, which runs it through Wine. x86_64 Alpine documents Wine support including WoW64 for many 32-bit applications, but application compatibility and required Windows dependencies vary.

GUI EXEs currently start inside Xvfb. Their windows are not embedded into the native SteveOS framebuffer yet.

## Chrome Remote Desktop

Server Mode includes Chromium specifically for full JavaScript/WebRTC applications. The native Compatibility Hub has a **Chrome Remote Desktop** button. Selecting it writes a Server Mode launch request and boots the Server Mode graphical runtime with the Chromium client pointed at:

`https://remotedesktop.google.com/access`

Chrome Remote Desktop is a web client provided by Google and supports accessing Windows computers from a computer. Google's documented flow requires the Windows host to have remote access configured, then the client selects the host and enters its PIN. citeturn0search0turn0search4

Chrome Remote Desktop uses WebRTC for the live connection and Google services for session negotiation. Google's network guide lists the Chrome Remote Desktop web/API endpoints and explains that sessions can use direct, STUN or TURN connectivity depending on network conditions and policy. citeturn0search3

### SteveOS flow

```text
SteveOS Compatibility Hub
        ↓
Start Server Mode
        ↓
Chromium
        ↓
remotedesktop.google.com/access
        ↓
Google sign-in
        ↓
Select Windows PC
        ↓
Enter PIN
        ↓
Chrome Remote Desktop / WebRTC
        ↓
Windows desktop
```

The small native SteveOS browser is not used for the active CRD session because it does not provide the JavaScript, WebRTC, authentication and interactive browser environment required by the current CRD web client.

## Flatpak Store

Flatpak is configured with the Flathub remote inside Server Mode. Flatpak applications retain Flatpak's sandbox model and can be installed per-user or system-wide.

## Multimedia and hardware compatibility

The Server Mode image uses Alpine Linux 3.24.2 and the LTS kernel. It includes FFmpeg, GStreamer base/good/bad/ugly plugins, ALSA, PipeWire, V4L2 utilities, Intel firmware and common Linux hardware tools. Host device nodes are exposed to the runtime when supported by the Linux kernel.

## Automatic updates

GitHub Actions publishes a rolling latest release containing the native EFI loader, Server Mode bootloader, LTS kernel, initramfs and full USB image. The native Update Centre checks the repository VERSION file and downloads release assets through the firmware HTTP client.

## Cross-platform application runtime

Server Mode provides an application dispatcher for Linux ELF binaries, AppImages, scripts, Windows EXE/COM files, Flatpak bundles, and macOS application formats. Linux programs execute in the Alpine Linux userspace; Windows programs are routed through Wine with Xvfb; Flatpak is configured for Flathub; macOS APP/DMG/PKG files are routed through Darling when Darling is installed.

## Linux Mint visual assets

The native desktop continues to use bundled Mint-Y icons for its application and system UI. The Mint-Y icon theme remains a third-party asset rather than being copied into the SteveOS kernel source.
