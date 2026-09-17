# Third-party inspiration and attribution

SteveOS is a freestanding x86-64 operating-system project. Its desktop shell is independently implemented for the native framebuffer environment, with visual and interaction ideas inspired by Linux Mint's Cinnamon desktop and Mint-Y icon theme.

## Linux Mint / Cinnamon

- Linux Mint Cinnamon: https://github.com/linuxmint/cinnamon
- Mint-Y icons: https://github.com/linuxmint/mint-y-icons

Cinnamon is distributed under GPL version 2 or later. Mint-Y is licensed under CC BY-SA 4.0, with bundled/icon-source projects carrying their own licences. SteveOS does not copy Cinnamon source code into the freestanding kernel; the shell architecture is independently implemented from scratch for SteveOS.

The current native UI borrows the broad desktop patterns that make Cinnamon familiar: a traditional panel, application menu, launcher/taskbar, control-center organisation, system-monitor style views, and a consistent icon/card language. SteveOS pins Mint-Y as a third-party source dependency and uses a small selected icon subset through a build-time resource conversion. The original repository remains the source of its licensing and attribution information.

See the upstream repositories for the complete copyright notices and licence texts.
