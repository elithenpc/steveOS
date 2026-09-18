# SteveOS server roadmap

SteveOS can already use firmware-provided networking for outbound HTTP/HTTPS requests, and the native desktop can inspect the firmware network adapter.

## Discord bot hosting

A normal Discord bot uses the Discord API and, for Gateway-based bots, maintains a real-time Gateway connection. SteveOS therefore needs a userspace runtime plus native TCP, TLS, DNS, and WebSocket support before a Node.js bot can run directly on SteveOS.

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