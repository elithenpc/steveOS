#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "memory.h"
#include "storage.h"
#include "network.h"
#include "kernel.h"
#include "tasks.h"
#include "interrupts.h"

extern const unsigned char _binary_build_boot_raw_start[];
extern const unsigned char _binary_build_boot_raw_end[];

typedef struct {
    UINT32 w, h, stride;
    EFI_GRAPHICS_PIXEL_FORMAT format;
    EFI_PIXEL_BITMASK mask;
    uint32_t *fb;
} SCREEN;

typedef struct { int x, y, w, h; } RECT;

typedef enum {
    VIEW_DESKTOP = 0,
    VIEW_IMAGE,
    VIEW_SYSTEM,
    VIEW_NETWORK,
    VIEW_INSTALLER
} VIEW;

static SCREEN screen;
static EFI_SIMPLE_POINTER_PROTOCOL *mouse;
static EFI_EVENT mouse_event;
static int mouse_x, mouse_y, mouse_left_down;
static VIEW view = VIEW_DESKTOP;
static int start_open, selected;
static EFI_STATUS memory_status, network_status;
static UINT64 total_memory;
static UINTN disk_count;

static void put_pixel(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || (UINT32)x >= screen.w || (UINT32)y >= screen.h) return;
    screen.fb[(UINTN)y * screen.stride + x] = c;
}

static void fill_rect(RECT r, uint32_t c) {
    int x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w, y1 = r.y + r.h;
    if (x1 > (int)screen.w) x1 = screen.w;
    if (y1 > (int)screen.h) y1 = screen.h;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            put_pixel(x, y, c);
}

static void draw_char(int x, int y, char c, uint32_t color, int scale) {
    static const uint8_t font[26][5] = {
        {0x7e,0x11,0x11,0x7e,0},{0x7f,0x49,0x49,0x36,0},{0x3e,0x41,0x41,0x22,0},{0x7f,0x41,0x41,0x3e,0},
        {0x7f,0x49,0x49,0x41,0},{0x7f,0x09,0x09,0x01,0},{0x3e,0x41,0x51,0x72,0},{0x7f,0x08,0x08,0x7f,0},
        {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0},{0x7f,0x08,0x14,0x63,0},{0x7f,0x40,0x40,0x40,0},
        {0x7f,0x06,0x18,0x06,0x7f},{0x7f,0x06,0x18,0x7f,0},{0x3e,0x41,0x41,0x3e,0},{0x7f,0x09,0x09,0x06,0},
        {0x3e,0x41,0x61,0x7e,0},{0x7f,0x09,0x19,0x66,0},{0x26,0x49,0x49,0x32,0},{0x01,0x7f,0x01,0x01,0},
        {0x3f,0x40,0x40,0x3f,0},{0x1f,0x60,0x60,0x1f,0},{0x7f,0x30,0x0c,0x30,0x7f},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };
    if (c < 'A' || c > 'Z') return;
    const uint8_t *g = font[c - 'A'];
    for (int col = 0; col < 5; ++col)
        for (int row = 0; row < 7; ++row)
            if (g[col] & (1u << row))
                fill_rect((RECT){x + col * scale, y + row * scale, scale, scale}, color);
}

static void draw_text(int x, int y, const char *s, uint32_t color, int scale) {
    while (*s) {
        if (*s == ' ') x += 6 * scale;
        else { draw_char(x, y, *s, color, scale); x += 6 * scale; }
        ++s;
    }
}

static void draw_number(int x, int y, UINT64 n, uint32_t color, int scale) {
    char buf[32]; int p = 0;
    if (!n) buf[p++] = '0';
    while (n && p < 31) { buf[p++] = (char)('0' + (n % 10)); n /= 10; }
    for (int i = p - 1; i >= 0; --i) {
        char c = buf[i];
        if (c == '0') draw_text(x, y, "0", color, scale);
        else draw_text(x, y, (char[2]){c,0}, color, scale);
        x += 6 * scale;
    }
}

static void draw_cursor(void) {
    for (int i = 0; i < 15; ++i) {
        put_pixel(mouse_x, mouse_y + i, 0xFFFFFF);
        if (i < 9) put_pixel(mouse_x + i, mouse_y + i, 0xFFFFFF);
    }
}

static int point_in_rect(int x, int y, RECT r) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static void draw_desktop(void) {
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h}, 0x17202A);
    fill_rect((RECT){0,0,(int)screen.w,36}, 0x0D1117);
    draw_text(14, 11, "STEVEOS", 0xFFFFFF, 1);
    draw_text((int)screen.w - 145, 11, "UEFI MODE", 0xAEB7C2, 1);
    draw_text(30, 72, "WELCOME", 0xFFFFFF, 3);
    draw_text(32, 112, "STEVEOS IS RUNNING", 0xC7D0D9, 1);
    draw_text(32, 136, "PRESS S FOR START", 0xC7D0D9, 1);
    fill_rect((RECT){20,(int)screen.h-52,145,40}, 0x101318);
    draw_text(43,(int)screen.h-39,"START",0xFFFFFF,1);
}

static void draw_start_menu(void) {
    const char *items[] = {"BLEHHH","SYSTEM INFO","NETWORK","INSTALL TO COMPUTER","SHUT DOWN"};
    const int count = 5;
    int height = 55 + count * 45 + 10;
    RECT menu = {24, (int)screen.h - height - 12, 420, height};
    fill_rect(menu, 0x20252B);
    fill_rect((RECT){menu.x,menu.y,menu.w,42},0x101318);
    draw_text(menu.x+18,menu.y+13,"STEVEOS",0xFFFFFF,2);
    for (int i=0;i<count;i++) {
        RECT row={menu.x+10,menu.y+50+i*45,menu.w-20,38};
        if (i==selected) fill_rect(row,0x3D4650);
        draw_text(row.x+12,row.y+11,items[i],0xFFFFFF,1);
    }
}

static void draw_image_view(void) {
    const unsigned char *p = _binary_build_boot_raw_start;
    UINT32 iw = *(const UINT32 *)p, ih = *(const UINT32 *)(p + 4);
    const unsigned char *pixels = p + 8;
    if (!iw || !ih) return;
    for (UINT32 y=0; y<screen.h; ++y) {
        UINT32 sy=(UINT64)y*ih/screen.h;
        for (UINT32 x=0; x<screen.w; ++x) {
            UINT32 sx=(UINT64)x*iw/screen.w;
            const unsigned char *q=pixels+((UINTN)sy*iw+sx)*4;
            put_pixel((int)x,(int)y,((uint32_t)q[2]<<16)|((uint32_t)q[1]<<8)|q[0]);
        }
    }
}

static void draw_system_view(void) {
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},0x11161C);
    draw_text(40,40,"SYSTEM INFORMATION",0xFFFFFF,2);
    draw_text(40,95,"MEMORY BYTES",0xAEB7C2,1); draw_number(220,95,total_memory,0xFFFFFF,1);
    draw_text(40,130,"DISKS",0xAEB7C2,1); draw_number(220,130,disk_count,0xFFFFFF,1);
    draw_text(40,165,"TASKS",0xAEB7C2,1); draw_number(220,165,steveos_task_count(),0xFFFFFF,1);
    draw_text(40,215,"KERNEL",0xAEB7C2,1); draw_text(220,215,"UEFI HANDOFF READY",0xFFFFFF,1);
    draw_text(40,260,"FILESYSTEM",0xAEB7C2,1); draw_text(220,260,"UEFI SIMPLE FILE SYSTEM",0xFFFFFF,1);
    draw_text(40,305,"INTERRUPTS",0xAEB7C2,1); draw_text(220,305,"NATIVE SETUP RESERVED",0xFFFFFF,1);
    draw_text(40,(int)screen.h-60,"ESC TO RETURN",0xC7D0D9,1);
}

static void draw_network_view(void) {
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},0x11161C);
    draw_text(40,40,"NETWORK",0xFFFFFF,2);
    draw_text(40,100,"FIRMWARE NIC",0xAEB7C2,1);
    if (!EFI_ERROR(network_status)) draw_text(220,100,"AVAILABLE",0xFFFFFF,1);
    else draw_text(220,100,"NOT AVAILABLE",0xFFFFFF,1);
    draw_text(40,145,"TCP IP",0xAEB7C2,1); draw_text(220,145,"NATIVE STACK NEXT",0xFFFFFF,1);
    draw_text(40,190,"DNS",0xAEB7C2,1); draw_text(220,190,"NATIVE STACK NEXT",0xFFFFFF,1);
    draw_text(40,235,"HTTP",0xAEB7C2,1); draw_text(220,235,"NATIVE STACK NEXT",0xFFFFFF,1);
    draw_text(40,(int)screen.h-60,"ESC TO RETURN",0xC7D0D9,1);
}

static void draw_installer(void) {
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},0x11161C);
    RECT panel={(int)screen.w/2-310,(int)screen.h/2-180,620,360};
    fill_rect(panel,0x20252B);
    draw_text(panel.x+35,panel.y+30,"STEVEOS INSTALLER",0xFFFFFF,2);
    draw_text(panel.x+35,panel.y+82,"AVAILABLE DISKS",0xAEB7C2,1);
    draw_number(panel.x+250,panel.y+82,disk_count,0xFFFFFF,1);
    draw_text(panel.x+35,panel.y+120,"TARGET SELECTION",0xAEB7C2,1);
    draw_text(panel.x+35,panel.y+150,"DISK WRITING IS LOCKED UNTIL A TARGET IS",0xD5DCE3,1);
    draw_text(panel.x+35,panel.y+174,"EXPLICITLY SELECTED AND CONFIRMED.",0xD5DCE3,1);
    fill_rect((RECT){panel.x+35,panel.y+238,180,48},0x30363D);
    fill_rect((RECT){panel.x+405,panel.y+238,180,48},0x3D4650);
    draw_text(panel.x+91,panel.y+255,"CANCEL",0xFFFFFF,1);
    draw_text(panel.x+447,panel.y+255,"BACK",0xFFFFFF,1);
}

static void redraw(void) {
    if (view == VIEW_IMAGE) draw_image_view();
    else if (view == VIEW_SYSTEM) draw_system_view();
    else if (view == VIEW_NETWORK) draw_network_view();
    else if (view == VIEW_INSTALLER) draw_installer();
    else {
        draw_desktop();
        if (start_open) draw_start_menu();
    }
    draw_cursor();
}

static void open_selected(void) {
    switch (selected) {
        case 0: view=VIEW_IMAGE; start_open=0; break;
        case 1: view=VIEW_SYSTEM; start_open=0; break;
        case 2: view=VIEW_NETWORK; start_open=0; break;
        case 3: view=VIEW_INSTALLER; start_open=0; break;
        case 4: uefi_call_wrapper(RT->ResetSystem,4,EfiResetShutdown,EFI_SUCCESS,0,NULL); break;
    }
    redraw();
}

static void handle_mouse(void) {
    if (!mouse) return;
    EFI_SIMPLE_POINTER_STATE state;
    EFI_STATUS status=uefi_call_wrapper(mouse->GetState,2,mouse,&state);
    if (EFI_ERROR(status)) return;

    mouse_x += state.RelativeMovementX / 2;
    mouse_y += state.RelativeMovementY / 2;
    if (mouse_x<0) mouse_x=0;
    if (mouse_y<0) mouse_y=0;
    if (mouse_x>=(int)screen.w) mouse_x=(int)screen.w-1;
    if (mouse_y>=(int)screen.h) mouse_y=(int)screen.h-1;

    int left=state.LeftButton ? 1 : 0;
    int clicked=left && !mouse_left_down;
    mouse_left_down=left;
    if (!clicked) { redraw(); return; }

    if (view != VIEW_DESKTOP) {
        if (view == VIEW_INSTALLER) {
            RECT panel={(int)screen.w/2-310,(int)screen.h/2-180,620,360};
            if (point_in_rect(mouse_x,mouse_y,(RECT){panel.x+35,panel.y+238,180,48}) ||
                point_in_rect(mouse_x,mouse_y,(RECT){panel.x+405,panel.y+238,180,48})) view=VIEW_DESKTOP;
        } else view=VIEW_DESKTOP;
        start_open=0; redraw(); return;
    }

    RECT start={20,(int)screen.h-58,150,48};
    if (point_in_rect(mouse_x,mouse_y,start)) { start_open=!start_open; selected=0; redraw(); return; }

    if (start_open) {
        int count=5, height=55+count*45+10;
        RECT menu={24,(int)screen.h-height-12,420,height};
        for(int i=0;i<count;i++) {
            RECT row={menu.x+10,menu.y+50+i*45,menu.w-20,38};
            if(point_in_rect(mouse_x,mouse_y,row)) { selected=i; open_selected(); return; }
        }
    }
}

static void handle_key(EFI_INPUT_KEY key) {
    if (view != VIEW_DESKTOP) {
        if (key.ScanCode==SCAN_ESC || key.UnicodeChar=='b' || key.UnicodeChar=='B') { view=VIEW_DESKTOP; redraw(); }
        return;
    }
    if (key.UnicodeChar=='s'||key.UnicodeChar=='S') { start_open=!start_open; selected=0; redraw(); return; }
    if (!start_open) return;
    if (key.ScanCode==SCAN_UP) { if (--selected<0) selected=4; redraw(); }
    else if (key.ScanCode==SCAN_DOWN) { if (++selected>4) selected=0; redraw(); }
    else if (key.ScanCode==SCAN_ESC) { start_open=0; redraw(); }
    else if (key.UnicodeChar==CHAR_CARRIAGE_RETURN || key.UnicodeChar==' ') open_selected();
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    InitializeLib(image_handle, system_table);

    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;
    EFI_STATUS status=uefi_call_wrapper(BS->LocateProtocol,3,&gEfiGraphicsOutputProtocolGuid,NULL,(void**)&gop);
    if(EFI_ERROR(status)||!gop) return status;

    UINT32 best_mode=gop->Mode->Mode, best_area=0;
    for(UINT32 mode=0;mode<gop->Mode->MaxMode;mode++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info=NULL; UINTN info_size=0;
        if(EFI_ERROR(uefi_call_wrapper(gop->QueryMode,4,gop,mode,&info_size,&info))) continue;
        UINT32 area=info->HorizontalResolution*info->VerticalResolution;
        if(area>best_area){best_area=area;best_mode=mode;}
    }
    uefi_call_wrapper(gop->SetMode,2,gop,best_mode);
    screen.w=gop->Mode->Info->HorizontalResolution;
    screen.h=gop->Mode->Info->VerticalResolution;
    screen.stride=gop->Mode->Info->PixelsPerScanLine;
    screen.format=gop->Mode->Info->PixelFormat;
    screen.mask=gop->Mode->Info->PixelInformation;
    screen.fb=(uint32_t*)(UINTN)gop->Mode->FrameBufferBase;

    EFI_GUID simple_pointer_guid={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
    status=uefi_call_wrapper(BS->LocateProtocol,3,&simple_pointer_guid,NULL,(void**)&mouse);
    if(!EFI_ERROR(status)&&mouse){
        uefi_call_wrapper(mouse->Reset,2,mouse,FALSE);
        mouse_event=mouse->Mode ? mouse->Mode->WaitForInput : NULL;
    } else mouse=NULL;

    memory_status=steveos_memory_init();
    if(!EFI_ERROR(memory_status)) total_memory=steveos_total_memory_bytes();
    disk_count=steveos_count_disks();
    network_status=steveos_network_available();
    steveos_tasks_init();
    steveos_interrupts_init();
    (void)steveos_kernel_handoff();

    EFI_EVENT timer_event=NULL;
    status=uefi_call_wrapper(BS->CreateEvent,5,EVT_TIMER,TPL_CALLBACK,NULL,NULL,&timer_event);
    if(EFI_ERROR(status)) return status;
    uefi_call_wrapper(BS->SetTimer,3,timer_event,TimerPeriodic,160000);

    EFI_EVENT events[3]; UINTN event_count=1;
    UINTN keyboard_index=0, mouse_index=(UINTN)-1, timer_index=(UINTN)-1;
    events[0]=ST->ConIn->WaitForKey;
    if(mouse_event){mouse_index=event_count;events[event_count++]=mouse_event;}
    timer_index=event_count;events[event_count++]=timer_event;

    mouse_x=(int)screen.w/2; mouse_y=(int)screen.h/2; mouse_left_down=0;
    redraw();

    while(1) {
        UINTN event_index=0;
        status=uefi_call_wrapper(BS->WaitForEvent,3,event_count,events,&event_index);
        if(EFI_ERROR(status)) continue;
        if(event_index==keyboard_index) {
            EFI_INPUT_KEY key;
            if(!EFI_ERROR(uefi_call_wrapper(ST->ConIn->ReadKeyStroke,2,ST->ConIn,&key))) handle_key(key);
        } else if(event_index==mouse_index) {
            handle_mouse();
        } else if(event_index==timer_index) {
            /* Periodic timer keeps the UI responsive on firmware without a mouse. */
            if(view==VIEW_DESKTOP && !mouse) redraw();
        }
    }
}
