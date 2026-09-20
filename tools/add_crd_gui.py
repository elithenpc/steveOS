#!/usr/bin/env python3
"""Add the Chrome Remote Desktop launcher to the generated native Compatibility Hub.

The native framebuffer browser is intentionally not used for CRD because CRD
requires JavaScript, WebRTC and Google authentication. The launcher requests
Server Mode, where Chromium is available.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DESKTOP = ROOT / "kernel" / "desktop.c"
MARK = "STEVEOS_CRD_GUI_1"


def main() -> None:
    text = DESKTOP.read_text(encoding="utf-8")
    if MARK in text:
        return

    marker = 'static void draw_compat(void){'
    if marker not in text:
        raise RuntimeError("draw_compat() was not generated")

    helper = r'''/* STEVEOS_CRD_GUI_1 */
static void compat_launch_crd(void){
    if(!boot_info||!boot_info->uefi_write_text||!boot_info->uefi_launch_server){
        terminal_add("REMOTE DESKTOP SERVER UNAVAILABLE");return;
    }
    const char*cfg="REMOTE_DESKTOP_AUTORUN=1\n";
    uint16_t path[64];size_t q=0;const char*wp="\\SteveOS\\Server\\remote-desktop.conf";
    while(*wp&&q+1<sizeof(path)/sizeof(path[0]))path[q++]=(uint16_t)(unsigned char)*wp++;
    path[q]=0;
    WRITEFILE wr=(WRITEFILE)(uintptr_t)boot_info->uefi_write_text;
    if(wr(path,cfg,(uint64_t)(sizeof("REMOTE_DESKTOP_AUTORUN=1\n")-1))!=0){
        terminal_add("REMOTE DESKTOP REQUEST FAILED");return;
    }
    LAUNCHSERVER launch=(LAUNCHSERVER)(uintptr_t)boot_info->uefi_launch_server;
    if(launch()!=0){terminal_add("SERVER MODE COULD NOT START");return;}
    terminal_add("STARTING CHROME REMOTE DESKTOP");
}
'''
    text = text.replace(marker, helper + marker, 1)

    button_anchor = 'fill_rect(42,446,(int)width-84,42,panel2_color());text(58,459,"OPEN FLATHUB IN BROWSER",text_color(),1);'
    if button_anchor not in text:
        raise RuntimeError("Compatibility Hub Flathub button not found")
    button = button_anchor + '\n    fill_rect(42,498,(int)width-84,42,panel2_color());text(58,511,"CHROME REMOTE DESKTOP",text_color(),1);'
    text = text.replace(button_anchor, button, 1)

    text = text.replace(
        'text(42,564,"macOS SUPPORT USES DARLING; APP COMPATIBILITY VARIES.",sub_color(),1);',
        'text(42,548,"CHROME REMOTE DESKTOP OPENS THE REAL CHROMIUM WEBRTC CLIENT IN SERVER MODE.",sub_color(),1);\n    text(42,568,"SIGN IN TO GOOGLE AND SELECT YOUR WINDOWS COMPUTER.",sub_color(),1);\n    text(42,588,"macOS SUPPORT USES DARLING; APP COMPATIBILITY VARIES.",sub_color(),1);',
        1,
    )

    click_anchor = 'else if(current_app==APP_SERVER){'
    if click_anchor not in text:
        raise RuntimeError("application click handler anchor not found")
    crd = 'else if(current_app==APP_COMPAT){if(hit(x,y,42,498,(int)width-84,42)){compat_launch_crd();mark_dirty();return;}}else if(current_app==APP_SERVER){'
    # The existing Compatibility Hub branch is already immediately before APP_SERVER.
    existing = 'else if(current_app==APP_COMPAT){'
    pos = text.find(existing)
    if pos < 0:
        raise RuntimeError("Compatibility Hub click branch not found")
    branch_end = text.find(click_anchor, pos)
    if branch_end < 0:
        raise RuntimeError("Compatibility Hub branch boundary not found")
    branch = text[pos:branch_end]
    if 'hit(x,y,42,498' not in branch:
        branch += 'if(hit(x,y,42,498,(int)width-84,42)){compat_launch_crd();mark_dirty();return;}'
        text = text[:pos] + branch + text[branch_end:]

    DESKTOP.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
