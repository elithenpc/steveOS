#!/usr/bin/env python3
"""Apply deterministic source-level desktop enhancements before the native build.

The native desktop is intentionally kept in one freestanding C translation unit.
This small preparer keeps the feature additions readable and avoids hand-editing
large generated build files. It is idempotent.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
DESKTOP = ROOT / "kernel" / "desktop.c"
HEADER = ROOT / "kernel" / "desktop.h"
MARK = "STEVEOS_EXTENDED_SETTINGS_2"


def function_span(text: str, name: str):
    m = re.search(r"static void " + re.escape(name) + r"\(void\)\s*\{", text)
    if not m:
        raise RuntimeError(f"could not find {name}()")
    start = m.start()
    brace = text.find("{", m.start())
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return start, i + 1
    raise RuntimeError(f"unterminated {name}()")


def replace_function(text: str, name: str, replacement: str):
    a, b = function_span(text, name)
    return text[:a] + replacement + text[b:]


def main():
    text = DESKTOP.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")

    if MARK in text:
        return

    # Keep private helper declarations private to desktop.c. This removes the
    # unused-static warning from every other translation unit including the header.
    header = header.replace(
        "/* Internal helpers used by desktop.c before their definitions. */\nstatic int terminal_file_match(const char *a, const char *b);\n\n",
        "",
    )
    HEADER.write_text(header, encoding="utf-8")

    anchor = '#include "desktop.h"\n'
    if anchor not in text:
        raise RuntimeError("desktop include anchor missing")
    text = text.replace(anchor, anchor + "\nstatic int terminal_file_match(const char *a, const char *b);\n", 1)


    # Cross-platform Compatibility Hub.
    text = text.replace('    APP_SERVER, APP_ADVANCED\n',
                        '    APP_SERVER, APP_ADVANCED, APP_COMPAT\n', 1)
    text = text.replace(
        'static const char*menu_names[]={"Web Browser","Calculator","Text Editor","File Manager","Image Viewer","Settings","Task Manager","Terminal","Calendar","Control Center","About SteveOS","System Information","Device Manager","Installer","App Store","Server Manager","Advanced Settings"};',
        'static const char*menu_names[]={"Web Browser","Calculator","Text Editor","File Manager","Image Viewer","Settings","Task Manager","Terminal","Calendar","Control Center","About SteveOS","System Information","Device Manager","Installer","App Store","Server Manager","Advanced Settings","Compatibility Hub"};',
        1)
    text = text.replace(
        'static const int menu_apps[]={APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_IMAGE,APP_SETTINGS,APP_TASKS,APP_TERMINAL,APP_CALENDAR,APP_CONTROL,APP_ABOUT,APP_SYSINFO,APP_DEVICES,APP_INSTALLER,APP_STORE,APP_SERVER,APP_ADVANCED};',
        'static const int menu_apps[]={APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_IMAGE,APP_SETTINGS,APP_TASKS,APP_TERMINAL,APP_CALENDAR,APP_CONTROL,APP_ABOUT,APP_SYSINFO,APP_DEVICES,APP_INSTALLER,APP_STORE,APP_SERVER,APP_ADVANCED,APP_COMPAT};',
        1)
    text = text.replace('for(int i=0;i<17;i++)if(contains_ci(menu_names[i],menu_search))',
                        'for(int i=0;i<18;i++)if(contains_ci(menu_names[i],menu_search))', 1)
    text = text.replace(
        'const int ap[]={APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_IMAGE,APP_SETTINGS,APP_TASKS,APP_TERMINAL,APP_CALENDAR,APP_CONTROL,APP_ABOUT,APP_SYSINFO,APP_DEVICES,APP_INSTALLER,APP_STORE,APP_SERVER,APP_ADVANCED};',
        'const int ap[]={APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_IMAGE,APP_SETTINGS,APP_TASKS,APP_TERMINAL,APP_CALENDAR,APP_CONTROL,APP_ABOUT,APP_SYSINFO,APP_DEVICES,APP_INSTALLER,APP_STORE,APP_SERVER,APP_ADVANCED,APP_COMPAT};',
        1)
    text = text.replace(
        'const char*names[]={"WEB BROWSER","CALCULATOR","TEXT EDITOR","FILE MANAGER","IMAGE VIEWER","SETTINGS","TASK MANAGER","TERMINAL","CALENDAR","CONTROL CENTER","ABOUT STEVEOS","SYSTEM INFORMATION","DEVICE MANAGER","INSTALLER","APP STORE","SERVER MANAGER","ADVANCED SETTINGS"};',
        'const char*names[]={"WEB BROWSER","CALCULATOR","TEXT EDITOR","FILE MANAGER","IMAGE VIEWER","SETTINGS","TASK MANAGER","TERMINAL","CALENDAR","CONTROL CENTER","ABOUT STEVEOS","SYSTEM INFORMATION","DEVICE MANAGER","INSTALLER","APP STORE","SERVER MANAGER","ADVANCED SETTINGS","COMPATIBILITY HUB"};',
        1)
    text = text.replace('for(int i=0;i<17;i++){\n        int col=i%cols,row=i/cols',
                        'for(int i=0;i<18;i++){\n        int col=i%cols,row=i/cols', 1)
    text = text.replace(
        'static const int desktop_icon_map[]={0,1,2,3,11,6,5,4,7,13,15,14,15,14,15,15,6};',
        'static const int desktop_icon_map[]={0,1,2,3,11,6,5,4,7,13,15,14,15,14,15,15,6,14};', 1)
    text = text.replace(
        ':i==14?"EFI + WINDOWS APP PACKAGES":i==15?"DISCORD + TAILSCALE SERVICES":"SERVER + BOOT RUNTIME CONTROLS"',
        ':i==14?"EFI + WINDOWS APP PACKAGES":i==15?"DISCORD + TAILSCALE SERVICES":i==16?"SERVER + BOOT RUNTIME CONTROLS":"LINUX + WINDOWS + MAC + FLATPAK"', 1)

    compat_helper = r"""static void compat_request_run(const STEVEOS_BOOT_FILE*f){
    if(!f||!boot_info||!boot_info->uefi_write_text||!boot_info->uefi_launch_server){
        terminal_add("SERVER APPLICATION BRIDGE UNAVAILABLE");return;
    }
    char name[128];file_name(f,name,sizeof(name));
    size_t last=0;for(size_t i=0;name[i];i++)if(name[i]=='/'||name[i]=='\\')last=i+1;
    const char*base=name+last;
    char cfg[192];size_t n=0;const char*p="APP_AUTORUN=/efi/SteveOS/Apps/";
    while(*p&&n+1<sizeof(cfg)){cfg[n++]=*p++;}
    while(*base&&n+1<sizeof(cfg)-2){cfg[n++]=*base++;}
    cfg[n++]='\n';cfg[n]=0;
    uint16_t path[64];size_t q=0;const char*wp="\\SteveOS\\Server\\run-app.conf";
    while(*wp&&q+1<sizeof(path)/sizeof(path[0]))path[q++]=(uint16_t)(unsigned char)*wp++;
    path[q]=0;
    WRITEFILE wr=(WRITEFILE)(uintptr_t)boot_info->uefi_write_text;
    if(wr(path,cfg,(uint64_t)n)!=0){terminal_add("APPLICATION REQUEST FAILED");return;}
    LAUNCHSERVER launch=(LAUNCHSERVER)(uintptr_t)boot_info->uefi_launch_server;
    if(launch()!=0){terminal_add("SERVER MODE COULD NOT START");return;}
    terminal_add("APPLICATION SENT TO SERVER MODE");
}
static void draw_compat(void){
    window_bar("COMPATIBILITY HUB","LINUX + WINDOWS + MAC + FLATPAK");
    text(42,116,"APPLICATION RUNTIMES",accent_color(),1);
    fill_rect(42,138,(int)width-84,52,panel_color());text(58,153,"LINUX ELF / APPIMAGE / SCRIPTS",text_color(),1);text(58,173,"NATIVE LINUX USERSPACE IN SERVER MODE",sub_color(),1);
    fill_rect(42,198,(int)width-84,52,panel_color());text(58,213,"WINDOWS EXE / COM",text_color(),1);text(58,233,"WINE + Xvfb IN SERVER MODE",sub_color(),1);
    fill_rect(42,258,(int)width-84,52,panel_color());text(58,273,"FLATPAK",text_color(),1);text(58,293,"FLATHUB + FLATPAK RUNTIME IN SERVER MODE",sub_color(),1);
    fill_rect(42,318,(int)width-84,52,panel_color());text(58,333,"macOS APP / DMG / PKG",text_color(),1);text(58,353,"DARLING BRIDGE WHEN INSTALLED",sub_color(),1);
    fill_rect(42,394,(int)width-84,42,accent_dark());text(58,407,"RUN SELECTED FILE",0xFFFFFFu,1);
    fill_rect(42,446,(int)width-84,42,panel2_color());text(58,459,"OPEN FLATHUB IN BROWSER",text_color(),1);
    text(42,520,"SELECT A FILE IN FILE MANAGER, THEN OPEN THIS HUB.",sub_color(),1);
    text(42,542,"SERVER MODE PROVIDES LINUX, WINE AND FLATPAK RUNTIMES.",sub_color(),1);
    text(42,564,"macOS SUPPORT USES DARLING; APP COMPATIBILITY VARIES.",sub_color(),1);
    taskbar();
}
"""
    server_anchor='static void draw_server(void){'
    if not text.includes(server_anchor): raise RuntimeError("draw_server anchor missing")
    text=text.replace(server_anchor,compat_helper+'\n'+server_anchor,1)

    text=text.replace(
        'else if(current_app==APP_SERVER){',
        'else if(current_app==APP_COMPAT){if(hit(x,y,42,394,(int)width-84,42)){if(selected_file>=0&&selected_file<(int)boot_info->boot_file_count)compat_request_run(&boot_files[selected_file]);else terminal_add("SELECT A FILE FIRST");mark_dirty();return;}if(hit(x,y,42,446,(int)width-84,42)){const char*p="http://flathub.org/";size_t n=0;while(p[n]&&n+1<BROWSER_URL_MAX){browser_url[n]=p[n];n++;}browser_url[n]=0;current_app=APP_BROWSER;browser_focus=0;browser_fetch();mark_dirty();return;}}else if(current_app==APP_SERVER){',
        1)
    text=text.replace(
        'current_app==APP_STORE||current_app==APP_SERVER||current_app==APP_ADVANCED)',
        'current_app==APP_STORE||current_app==APP_SERVER||current_app==APP_ADVANCED||current_app==APP_COMPAT)', 1)
    text=text.replace(
        'case APP_STORE:draw_store();break;case APP_SERVER:draw_server();break;case APP_ADVANCED:draw_advanced();break;',
        'case APP_STORE:draw_store();break;case APP_SERVER:draw_server();break;case APP_ADVANCED:draw_advanced();break;case APP_COMPAT:draw_compat();break;', 1)

    old_open = r"""else if(f->kind==4){
                if(boot_info->uefi_run_windows_app){
                    RUNWINDOWSAPP fn=(RUNWINDOWSAPP)(uintptr_t)boot_info->uefi_run_windows_app;
                    fn(f->name);
                }
            }else if(f->kind==4){if(boot_info->uefi_run_windows_app){RUNWINDOWSAPP fn=(RUNWINDOWSAPP)(uintptr_t)boot_info->uefi_run_windows_app;fn(f->name);}}else if(f->kind==2&&f->data){"""
    new_open = r"""else if(f->kind==4){
                if(boot_info->uefi_run_windows_app){
                    RUNWINDOWSAPP fn=(RUNWINDOWSAPP)(uintptr_t)boot_info->uefi_run_windows_app;
                    fn(f->name);
                }else compat_request_run(f);
            }else if(f->kind==2&&f->data){"""
    if(!text.includes(old_open)) throw new Error("terminal OPEN compatibility anchor missing");
    text=text.replace(old_open,new_open,1)

    old_files = r"""else if(f->kind==4&&boot_info->uefi_run_windows_app){RUNWINDOWSAPP fn=(RUNWINDOWSAPP)(uintptr_t)boot_info->uefi_run_windows_app;fn(f->name);}else if(f->kind==2&&f->data){"""
    new_files = r"""else if(f->kind==4&&boot_info->uefi_run_windows_app){RUNWINDOWSAPP fn=(RUNWINDOWSAPP)(uintptr_t)boot_info->uefi_run_windows_app;fn(f->name);}else if(f->kind==4){compat_request_run(f);}else if(f->kind==2&&f->data){"""
    if(!text.includes(old_files)) throw new Error("file manager compatibility anchor missing");
    text=text.replace(old_files,new_files,1)
    text=text.replace(
        'if(hit(x,y,bx,by,cw,ch)){launch_app((int[]){APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_IMAGE,APP_SETTINGS,APP_TASKS,APP_TERMINAL,APP_CALENDAR,APP_CONTROL,APP_ABOUT,APP_SYSINFO,APP_DEVICES,APP_INSTALLER,APP_STORE,APP_SERVER,APP_ADVANCED}[i]);return;}}}',
        'if(hit(x,y,bx,by,cw,ch)){launch_app((int[]){APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_IMAGE,APP_SETTINGS,APP_TASKS,APP_TERMINAL,APP_CALENDAR,APP_CONTROL,APP_ABOUT,APP_SYSINFO,APP_DEVICES,APP_INSTALLER,APP_STORE,APP_SERVER,APP_ADVANCED,APP_COMPAT}[i]);return;}}}', 1)

    # Extended settings are stored in the existing persistent service_flags byte.
    helper = r'''
/* STEVEOS_EXTENDED_SETTINGS_1
 * service_flags bits 0-2 are existing services. Extra persistent settings:
 * bit 3 automatic update checks, bit 4 update notifications, bit 5 animations,
 * bit 6 24-hour clock, bit 7 conservative UI mode.
 */
static const char *setting_state(uint8_t bit){return (service_flags&bit)?"ON":"OFF";}
static void toggle_setting(uint8_t bit){service_flags^=bit;save_settings();mark_dirty();}
'''
    marker = 'static void browser_copy_url(char*out,const char*in);\n'
    if marker not in text:
        raise RuntimeError("desktop helper anchor missing")
    text = text.replace(marker, helper + marker, 1)

    old_settings = None
    a, b = function_span(text, "draw_settings")
    # function_span only targets static void functions and returns the body.
    old_settings = text[a:b]
    new_settings = r'''static void draw_settings(void){
    window_bar("SYSTEM SETTINGS","PERSONALIZATION + UPDATE + INPUT");
    text(42,116,"PERSONALIZATION",accent_color(),1);
    fill_rect(42,136,(int)width-84,54,light_theme?panel2_color():panel_color());
    text(58,154,"THEME",text_color(),1);text((int)width-190,154,light_theme?"LIGHT":"DARK",accent_color(),1);
    fill_rect(42,202,(int)width-84,54,panel_color());
    text(58,220,"POINTER SCALE",text_color(),1);u64_text((int)width-190,220,pointer_scale,accent_color(),1);text(58,238,"1 TO 4",sub_color(),1);
    fill_rect(42,268,(int)width-84,54,panel_color());
    text(58,286,"ACCENT PRESET",text_color(),1);u64_text((int)width-190,286,(uint64_t)(accent_id+1),accent_color(),1);text((int)width-155,286,"1-4",sub_color(),1);

    text(42,338,"UPDATES",accent_color(),1);
    fill_rect(42,354,(int)width-84,46,panel_color());text(58,368,"AUTOMATIC UPDATE CHECKS",text_color(),1);text((int)width-180,368,setting_state(8),accent_color(),1);
    fill_rect(42,408,(int)width-84,46,panel_color());text(58,422,"UPDATE NOTIFICATIONS",text_color(),1);text((int)width-180,422,setting_state(16),accent_color(),1);

    text(42,472,"INTERFACE",accent_color(),1);
    fill_rect(42,488,(int)width-84,46,panel_color());text(58,502,"UI ANIMATIONS",text_color(),1);text((int)width-180,502,setting_state(32),accent_color(),1);
    fill_rect(42,542,(int)width-84,46,panel_color());text(58,556,"24-HOUR CLOCK",text_color(),1);text((int)width-180,556,setting_state(64),accent_color(),1);
    fill_rect(42,596,(int)width-84,46,panel_color());text(58,610,"CONSERVATIVE UI MODE",text_color(),1);text((int)width-180,610,setting_state(128),accent_color(),1);

    text(42,(int)height-98,"CLICK A ROW TO TOGGLE  •  F5 SAVES SETTINGS TO NVRAM",sub_color(),1);taskbar();
}'''
    text = text[:a] + new_settings + text[b:]

    # Replace the settings click handler with the extended controls.
    old = 'else if(current_app==APP_SETTINGS){if(hit(x,y,42,136,(int)width-84,54))light_theme^=1;else if(hit(x,y,42,202,(int)width-84,54)){pointer_scale=pointer_scale>=4?1:pointer_scale+1;native_pointer_set_scale(pointer_scale);}else if(hit(x,y,42,308,(int)width-84,54)){accent_id=(uint8_t)((accent_id+1)&3u);}mark_dirty();}'
    new = 'else if(current_app==APP_SETTINGS){if(hit(x,y,42,136,(int)width-84,54)){light_theme^=1;save_settings();}else if(hit(x,y,42,202,(int)width-84,54)){pointer_scale=pointer_scale>=4?1:pointer_scale+1;native_pointer_set_scale(pointer_scale);save_settings();}else if(hit(x,y,42,268,(int)width-84,54)){accent_id=(uint8_t)((accent_id+1)&3u);save_settings();}else if(hit(x,y,42,354,(int)width-84,46))toggle_setting(8);else if(hit(x,y,42,408,(int)width-84,46))toggle_setting(16);else if(hit(x,y,42,488,(int)width-84,46))toggle_setting(32);else if(hit(x,y,42,542,(int)width-84,46))toggle_setting(64);else if(hit(x,y,42,596,(int)width-84,46))toggle_setting(128);mark_dirty();}'
    if old not in text:
        raise RuntimeError("settings click handler anchor missing")
    text = text.replace(old, new, 1)

    # Add terminal commands for every persistent setting, useful even without a mouse.
    anchor = 'else if(str_eq(terminal_input,"UPDATE")){refresh_update_info();terminal_add(update_info.available?"UPDATE READY":"NO NEW UPDATE");}'
    replacement = '''else if(str_eq(terminal_input,"SETTING UPDATEAUTO ON")){service_flags|=8;save_settings();terminal_add("AUTO UPDATE CHECKS: ON");}
    else if(str_eq(terminal_input,"SETTING UPDATEAUTO OFF")){service_flags&=(uint8_t)~8u;save_settings();terminal_add("AUTO UPDATE CHECKS: OFF");}
    else if(str_eq(terminal_input,"SETTING NOTIFY ON")){service_flags|=16;save_settings();terminal_add("UPDATE NOTIFICATIONS: ON");}
    else if(str_eq(terminal_input,"SETTING NOTIFY OFF")){service_flags&=(uint8_t)~16u;save_settings();terminal_add("UPDATE NOTIFICATIONS: OFF");}
    else if(str_eq(terminal_input,"SETTING ANIMATIONS ON")){service_flags|=32;save_settings();terminal_add("UI ANIMATIONS: ON");}
    else if(str_eq(terminal_input,"SETTING ANIMATIONS OFF")){service_flags&=(uint8_t)~32u;save_settings();terminal_add("UI ANIMATIONS: OFF");}
    else if(str_eq(terminal_input,"SETTING CLOCK24 ON")){service_flags|=64;save_settings();terminal_add("24-HOUR CLOCK: ON");}
    else if(str_eq(terminal_input,"SETTING CLOCK24 OFF")){service_flags&=(uint8_t)~64u;save_settings();terminal_add("24-HOUR CLOCK: OFF");}
    else if(str_eq(terminal_input,"SETTING SAFE ON")){service_flags|=128;save_settings();terminal_add("CONSERVATIVE UI: ON");}
    else if(str_eq(terminal_input,"SETTING SAFE OFF")){service_flags&=(uint8_t)~128u;save_settings();terminal_add("CONSERVATIVE UI: OFF");}
    else if(str_eq(terminal_input,"SETTINGS")){terminal_add((service_flags&8)?"UPDATE AUTO: ON":"UPDATE AUTO: OFF");terminal_add((service_flags&16)?"UPDATE NOTIFY: ON":"UPDATE NOTIFY: OFF");terminal_add((service_flags&32)?"ANIMATIONS: ON":"ANIMATIONS: OFF");terminal_add((service_flags&64)?"CLOCK 24H: ON":"CLOCK 24H: OFF");terminal_add((service_flags&128)?"SAFE UI: ON":"SAFE UI: OFF");}
    else if(str_eq(terminal_input,"UPDATE")){refresh_update_info();terminal_add(update_info.available?"UPDATE READY":"NO NEW UPDATE");}'''
    if anchor not in text:
        raise RuntimeError("terminal update anchor missing")
    text = text.replace(anchor, replacement, 1)

    # Auto-check is now controlled by the persistent setting. Existing update code
    # still performs the real GitHub VERSION lookup and exposes the result in the UI.
    text = text.replace('if(++refresh_ticks>=8000){refresh_ticks=0;refresh_network_info();if(!update_checked)refresh_update_info();dirty=1;}',
                        'if(++refresh_ticks>=8000){refresh_ticks=0;refresh_network_info();if((service_flags&8)&&!update_checked)refresh_update_info();if((service_flags&16)&&update_info.available)terminal_add("STEVEOS UPDATE AVAILABLE - OPEN ADVANCED SETTINGS");dirty=1;}',
                        1)

    # Default automatic update checks on for new installations.
    text = text.replace('load_settings();load_note();load_bookmarks();', 'load_settings();if(!(service_flags&8))service_flags|=8;load_note();load_bookmarks();', 1)

    # Make the extended settings visible in HELP output.
    text = text.replace('TYPE HELP FOR COMMANDS', 'TYPE HELP FOR COMMANDS  •  SETTINGS  •  SETTING UPDATEAUTO/NOTIFY/ANIMATIONS/CLOCK24/SAFE', 1)

    DESKTOP.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
