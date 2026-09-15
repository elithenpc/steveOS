#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "memory.h"
#include "storage.h"
#include "network.h"
#include "kernel.h"
#include "tasks.h"
#include "interrupts.h"
#include "fs.h"
#include "installer.h"

extern const unsigned char _binary_build_boot_raw_start[];
extern const unsigned char _binary_build_boot_raw_end[];

typedef struct { UINT32 w, h, stride; EFI_GRAPHICS_PIXEL_FORMAT format; EFI_PIXEL_BITMASK mask; uint32_t *fb; } SCREEN;
typedef struct { int x, y, w, h; } RECT;
typedef enum { VIEW_DESKTOP=0, VIEW_IMAGE, VIEW_SYSTEM, VIEW_NETWORK, VIEW_FILES, VIEW_TERMINAL, VIEW_SETTINGS, VIEW_INSTALLER } VIEW;

typedef struct { const char *name; VIEW view; } APP;

static SCREEN screen;
static EFI_SIMPLE_POINTER_PROTOCOL *mouse;
static EFI_EVENT mouse_event;
static EFI_EVENT timer_event;
static EFI_FILE_PROTOCOL *boot_root;
static VIEW view=VIEW_DESKTOP;
static int start_open, selected;
static int mouse_x, mouse_y, mouse_left_down;
static int settings_light;
static UINT32 mouse_divisor=2;
static UINT64 total_memory;
static UINTN disk_count;
static EFI_STATUS network_status;
static CHAR8 terminal_line[96];
static UINTN terminal_len;
static CHAR8 terminal_status[128];

static const APP apps[] = {
    {"BLEHHH VIEWER", VIEW_IMAGE},
    {"SYSTEM INFO", VIEW_SYSTEM},
    {"NETWORK", VIEW_NETWORK},
    {"FILES", VIEW_FILES},
    {"TERMINAL", VIEW_TERMINAL},
    {"SETTINGS", VIEW_SETTINGS},
    {"INSTALLER", VIEW_INSTALLER}
};
#define APP_COUNT (sizeof(apps)/sizeof(apps[0]))

static uint32_t bg_color(void) { return settings_light ? 0xE6E9ED : 0x17202A; }
static uint32_t bar_color(void) { return settings_light ? 0xCCD2D9 : 0x0D1117; }
static uint32_t panel_color(void) { return settings_light ? 0xF5F6F8 : 0x20252B; }
static uint32_t card_color(void) { return settings_light ? 0xD5DAE0 : 0x222B34; }
static uint32_t accent_color(void) { return settings_light ? 0x9AA4AE : 0x3D4650; }
static uint32_t fg_color(void) { return settings_light ? 0x11161C : 0xFFFFFF; }
static uint32_t sub_color(void) { return settings_light ? 0x3C4650 : 0xC7D0D9; }

static void put_pixel(int x,int y,uint32_t c) {
    if(x<0||y<0||(UINT32)x>=screen.w||(UINT32)y>=screen.h) return;
    screen.fb[(UINTN)y*screen.stride+(UINTN)x]=c;
}

static void fill_rect(RECT r,uint32_t c) {
    int x0=r.x<0?0:r.x,y0=r.y<0?0:r.y,x1=r.x+r.w,y1=r.y+r.h;
    if(x1>(int)screen.w)x1=(int)screen.w;
    if(y1>(int)screen.h)y1=(int)screen.h;
    for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++) put_pixel(x,y,c);
}

static void draw_char(int x,int y,char c,uint32_t color,int scale) {
    static const uint8_t font[26][5]={
        {0x7e,0x11,0x11,0x7e,0},{0x7f,0x49,0x49,0x36,0},{0x3e,0x41,0x41,0x22,0},{0x7f,0x41,0x41,0x3e,0},
        {0x7f,0x49,0x49,0x41,0},{0x7f,0x09,0x09,0x01,0},{0x3e,0x41,0x51,0x72,0},{0x7f,0x08,0x08,0x7f,0},
        {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0},{0x7f,0x08,0x14,0x63,0},{0x7f,0x40,0x40,0x40,0},
        {0x7f,0x06,0x18,0x06,0x7f},{0x7f,0x06,0x18,0x7f,0},{0x3e,0x41,0x41,0x3e,0},{0x7f,0x09,0x09,0x06,0},
        {0x3e,0x41,0x61,0x7e,0},{0x7f,0x09,0x19,0x66,0},{0x26,0x49,0x49,0x32,0},{0x01,0x7f,0x01,0x01,0},
        {0x3f,0x40,0x40,0x3f,0},{0x1f,0x60,0x60,0x1f,0},{0x7f,0x30,0x0c,0x30,0x7f},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };
    if(c>='a'&&c<='z')c=(char)(c-32);
    if(c<'A'||c>'Z')return;
    const uint8_t *g=font[c-'A'];
    for(int col=0;col<5;col++) for(int row=0;row<7;row++) if(g[col]&(1u<<row))
        fill_rect((RECT){x+col*scale,y+row*scale,scale,scale},color);
}

static void draw_text(int x,int y,const char *s,uint32_t color,int scale) {
    while(*s) {
        if(*s==' ') x+=6*scale;
        else if(*s=='-'||*s=='.'||*s==':'||*s=='/'||*s=='_') {
            if(*s=='-') fill_rect((RECT){x,y+3*scale,5*scale,scale},color);
            else if(*s=='.') fill_rect((RECT){x,y+6*scale,scale,scale},color);
            else if(*s==':') { fill_rect((RECT){x,y+2*scale,scale,scale},color); fill_rect((RECT){x,y+6*scale,scale,scale},color); }
            else if(*s=='_') fill_rect((RECT){x,y+6*scale,5*scale,scale},color);
            else fill_rect((RECT){x+2*scale,y,scale,7*scale},color);
            x+=6*scale;
        } else { draw_char(x,y,*s,color,scale); x+=6*scale; }
        s++;
    }
}

static void draw_number(int x,int y,UINT64 n,uint32_t color,int scale) {
    char b[32]; int p=0;
    if(!n)b[p++]='0';
    while(n&&p<31){b[p++]=(char)('0'+n%10);n/=10;}
    while(p--){draw_text(x,y,(char[]){b[p],0},color,scale);x+=6*scale;}
}

static int point_in_rect(int x,int y,RECT r) { return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h; }

static void draw_cursor(void) {
    for(int i=0;i<15;i++){put_pixel(mouse_x,mouse_y+i,0xFFFFFF);if(i<9)put_pixel(mouse_x+i,mouse_y+i,0xFFFFFF);}
}

static void draw_top_bar(const char *title) {
    fill_rect((RECT){0,0,(int)screen.w,40},bar_color());
    draw_text(14,12,"STEVEOS",fg_color(),1);
    if(title) draw_text(115,12,title,sub_color(),1);
    EFI_TIME t;
    if(!EFI_ERROR(uefi_call_wrapper(RT->GetTime,2,&t,NULL))) {
        char c[9];
        c[0]=(char)('0'+t.Hour/10);c[1]=(char)('0'+t.Hour%10);c[2]=':';
        c[3]=(char)('0'+t.Minute/10);c[4]=(char)('0'+t.Minute%10);c[5]=':';
        c[6]=(char)('0'+t.Second/10);c[7]=(char)('0'+t.Second%10);c[8]=0;
        draw_text((int)screen.w-54,12,c,sub_color(),1);
    }
}

static void draw_desktop(void) {
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},bg_color());
    draw_top_bar(NULL);
    draw_text(32,70,"WELCOME",fg_color(),3);
    draw_text(34,112,"STEVEOS IS RUNNING",sub_color(),1);
    draw_text(34,136,"UEFI DESKTOP READY",sub_color(),1);
    RECT card[4]={{30,185,230,105},{280,185,230,105},{30,310,230,105},{280,310,230,105}};
    const char *head[4]={"MEMORY","DISKS","NETWORK","TASKS"};
    for(int i=0;i<4;i++){fill_rect(card[i],card_color());draw_text(card[i].x+18,card[i].y+18,head[i],sub_color(),1);}
    draw_number(card[0].x+18,card[0].y+52,total_memory/1024/1024,fg_color(),2);
    draw_text(card[0].x+95,card[0].y+55,"MB",sub_color(),1);
    draw_number(card[1].x+18,card[1].y+52,disk_count,fg_color(),2);
    draw_text(card[2].x+18,card[2].y+52,!EFI_ERROR(network_status)?"NIC OK":"NO NIC",fg_color(),1);
    draw_number(card[3].x+18,card[3].y+52,steveos_task_count(),fg_color(),2);
    fill_rect((RECT){20,(int)screen.h-52,150,40},accent_color());
    draw_text(43,(int)screen.h-39,"START",fg_color(),1);
    draw_text(190,(int)screen.h-39,"S",sub_color(),1);
}

static void draw_start_menu(void) {
    int height=55+(int)APP_COUNT*42+10;
    RECT menu={24,(int)screen.h-height-12,420,height};
    fill_rect(menu,panel_color());
    fill_rect((RECT){menu.x,menu.y,menu.w,42},bar_color());
    draw_text(menu.x+18,menu.y+13,"STEVEOS",fg_color(),2);
    for(UINTN i=0;i<APP_COUNT;i++){
        RECT row={menu.x+10,menu.y+50+(int)i*42,menu.w-20,35};
        if((int)i==selected)fill_rect(row,accent_color());
        draw_text(row.x+12,row.y+10,apps[i].name,fg_color(),1);
    }
    RECT shut={menu.x+10,menu.y+50+(int)APP_COUNT*42,menu.w-20,35};
    if(selected==(int)APP_COUNT)fill_rect(shut,accent_color());
    draw_text(shut.x+12,shut.y+10,"SHUT DOWN",fg_color(),1);
}

static void draw_image_view(void) {
    const unsigned char *p=_binary_build_boot_raw_start;
    UINT32 iw=*(const UINT32*)p,ih=*(const UINT32*)(p+4); const unsigned char *pixels=p+8;
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},0x000000);
    if(iw&&ih){
        UINT32 maxw=screen.w,maxh=screen.h>48?screen.h-48:screen.h;
        UINT32 dw=maxw,dh=(UINT64)ih*dw/iw;
        if(dh>maxh){dh=maxh;dw=(UINT64)iw*dh/ih;}
        int ox=((int)screen.w-(int)dw)/2,oy=48+((int)maxh-(int)dh)/2;
        for(UINT32 y=0;y<dh;y++){
            UINT32 sy=(UINT64)y*ih/dh;
            for(UINT32 x=0;x<dw;x++){
                UINT32 sx=(UINT64)x*iw/dw; const unsigned char *q=pixels+((UINTN)sy*iw+sx)*4;
                put_pixel(ox+(int)x,oy+(int)y,((uint32_t)q[2]<<16)|((uint32_t)q[1]<<8)|q[0]);
            }
        }
    }
    draw_top_bar("BLEHHH VIEWER"); draw_text(15,(int)screen.h-25,"ESC TO RETURN",0xFFFFFF,1);
}

static void draw_system_view(void) {
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},bg_color()); draw_top_bar("SYSTEM INFO");
    draw_text(35,70,"SYSTEM INFORMATION",fg_color(),2);
    draw_text(35,125,"MEMORY MB",sub_color(),1);draw_number(220,125,total_memory/1024/1024,fg_color(),1);
    draw_text(35,160,"DISKS",sub_color(),1);draw_number(220,160,disk_count,fg_color(),1);
    draw_text(35,195,"TASKS",sub_color(),1);draw_number(220,195,steveos_task_count(),fg_color(),1);
    draw_text(35,230,"DISPLAY",sub_color(),1);draw_number(220,230,screen.w,fg_color(),1);draw_text(260,230,"X",sub_color(),1);draw_number(275,230,screen.h,fg_color(),1);
    draw_text(35,265,"KERNEL",sub_color(),1);draw_text(220,265,"UEFI HANDOFF",fg_color(),1);
    draw_text(35,300,"FILESYSTEM",sub_color(),1);draw_text(220,300,"SIMPLE FILE SYSTEM",fg_color(),1);
    draw_text(35,335,"INTERRUPTS",sub_color(),1);draw_text(220,335,"FOUNDATION READY",fg_color(),1);
    draw_text(35,370,"MOUSE DIVISOR",sub_color(),1);draw_number(220,370,mouse_divisor,fg_color(),1);
    draw_text(35,(int)screen.h-35,"ESC TO RETURN",sub_color(),1);
}

static void draw_network_view(void) {
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},bg_color()); draw_top_bar("NETWORK");
    draw_text(35,70,"NETWORK STATUS",fg_color(),2);
    draw_text(35,125,"FIRMWARE NIC",sub_color(),1);draw_text(220,125,!EFI_ERROR(network_status)?"AVAILABLE":"NOT AVAILABLE",fg_color(),1);
    draw_text(35,165,"TCP IP",sub_color(),1);draw_text(220,165,"STACK FOUNDATION",fg_color(),1);
    draw_text(35,205,"DNS",sub_color(),1);draw_text(220,205,"NATIVE STACK NEXT",fg_color(),1);
    draw_text(35,245,"HTTP",sub_color(),1);draw_text(220,245,"NATIVE STACK NEXT",fg_color(),1);
    draw_text(35,285,"NIC POLLING",sub_color(),1);draw_text(220,285,"UEFI SIMPLE NETWORK",fg_color(),1);
    draw_text(35,(int)screen.h-35,"ESC TO RETURN",sub_color(),1);
}

static void draw_files_view(void) {
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},bg_color()); draw_top_bar("FILES");
    draw_text(35,65,"BOOT VOLUME",fg_color(),2);
    if(!boot_root){draw_text(35,115,"NO VOLUME AVAILABLE",fg_color(),1);draw_text(35,(int)screen.h-35,"ESC TO RETURN",sub_color(),1);return;}
    EFI_FILE_PROTOCOL *dir=NULL; EFI_STATUS status=uefi_call_wrapper(boot_root->Open,5,boot_root,&dir,L".",EFI_FILE_MODE_READ,0);
    if(EFI_ERROR(status)) dir=boot_root;
    UINTN info_size; int y=110,shown=0;
    while(y<(int)screen.h-65 && shown<12){
        info_size=0; status=uefi_call_wrapper(dir->Read,3,dir,&info_size,NULL);
        if(status!=EFI_BUFFER_TOO_SMALL||!info_size)break;
        EFI_FILE_INFO *info=AllocatePool(info_size); if(!info)break;
        status=uefi_call_wrapper(dir->Read,3,dir,&info_size,info);
        if(EFI_ERROR(status)||!info_size){FreePool(info);break;}
        char name[52];UINTN j=0;while(info->FileName[j]&&j<49){name[j]=(info->FileName[j]<128)?(char)info->FileName[j]:'?';j++;}name[j]=0;
        if(j){draw_text(45,y,(info->Attribute&EFI_FILE_DIRECTORY)?"DIR":"FILE",sub_color(),1);draw_text(100,y,name,fg_color(),1);y+=25;shown++;}
        FreePool(info);
    }
    if(dir!=boot_root)uefi_call_wrapper(dir->Close,1,dir);
    if(!shown)draw_text(45,110,"EMPTY OR UNREADABLE",fg_color(),1);
    draw_text(35,(int)screen.h-35,"ESC TO RETURN",sub_color(),1);
}

static void terminal_set_status(const char *s){UINTN i=0;while(s[i]&&i<sizeof(terminal_status)-1){terminal_status[i]=s[i];i++;}terminal_status[i]=0;}
static int eqi(const char *a,const char *b){while(*a&&*b){char x=*a,y=*b;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return 0;a++;b++;}return *a==0&&*b==0;}

static void terminal_execute(void){
    terminal_line[terminal_len]=0;
    if(eqi((char*)terminal_line,"HELP"))terminal_set_status("COMMANDS: HELP SYSINFO LS NET CLEAR REBOOT SHUTDOWN");
    else if(eqi((char*)terminal_line,"SYSINFO")){view=VIEW_SYSTEM;terminal_set_status("SYSTEM INFO OPENED");}
    else if(eqi((char*)terminal_line,"LS")){view=VIEW_FILES;terminal_set_status("FILES OPENED");}
    else if(eqi((char*)terminal_line,"NET"))terminal_set_status(!EFI_ERROR(network_status)?"FIRMWARE NIC AVAILABLE":"NO FIRMWARE NIC");
    else if(eqi((char*)terminal_line,"CLEAR"))terminal_set_status("TERMINAL CLEARED");
    else if(eqi((char*)terminal_line,"REBOOT"))uefi_call_wrapper(RT->ResetSystem,4,EfiResetCold,EFI_SUCCESS,0,NULL);
    else if(eqi((char*)terminal_line,"SHUTDOWN"))uefi_call_wrapper(RT->ResetSystem,4,EfiResetShutdown,EFI_SUCCESS,0,NULL);
    else if(terminal_len)terminal_set_status("UNKNOWN COMMAND - TYPE HELP");
    terminal_len=0;terminal_line[0]=0;
}

static void draw_terminal_view(void){
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},0x070A0D);draw_top_bar("TERMINAL");
    draw_text(30,75,"STEVEOS TERMINAL",0xFFFFFF,2);
    draw_text(30,120,"TYPE HELP FOR COMMANDS",0xB8C2CC,1);
    draw_text(30,165,"STATUS",0x7F8C99,1);draw_text(30,195,(char*)terminal_status,0xFFFFFF,1);
    draw_text(30,(int)screen.h-75,"STEVE> ",0xFFFFFF,1);draw_text(85,(int)screen.h-75,(char*)terminal_line,0xFFFFFF,1);
    draw_text(30,(int)screen.h-35,"ENTER RUNS COMMAND - ESC RETURNS",0x7F8C99,1);
}

static void draw_settings_view(void){
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},bg_color());draw_top_bar("SETTINGS");
    draw_text(35,65,"SETTINGS",fg_color(),2);
    fill_rect((RECT){35,110,500,65},card_color());draw_text(55,130,"LIGHT THEME",sub_color(),1);draw_text(350,130,settings_light?"ON":"OFF",fg_color(),1);
    fill_rect((RECT){35,195,500,65},card_color());draw_text(55,215,"MOUSE DIVISOR",sub_color(),1);draw_number(350,215,mouse_divisor,fg_color(),1);
    draw_text(35,290,"ENTER TO TOGGLE THEME",sub_color(),1);draw_text(35,320,"UP/DOWN TO CHANGE MOUSE",sub_color(),1);
    draw_text(35,(int)screen.h-35,"ESC TO RETURN",sub_color(),1);
}

static void draw_installer(void){
    fill_rect((RECT){0,0,(int)screen.w,(int)screen.h},bg_color());draw_top_bar("INSTALLER");
    RECT p={(int)screen.w/2-310,(int)screen.h/2-175,620,350};fill_rect(p,panel_color());
    draw_text(p.x+35,p.y+30,"STEVEOS INSTALLER",fg_color(),2);draw_text(p.x+35,p.y+85,"AVAILABLE DISKS",sub_color(),1);draw_number(p.x+255,p.y+85,disk_count,fg_color(),1);
    draw_text(p.x+35,p.y+125,"SAFE MODE",sub_color(),1);draw_text(p.x+255,p.y+125,"DISK WRITES LOCKED",fg_color(),1);
    draw_text(p.x+35,p.y+165,"TARGET SELECTION IS REQUIRED",sub_color(),1);draw_text(p.x+35,p.y+189,"BEFORE ANY INSTALL ACTION.",sub_color(),1);
    fill_rect((RECT){p.x+35,p.y+245,180,48},accent_color());fill_rect((RECT){p.x+405,p.y+245,180,48},accent_color());
    draw_text(p.x+95,p.y+262,"BACK",fg_color(),1);draw_text(p.x+444,p.y+262,"RESCAN",fg_color(),1);
}

static void redraw(void){
    if(view==VIEW_IMAGE)draw_image_view();else if(view==VIEW_SYSTEM)draw_system_view();else if(view==VIEW_NETWORK)draw_network_view();else if(view==VIEW_FILES)draw_files_view();else if(view==VIEW_TERMINAL)draw_terminal_view();else if(view==VIEW_SETTINGS)draw_settings_view();else if(view==VIEW_INSTALLER)draw_installer();else{draw_desktop();if(start_open)draw_start_menu();}
    draw_cursor();
}

static void open_selected(void){
    if(selected<(int)APP_COUNT)view=apps[selected].view;
    else uefi_call_wrapper(RT->ResetSystem,4,EfiResetShutdown,EFI_SUCCESS,0,NULL);
    start_open=0;redraw();
}

static void handle_mouse(void){
    if(!mouse)return;EFI_SIMPLE_POINTER_STATE s;EFI_STATUS st=uefi_call_wrapper(mouse->GetState,2,mouse,&s);if(EFI_ERROR(st))return;
    mouse_x+=s.RelativeMovementX/(INT32)mouse_divisor;mouse_y+=s.RelativeMovementY/(INT32)mouse_divisor;
    if(mouse_x<0)mouse_x=0;if(mouse_y<0)mouse_y=0;if(mouse_x>=(int)screen.w)mouse_x=(int)screen.w-1;if(mouse_y>=(int)screen.h)mouse_y=(int)screen.h-1;
    int left=s.LeftButton?1:0,clicked=left&&!mouse_left_down;mouse_left_down=left;
    if(!clicked){redraw();return;}
    if(view!=VIEW_DESKTOP){
        if(view==VIEW_SETTINGS){if(point_in_rect(mouse_x,mouse_y,(RECT){35,110,500,65}))settings_light=!settings_light;else if(point_in_rect(mouse_x,mouse_y,(RECT){35,195,500,65})){mouse_divisor++;if(mouse_divisor>5)mouse_divisor=1;}else if(point_in_rect(mouse_x,mouse_y,(RECT){35,(int)screen.h-55,220,35}))view=VIEW_DESKTOP;}
        else if(view==VIEW_INSTALLER){RECT p={(int)screen.w/2-310,(int)screen.h/2-175,620,350};if(point_in_rect(mouse_x,mouse_y,(RECT){p.x+35,p.y+245,180,48}))view=VIEW_DESKTOP;else if(point_in_rect(mouse_x,mouse_y,(RECT){p.x+405,p.y+245,180,48}))disk_count=steveos_count_disks();}
        else view=VIEW_DESKTOP;start_open=0;redraw();return;
    }
    if(point_in_rect(mouse_x,mouse_y,(RECT){20,(int)screen.h-58,150,48})){start_open=!start_open;selected=0;redraw();return;}
    if(start_open){int h=55+(int)APP_COUNT*42+10;RECT menu={24,(int)screen.h-h-12,420,h};for(UINTN i=0;i<APP_COUNT+1;i++){RECT row={menu.x+10,menu.y+50+(int)i*42,menu.w-20,35};if(point_in_rect(mouse_x,mouse_y,row)){selected=(int)i;open_selected();return;}}}
}

static void handle_terminal_key(EFI_INPUT_KEY key){
    if(key.ScanCode==SCAN_ESC){view=VIEW_DESKTOP;terminal_len=0;redraw();return;}
    if(key.UnicodeChar==CHAR_CARRIAGE_RETURN){terminal_execute();redraw();return;}
    if(key.UnicodeChar==CHAR_BACKSPACE){if(terminal_len)terminal_line[--terminal_len]=0;redraw();return;}
    CHAR16 c=key.UnicodeChar;
    if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c==' '){if(terminal_len<sizeof(terminal_line)-1){if(c>='a'&&c<='z')c=(CHAR16)(c-32);terminal_line[terminal_len++]=(CHAR8)c;terminal_line[terminal_len]=0;}}
    redraw();
}

static void handle_key(EFI_INPUT_KEY key){
    if(view==VIEW_TERMINAL){handle_terminal_key(key);return;}
    if(view!=VIEW_DESKTOP){
        if(view==VIEW_SETTINGS){if(key.ScanCode==SCAN_ESC){view=VIEW_DESKTOP;redraw();return;}if(key.ScanCode==SCAN_UP){if(mouse_divisor>1)mouse_divisor--;redraw();return;}if(key.ScanCode==SCAN_DOWN){if(mouse_divisor<5)mouse_divisor++;redraw();return;}if(key.UnicodeChar==CHAR_CARRIAGE_RETURN||key.UnicodeChar==' '){settings_light=!settings_light;redraw();return;}}
        if(key.ScanCode==SCAN_ESC||key.UnicodeChar=='b'||key.UnicodeChar=='B'){view=VIEW_DESKTOP;redraw();}return;
    }
    if(key.UnicodeChar=='s'||key.UnicodeChar=='S'){start_open=!start_open;selected=0;redraw();return;}
    if(!start_open)return;
    if(key.ScanCode==SCAN_UP){selected--;if(selected<0)selected=(int)APP_COUNT;redraw();return;}
    if(key.ScanCode==SCAN_DOWN){selected++;if(selected>(int)APP_COUNT)selected=0;redraw();return;}
    if(key.ScanCode==SCAN_ESC){start_open=0;redraw();return;}
    if(key.UnicodeChar==CHAR_CARRIAGE_RETURN||key.UnicodeChar==' ')open_selected();
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle,EFI_SYSTEM_TABLE *system_table){
    InitializeLib(image_handle,system_table);
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;EFI_STATUS status=uefi_call_wrapper(BS->LocateProtocol,3,&gEfiGraphicsOutputProtocolGuid,NULL,(void**)&gop);if(EFI_ERROR(status)||!gop)return status;
    UINT32 best=gop->Mode->Mode,area_best=0;
    for(UINT32 m=0;m<gop->Mode->MaxMode;m++){EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info=NULL;UINTN sz=0;status=uefi_call_wrapper(gop->QueryMode,4,gop,m,&sz,&info);if(EFI_ERROR(status))continue;UINT32 area=info->HorizontalResolution*info->VerticalResolution;if(area>area_best){area_best=area;best=m;}}
    uefi_call_wrapper(gop->SetMode,2,gop,best);
    screen.w=gop->Mode->Info->HorizontalResolution;screen.h=gop->Mode->Info->VerticalResolution;screen.stride=gop->Mode->Info->PixelsPerScanLine;screen.format=gop->Mode->Info->PixelFormat;screen.mask=gop->Mode->Info->PixelInformation;screen.fb=(uint32_t*)(UINTN)gop->Mode->FrameBufferBase;

    EFI_GUID simple_pointer_guid={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
    status=uefi_call_wrapper(BS->LocateProtocol,3,&simple_pointer_guid,NULL,(void**)&mouse);
    if(!EFI_ERROR(status)&&mouse){uefi_call_wrapper(mouse->Reset,2,mouse,FALSE);mouse_event=mouse->WaitForInput;}

    EFI_LOADED_IMAGE *loaded=NULL;
    if(!EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol,3,image_handle,&gEfiLoadedImageProtocolGuid,(void**)&loaded))&&loaded)steveos_fs_open_volume(loaded->DeviceHandle,&boot_root);

    status=steveos_memory_init();total_memory=EFI_ERROR(status)?0:steveos_total_memory_bytes();
    disk_count=steveos_count_disks();network_status=steveos_http_test();steveos_tasks_init();steveos_interrupts_init();terminal_set_status("READY - TYPE HELP");

    status=uefi_call_wrapper(BS->CreateEvent,5,EVT_TIMER,TPL_CALLBACK,NULL,NULL,&timer_event);if(EFI_ERROR(status))return status;
    uefi_call_wrapper(BS->SetTimer,3,timer_event,TimerPeriodic,5000000);
    EFI_EVENT events[3];UINTN event_count=0;events[event_count++]=ST->ConIn->WaitForKey;if(mouse_event)events[event_count++]=mouse_event;events[event_count++]=timer_event;
    mouse_x=(int)screen.w/2;mouse_y=(int)screen.h/2;mouse_left_down=0;redraw();
    for(;;){UINTN index=0;status=uefi_call_wrapper(BS->WaitForEvent,3,event_count,events,&index);if(EFI_ERROR(status))continue;if(events[index]==ST->ConIn->WaitForKey){EFI_INPUT_KEY key;if(!EFI_ERROR(uefi_call_wrapper(ST->ConIn->ReadKeyStroke,2,ST->ConIn,&key)))handle_key(key);}else if(mouse_event&&events[index]==mouse_event)handle_mouse();else if(events[index]==timer_event)redraw();}
}
