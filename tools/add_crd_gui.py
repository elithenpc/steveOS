#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DESKTOP = ROOT / "kernel" / "desktop.c"
MARK = "STEVEOS_CRD_GUI_1"


def main():
    text = DESKTOP.read_text(encoding="utf-8")
    if MARK in text:
        return

    anchor = 'static void draw_compat(void){'
    if anchor not in text:
        raise RuntimeError("draw_compat() was not generated")

    helper = '''/* STEVEOS_CRD_GUI_1 */
static void compat_launch_crd(void){
    if(!boot_info||!boot_info->uefi_write_text||!boot_info->uefi_launch_server){terminal_add("REMOTE DESKTOP SERVER UNAVAILABLE");return;}
    const char*cfg="REMOTE_DESKTOP_AUTORUN=1\\n";
    uint16_t path[64];size_t q=0;const char*wp="\\\\SteveOS\\\\Server\\\\remote-desktop.conf";
    while(*wp&&q+1<sizeof(path)/sizeof(path[0])){
        path[q++]=(uint16_t)(unsigned char)*wp++;
    }
    path[q]=0;
    WRITEFILE wr=(WRITEFILE)(uintptr_t)boot_info->uefi_write_text;
    if(wr(path,cfg,(uint64_t)(sizeof("REMOTE_DESKTOP_AUTORUN=1\\n")-1))!=0){terminal_add("REMOTE DESKTOP REQUEST FAILED");return;}
    LAUNCHSERVER launch=(LAUNCHSERVER)(uintptr_t)boot_info->uefi_launch_server;
    if(launch()!=0){terminal_add("SERVER MODE COULD NOT START");return;}
    terminal_add("STARTING CHROME REMOTE DESKTOP");
}
'''
    text = text.replace(anchor, helper + anchor, 1)

    button = 'fill_rect(42,446,(int)width-84,42,panel2_color());text(58,459,"OPEN FLATHUB IN BROWSER",text_color(),1);'
    if button not in text:
        raise RuntimeError("Flathub button not found")
    text = text.replace(button, button + '\n    fill_rect(42,498,(int)width-84,42,panel2_color());text(58,511,"CHROME REMOTE DESKTOP",text_color(),1);', 1)

    click = ('if(hit(x,y,42,446,(int)width-84,42)){const char*p="http://flathub.org/";'
             'size_t n=0;while(p[n]&&n+1<BROWSER_URL_MAX){browser_url[n]=p[n];n++;}'
             'browser_url[n]=0;current_app=APP_BROWSER;browser_focus=0;browser_fetch();mark_dirty();return;}}'
             'else if(current_app==APP_SERVER){')
    replacement = ('if(hit(x,y,42,446,(int)width-84,42)){const char*p="https://flathub.org/";'
                   'size_t n=0;while(p[n]&&n+1<BROWSER_URL_MAX){browser_url[n]=p[n];n++;}'
                   'browser_url[n]=0;current_app=APP_BROWSER;browser_focus=0;browser_fetch();mark_dirty();return;}'
                   'if(hit(x,y,42,498,(int)width-84,42)){compat_launch_crd();mark_dirty();return;}}'
                   'else if(current_app==APP_SERVER){')
    if click not in text:
        raise RuntimeError("Compatibility Hub click handler did not match")
    text = text.replace(click, replacement, 1)

    DESKTOP.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
