# SteveOS GUI-first interface

SteveOS is intended to be usable without a terminal. Every user-facing system operation should have a graphical entry point, while the terminal remains available for recovery and advanced users.

## Core shell

- Desktop and application launcher
- Search
- Task switching
- Window/application state
- Power and restart controls
- Notifications
- Quick settings / Control Centre
- System status

## Applications

- Browser
- Calculator
- Text editor
- File manager
- Image viewer
- Calendar
- Terminal
- Task manager
- System information
- Device manager
- Installer
- App Store
- Server Manager
- Advanced Settings
- About

## System settings

- Theme/light/dark appearance
- Accent colour
- Pointer scale
- Display information
- Time and date
- Update checking
- Update notifications
- Automatic update policy
- Boot delay
- Logging level
- Server services
- Network status
- Power actions
- Accessibility/input options

## Server Manager

The graphical Server Manager is the front end for Server Mode configuration. It should expose:

- Server installation
- Start/stop/restart
- Service status
- Discord Bot Manager
- Discord bot repository
- Discord bot runtime
- Bot entry command
- Dependency installation
- Bot enable/disable
- Bot restart policy
- Bot logs
- Wi-Fi configuration
- Tailscale configuration
- SSH configuration
- Windows EXE runtime
- Media/camera/audio status

Secrets such as Discord tokens and Tailscale authentication keys must be entered through password fields and must never be written to source control.

## Updates

The GUI must show:

- Installed SteveOS version
- Latest GitHub version
- Update availability
- Release status
- Check now
- Install update
- Restart after update
- Automatic update preference

The update implementation uses the existing GitHub-backed update functions in `src/kernel.c` and should remain usable even when the user never opens the terminal.

## User-friendly rules

1. Prefer buttons, menus and clear status text over commands.
2. Destructive operations require confirmation.
3. Long-running operations show progress or a busy state.
4. Errors explain what happened and what the user can do next.
5. Settings persist through the existing firmware-backed settings mechanism.
6. Advanced functionality remains available, but is separated from the normal user flow.
7. Unsupported hardware should be shown as unavailable rather than causing a crash.
8. Every GUI action must map to a real backend operation. A control must not claim success unless the backend returned success.
