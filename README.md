# steveOS

A tiny x86-64 operating system project.

## Current milestone: 0.1

steveOS currently boots as a UEFI application and draws the repository's `blehhh.png` image directly to the framebuffer.

The boot path is:

```text
UEFI firmware
    ↓
EFI/BOOT/BOOTX64.EFI
    ↓
Graphics Output Protocol
    ↓
blehhh.png displayed fullscreen
```

The GitHub Actions workflow builds `BOOTX64.EFI` and a FAT32 `steveOS.img` USB image automatically.

## Testing

Download the `steveOS-boot` artifact from a successful GitHub Actions run. The `steveOS.img` file can be tested in QEMU or written to a USB drive.

This is intentionally tiny for now. Later milestones can replace the UEFI application with a real kernel and add keyboard input, memory management, filesystems, a desktop, networking, and other OS features.
