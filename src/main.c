#include <efi.h>
#include <efilib.h>
#include <stdint.h>

extern const unsigned char _binary_build_boot_raw_start[];
extern const unsigned char _binary_build_boot_raw_end[];

typedef struct {
    uint32_t w;
    uint32_t h;
    UINT32 stride;
    EFI_GRAPHICS_PIXEL_FORMAT format;
    EFI_PIXEL_BITMASK mask;
    uint32_t *fb;
} SCREEN;

static SCREEN screen;
static EFI_SIMPLE_POINTER_PROTOCOL *mouse = NULL;
static int mouse_x;
static int mouse_y;
static int mouse_left_down;

typedef struct { int x, y, w, h; } RECT;

static void put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (UINT32)x >= screen.w || (UINT32)y >= screen.h) return;
    screen.fb[(UINTN)y * screen.stride + x] = color;
}

static void fill_rect(RECT r, uint32_t color) {
    int x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w, y1 = r.y + r.h;
    if (x1 > (int)screen.w) x1 = screen.w;
    if (y1 > (int)screen.h) y1 = screen.h;
    for (int y=y0;y<y1;y++) for (int x=x0;x<x1;x++) put_pixel(x,y,color);
}

static void draw_char(int x,int y,char c,uint32_t color,int scale) {
    static const uint8_t font[][5] = {
        {0x7e,0x11,0x11,0x7e,0},{0x7f,0x49,0x49,0x36,0},{0x3e,0x41,0x41,0x22,0},{0x7f,0x41,0x41,0x3e,0},
        {0x7f,0x49,0x49,0x41,0},{0x7f,0x09,0x09,0x01,0},{0x3e,0x41,0x51,0x72,0},{0x7f,0x08,0x08,0x7f,0},
        {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0},{0x7f,0x08,0x14,0x63,0},{0x7f,0x40,0x40,0x40,0},
        {0x7f,0x06,0x18,0x06,0x7f},{0x7f,0x06,0x18,0x7f,0},{0x3e,0x41,0x41,0x3e,0},{0x7f,0x09,0x09,0x06,0},
        {0x3e,0x41,0x61,0x7e,0},{0x7f,0x09,0x19,0x66,0},{0x26,0x49,0x49,0x32,0},{0x01,0x7f,0x01,0x01,0},
        {0x3f,0x40,0x40,0x3f,0},{0x1f,0x60,0x60,0x1f,0},{0x7f,0x30,0x0c,0x30,0x7f},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };
    if(c<'A'||c>'Z') return;
    const uint8_t *g=font[c-'A'];
    for(int col=0;col<5;col++) for(int row=0;row<7;row++) if(g[col]&(1u<<row))
        fill_rect((RECT){x+col*scale,y+row*scale,scale,scale},color);
}

static void draw_text(int x,int y,const char *s,uint32_t color,int scale) {
    while(*s){ if(*s==' ') x+=6*scale; else {draw_char(x,y,*s,color,scale);x+=6*scale;} s++; }
}

static void draw_image(void) {
    const unsigned char *p=_binary_build_boot_raw_start;
    uint32_t iw=*(const uint32_t*)(p), ih=*(const uint32_t*)(p+4);
    const unsigned char *pixels=p+8;
    if(!iw||!ih) return;
    for(UINT32 y=0;y<screen.h;y++){
        UINT32 sy=(UINT64)y*ih/screen.h;
        for(UINT32 x=0;x<screen.w;x++){
            UINT32 sx=(UINT64)x*iw/screen.w;
            const unsigned char *q=pixels+((UINTN)sy*iw+sx)*4;
            put_pixel(x,y,((uint32_t)q[2]<<16)|((uint32_t)q[1]<<8)|q[0]);
        }
    }
}

static void draw_cursor(void) {
    for(int i=0;i<16;i++){put_pixel(mouse_x,mouse_y+i,0xFFFFFF);if(i<10)put_pixel(mouse_x+i,mouse_y+i,0xFFFFFF);}
}

static int point_in_rect(int x,int y,RECT r){return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}

static void draw_start_menu(int selected) {
    RECT menu={24,(int)screen.h-250,390,220}; fill_rect(menu,0x20252B); fill_rect((RECT){menu.x,menu.y,menu.w,42},0x101318);
    draw_text(menu.x+18,menu.y+13,"STEVEOS",0xFFFFFF,2);
    const char *items[]={"BLEHHH","INSTALL TO COMPUTER","SHUT DOWN"};
    for(int i=0;i<3;i++){RECT row={menu.x+10,menu.y+55+i*48,menu.w-20,40};if(i==selected)fill_rect(row,0x3D4650);draw_text(row.x+12,row.y+12,items[i],0xFFFFFF,1);}
}

static void draw_desktop(void) {
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},0x17202A); fill_rect((RECT){0,0,(int)screen.w,34},0x0D1117);
    draw_text(14,10,"STEVEOS",0xFFFFFF,1); draw_text((int)screen.w-145,10,"UEFI MODE",0xAEB7C2,1);
    draw_text(28,70,"WELCOME",0xFFFFFF,3); draw_text(30,108,"STEVEOS IS RUNNING",0xC7D0D9,1); draw_text(30,132,"PRESS S FOR START",0xC7D0D9,1);
    fill_rect((RECT){20,(int)screen.h-50,130,38},0x101318); draw_text(42,(int)screen.h-38,"START",0xFFFFFF,1);
}

static void draw_installer(void) {
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},0x11161C); RECT panel={(int)screen.w/2-280,(int)screen.h/2-150,560,300}; fill_rect(panel,0x20252B);
    draw_text(panel.x+35,panel.y+30,"STEVEOS INSTALLER",0xFFFFFF,2); draw_text(panel.x+35,panel.y+82,"INSTALL STEVEOS TO THIS COMPUTER",0xD5DCE3,1);
    draw_text(panel.x+35,panel.y+108,"DISK SELECTION WILL BE ADDED NEXT",0x9DA7B2,1);
    fill_rect((RECT){panel.x+35,panel.y+190,180,48},0x3D4650); fill_rect((RECT){panel.x+245,panel.y+190,180,48},0x30363D);
    draw_text(panel.x+98,panel.y+207,"INSTALL",0xFFFFFF,1); draw_text(panel.x+310,panel.y+207,"CANCEL",0xFFFFFF,1);
}

static void redraw(int start_open,int selected,int installer_open){if(installer_open){draw_installer();draw_cursor();return;}draw_desktop();if(start_open)draw_start_menu(selected);draw_cursor();}

static void handle_mouse(int *start_open,int *selected,int *show_image,int *installer_open) {
    if(!mouse)return; EFI_SIMPLE_POINTER_STATE state; EFI_STATUS status=uefi_call_wrapper(mouse->GetState,2,mouse,&state); if(EFI_ERROR(status))return;
    mouse_x+=state.RelativeMovementX/2; mouse_y+=state.RelativeMovementY/2;
    if(mouse_x<0)mouse_x=0;if(mouse_y<0)mouse_y=0;if(mouse_x>=(int)screen.w)mouse_x=screen.w-1;if(mouse_y>=(int)screen.h)mouse_y=screen.h-1;
    int left=state.LeftButton?1:0,clicked=left&&!mouse_left_down;mouse_left_down=left;if(!clicked)return;
    if(*installer_open){RECT panel={(int)screen.w/2-280,(int)screen.h/2-150,560,300};if(point_in_rect(mouse_x,mouse_y,(RECT){panel.x+245,panel.y+190,180,48}))*installer_open=0;redraw(0,0,*installer_open);return;}
    RECT start={20,(int)screen.h-55,140,48};if(point_in_rect(mouse_x,mouse_y,start)){*start_open=!*start_open;redraw(*start_open,*selected,0);return;}
    if(*start_open){RECT menu={24,(int)screen.h-250,390,220};for(int i=0;i<3;i++){RECT row={menu.x+10,menu.y+55+i*48,menu.w-20,40};if(point_in_rect(mouse_x,mouse_y,row)){*selected=i;if(i==0){*show_image=1;*start_open=0;draw_image();draw_cursor();}else if(i==1){*installer_open=1;*start_open=0;redraw(0,0,1);}else uefi_call_wrapper(RT->ResetSystem,4,EfiResetShutdown,EFI_SUCCESS,0,NULL);return;}}}
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle,EFI_SYSTEM_TABLE *system_table){
    InitializeLib(image_handle,system_table); EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;
    EFI_STATUS status=uefi_call_wrapper(BS->LocateProtocol,3,&gEfiGraphicsOutputProtocolGuid,NULL,(void**)&gop);if(EFI_ERROR(status)||!gop)return status;
    UINT32 best_mode=gop->Mode->Mode,best_area=0;for(UINT32 mode=0;mode<gop->Mode->MaxMode;mode++){EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info=NULL;UINTN info_size=0;status=uefi_call_wrapper(gop->QueryMode,4,gop,mode,&info_size,&info);if(EFI_ERROR(status))continue;UINT32 area=info->HorizontalResolution*info->VerticalResolution;if(area>best_area){best_area=area;best_mode=mode;}}
    uefi_call_wrapper(gop->SetMode,2,gop,best_mode);screen.w=gop->Mode->Info->HorizontalResolution;screen.h=gop->Mode->Info->VerticalResolution;screen.stride=gop->Mode->Info->PixelsPerScanLine;screen.format=gop->Mode->Info->PixelFormat;screen.mask=gop->Mode->Info->PixelInformation;screen.fb=(uint32_t*)(UINTN)gop->Mode->FrameBufferBase;

    /* GNU-EFI 3.0.15 does not export the Simple Pointer GUID symbol. */
    EFI_GUID simple_pointer_guid={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
    status=uefi_call_wrapper(BS->LocateProtocol,3,&simple_pointer_guid,NULL,(void**)&mouse);if(!EFI_ERROR(status)&&mouse)uefi_call_wrapper(mouse->Reset,2,mouse,FALSE);

    EFI_EVENT timer_event;status=uefi_call_wrapper(BS->CreateEvent,5,EVT_TIMER,TPL_CALLBACK,NULL,NULL,&timer_event);if(EFI_ERROR(status))return status;uefi_call_wrapper(BS->SetTimer,3,timer_event,TimerPeriodic,160000);
    EFI_EVENT events[2]={ST->ConIn->WaitForKey,timer_event};EFI_INPUT_KEY key;int start_open=0,selected=0,show_image=0,installer_open=0;mouse_x=screen.w/2;mouse_y=screen.h/2;mouse_left_down=0;redraw(0,0,0);
    while(1){UINTN event_index=0;status=uefi_call_wrapper(BS->WaitForEvent,3,2,events,&event_index);if(EFI_ERROR(status))continue;if(event_index==1){if(!show_image)handle_mouse(&start_open,&selected,&show_image,&installer_open);continue;}status=uefi_call_wrapper(ST->ConIn->ReadKeyStroke,2,ST->ConIn,&key);if(EFI_ERROR(status))continue;
        if(installer_open){if(key.ScanCode==SCAN_ESC){installer_open=0;redraw(0,0,0);}continue;}if(show_image){show_image=0;redraw(0,0,0);continue;}
        if(key.UnicodeChar=='s'||key.UnicodeChar=='S'){start_open=!start_open;selected=0;redraw(start_open,selected,0);continue;}if(!start_open)continue;
        if(key.ScanCode==SCAN_UP){selected--;if(selected<0)selected=2;redraw(1,selected,0);}else if(key.ScanCode==SCAN_DOWN){selected++;if(selected>2)selected=0;redraw(1,selected,0);}else if(key.ScanCode==SCAN_ESC){start_open=0;redraw(0,selected,0);}else if(key.UnicodeChar==CHAR_CARRIAGE_RETURN||key.UnicodeChar==' '){if(selected==0){show_image=1;start_open=0;draw_image();draw_cursor();}else if(selected==1){installer_open=1;start_open=0;redraw(0,0,1);}else uefi_call_wrapper(RT->ResetSystem,4,EfiResetShutdown,EFI_SUCCESS,0,NULL);}
    }
}
