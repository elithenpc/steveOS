#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "shell.h"
#include "memory.h"
#include "storage.h"
#include "network.h"
#include "kernel.h"
#include "tasks.h"
#include "interrupts.h"
#include "fs.h"

extern const unsigned char _binary_build_boot_raw_start[];
extern const unsigned char _binary_build_boot_raw_end[];

#define MAX_WINDOWS 10
#define TERMINAL_LINES 13
#define TERMINAL_COLS 76
#define MAX_FILES 14
#define MAX_TARGETS 8

typedef struct { UINT32 w,h,stride; EFI_GRAPHICS_PIXEL_FORMAT format; EFI_PIXEL_BITMASK mask; UINT32 *fb; } SCREEN;
typedef struct { int x,y,w,h; } RECT;
typedef enum { APP_IMAGE=0, APP_SYSTEM, APP_NETWORK, APP_FILES, APP_TERMINAL, APP_TASKS, APP_SETTINGS, APP_INSTALLER, APP_ABOUT } APP_ID;

typedef struct {
    int open;
    int minimized;
    int maximized;
    int x,y,w,h;
    int drag;
    int dx,dy;
    APP_ID id;
    char title[32];
} WINDOW;

static SCREEN s;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
static EFI_SIMPLE_POINTER_PROTOCOL *mouse;
static EFI_EVENT mouse_event;
static EFI_EVENT timer_event;
static EFI_FILE_PROTOCOL *boot_root;
static EFI_HANDLE image_handle;
static EFI_HANDLE source_device;
static EFI_HANDLE targets[MAX_TARGETS];
static UINTN target_count;
static int target_selected=-1;
static int installer_message;
static UINT64 total_memory;
static UINTN disk_count;
static EFI_STATUS network_status;
static UINT32 mouse_divisor=2;
static int light_theme=0;
static int start_open=0;
static int start_selected=0;
static int active_window=-1;
static int mouse_x,mouse_y,mouse_left;
static WINDOW windows[MAX_WINDOWS];
static CHAR8 term[TERMINAL_LINES][TERMINAL_COLS+1];
static int term_count;
static CHAR8 term_input[96];
static UINTN term_len;
static CHAR8 files[MAX_FILES][64];
static int file_count;
static int files_loaded;

static UINT32 bg(void){return light_theme?0xE9EDF2:0x111722;}
static UINT32 panel(void){return light_theme?0xF8FAFC:0x202734;}
static UINT32 panel2(void){return light_theme?0xE1E6EC:0x283141;}
static UINT32 top(void){return light_theme?0xD2D8E0:0x161D29;}
static UINT32 fg(void){return light_theme?0x18202B:0xF7F9FC;}
static UINT32 sub(void){return light_theme?0x4B5868:0xAEB8C7;}
static UINT32 accent(void){return light_theme?0x7A8797:0x46566D;}
static UINT32 danger(void){return 0xA83D4B;}

static void px(int x,int y,UINT32 c){if(x>=0&&y>=0&&(UINT32)x<s.w&&(UINT32)y<s.h)s.fb[(UINTN)y*s.stride+(UINTN)x]=c;}
static void rect(RECT r,UINT32 c){int x0=r.x<0?0:r.x,y0=r.y<0?0:r.y,x1=r.x+r.w,y1=r.y+r.h;if(x1>(int)s.w)x1=s.w;if(y1>(int)s.h)y1=s.h;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)px(x,y,c);}
static void round_rect(RECT r,UINT32 c,int rad){if(rad<1){rect(r,c);return;}for(int y=0;y<r.h;y++){int inset=0;if(y<rad){int d=rad-1-y;inset=(d*d)/(rad?rad:1);}else if(y>=r.h-rad){int d=y-(r.h-rad);inset=(d*d)/(rad?rad:1);}rect((RECT){r.x+inset,y+r.y,r.w-2*inset,1},c);}}
static void shadow(RECT r){rect((RECT){r.x+5,r.y+6,r.w,r.h},0x090D14);round_rect(r,panel(),8);}

static const UINT8 letters[26][5]={
{0x7e,0x11,0x11,0x7e,0},{0x7f,0x49,0x49,0x36,0},{0x3e,0x41,0x41,0x22,0},{0x7f,0x41,0x41,0x3e,0},
{0x7f,0x49,0x49,0x41,0},{0x7f,0x09,0x09,0x01,0},{0x3e,0x41,0x51,0x72,0},{0x7f,0x08,0x08,0x7f,0},
{0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0},{0x7f,0x08,0x14,0x63,0},{0x7f,0x40,0x40,0x40,0},
{0x7f,0x06,0x18,0x06,0x7f},{0x7f,0x06,0x18,0x7f,0},{0x3e,0x41,0x41,0x3e,0},{0x7f,0x09,0x09,0x06,0},
{0x3e,0x41,0x61,0x7e,0},{0x7f,0x09,0x19,0x66,0},{0x26,0x49,0x49,0x32,0},{0x01,0x7f,0x01,0x01,0},
{0x3f,0x40,0x40,0x3f,0},{0x1f,0x60,0x60,0x1f,0},{0x7f,0x30,0x0c,0x30,0x7f},{0x63,0x14,0x08,0x14,0x63},
{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};
static const UINT8 digits[10][5]={
{0x3e,0x45,0x49,0x51,0x3e},{0,0x21,0x7f,1,0},{0x23,0x45,0x49,0x49,0x31},{0x22,0x41,0x49,0x49,0x36},{0x0c,0x14,0x24,0x7f,4},
{0x7a,0x49,0x49,0x49,0x46},{0x3e,0x49,0x49,0x49,0x26},{0x40,0x47,0x48,0x50,0x60},{0x36,0x49,0x49,0x49,0x36},{0x32,0x49,0x49,0x49,0x3e}
};

static void glyph(int x,int y,char c,UINT32 ccol,int sc){const UINT8 *g=0;if(c>='A'&&c<='Z')g=letters[c-'A'];else if(c>='a'&&c<='z')g=letters[c-'a'];else if(c>='0'&&c<='9')g=digits[c-'0'];if(g){for(int a=0;a<5;a++)for(int b=0;b<7;b++)if(g[a]&(1u<<b))rect((RECT){x+a*sc,y+b*sc,sc,sc},ccol);return;}if(c=='-')rect((RECT){x,y+3*sc,5*sc,sc},ccol);else if(c=='.')rect((RECT){x,y+6*sc,sc,sc},ccol);else if(c==':'){rect((RECT){x,y+2*sc,sc,sc},ccol);rect((RECT){x,y+6*sc,sc,sc},ccol);}else if(c=='_')rect((RECT){x,y+6*sc,5*sc,sc},ccol);else if(c=='/')rect((RECT){x+2*sc,y,sc,7*sc},ccol);else if(c=='>'){rect((RECT){x,y+2*sc,sc,sc},ccol);rect((RECT){x+sc,y+3*sc,sc,sc},ccol);rect((RECT){x,y+4*sc,sc,sc},ccol);}else if(c=='%'){rect((RECT){x,y,sc,sc},ccol);rect((RECT){x+4*sc,y+6*sc,sc,sc},ccol);rect((RECT){x+3*sc,y+sc,sc,4*sc},ccol);}}
static void text(int x,int y,const char *v,UINT32 ccol,int sc){while(*v){if(*v==' ')x+=6*sc;else{glyph(x,y,*v,ccol,sc);x+=6*sc;}v++;}}
static void num(int x,int y,UINT64 n,UINT32 ccol,int sc){char b[32];int p=0;if(!n)b[p++]='0';while(n&&p<31){b[p++]=(char)('0'+n%10);n/=10;}while(p--) {glyph(x,y,b[p],ccol,sc);x+=6*sc;}}
static int inside(int x,int y,RECT r){return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}

static void cursor(void){for(int i=0;i<16;i++){px(mouse_x,mouse_y+i,0xFFFFFF);if(i<10)px(mouse_x+i,mouse_y+i,0xFFFFFF);}}

static void task_add_shell(void){if(steveos_task_count()==0)steveos_task_create((UINT64)(UINTN)&steveos_shell_start);}
static void refresh_stats(void){disk_count=steveos_count_disks();memory_status=steveos_memory_init();total_memory=steveos_total_memory_bytes();network_status=steveos_http_test();}

static void term_add(const char *v){if(!v)return;int i=0;if(term_count<TERMINAL_LINES){while(i<TERMINAL_COLS&&v[i]){term[term_count][i]=v[i];i++;}term[term_count][i]=0;term_count++;}else{for(int r=1;r<TERMINAL_LINES;r++)for(int c=0;c<=TERMINAL_COLS;c++)term[r-1][c]=term[r][c];while(i<TERMINAL_COLS&&v[i]){term[TERMINAL_LINES-1][i]=v[i];i++;}term[TERMINAL_LINES-1][i]=0;}}
static int eq(const char *a,const char *b){while(*a&&*b&&*a==*b){a++;b++;}return *a==0&&*b==0;}
static void terminal_command(void){term_input[term_len]=0;term_add(term_input);if(eq((char*)term_input,"HELP")){term_add("HELP SYSINFO TASKS LS PWD DATE NET CLEAR WINDOWS");term_add("REBOOT SHUTDOWN INSTALLER ABOUT THEME");}
else if(eq((char*)term_input,"SYSINFO")){term_add("STEVEOS SYSTEM");term_add("MEMORY MB:");term_add("DISKS:");}
else if(eq((char*)term_input,"TASKS")){term_add("TASK MANAGER OPEN WITH TASKS APP");}
else if(eq((char*)term_input,"LS")){if(!boot_root)term_add("NO BOOT VOLUME");else{if(!files_loaded)files_loaded=1;term_add("FILES ARE SHOWN IN FILE MANAGER");}}
else if(eq((char*)term_input,"PWD")){term_add("\\");}
else if(eq((char*)term_input,"DATE")){EFI_TIME t;if(!EFI_ERROR(uefi_call_wrapper(RT->GetTime,2,&t,NULL))){char d[24];d[0]=(char)('0'+t.Day/10);d[1]=(char)('0'+t.Day%10);d[2]='-';d[3]=(char)('0'+t.Month/10);d[4]=(char)('0'+t.Month%10);d[5]='-';d[6]=(char)('0'+(t.Year/1000)%10);d[7]=(char)('0'+(t.Year/100)%10);d[8]=(char)('0'+(t.Year/10)%10);d[9]=(char)('0'+t.Year%10);d[10]=0;term_add(d);}}
else if(eq((char*)term_input,"NET")){term_add(EFI_ERROR(network_status)?"NIC NOT AVAILABLE":"NIC AVAILABLE");}
else if(eq((char*)term_input,"CLEAR")){term_count=0;}
else if(eq((char*)term_input,"WINDOWS")){term_add("WINDOW MANAGER ACTIVE");}
else if(eq((char*)term_input,"REBOOT")){uefi_call_wrapper(RT->ResetSystem,4,EfiResetCold,EFI_SUCCESS,0,NULL);}
else if(eq((char*)term_input,"SHUTDOWN")){uefi_call_wrapper(RT->ResetSystem,4,EfiResetShutdown,EFI_SUCCESS,0,NULL);}
else if(eq((char*)term_input,"INSTALLER")){installer_message=0;}
else if(eq((char*)term_input,"ABOUT")){term_add("STEVEOS CUSTOM UEFI DESKTOP");}
else if(eq((char*)term_input,"THEME")){light_theme=!light_theme;}
else if(term_len)term_add("UNKNOWN COMMAND - TYPE HELP");term_len=0;term_input[0]=0;}

static void files_scan(void){if(!boot_root)return;file_count=0;uefi_call_wrapper(boot_root->SetPosition,2,boot_root,0);UINT8 *buf=AllocatePool(4096);if(!buf)return;while(file_count<MAX_FILES){UINTN sz=4096;EFI_STATUS st=uefi_call_wrapper(boot_root->Read,3,boot_root,&sz,buf);if(EFI_ERROR(st)||sz==0)break;EFI_FILE_INFO *fi=(EFI_FILE_INFO*)buf;int j=0;while(j<63&&fi->FileName[j]){files[file_count][j]=(fi->FileName[j]<128)?(char)fi->FileName[j]:'?';j++;}files[file_count][j]=0;file_count++;}FreePool(buf);files_loaded=1;}

static void discover_targets(void){target_count=0;target_selected=-1;UINTN sz=0;EFI_HANDLE *hs=0;if(uefi_call_wrapper(BS->LocateHandle,5,ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&sz,NULL)!=EFI_BUFFER_TOO_SMALL)return;hs=AllocatePool(sz);if(!hs)return;if(EFI_ERROR(uefi_call_wrapper(BS->LocateHandle,5,ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&sz,hs))){FreePool(hs);return;}for(UINTN i=0;i<sz/sizeof(EFI_HANDLE)&&target_count<MAX_TARGETS;i++)if(hs[i]!=source_device)targets[target_count++]=hs[i];FreePool(hs);}

static EFI_STATUS write_install(void){if(target_selected<0||target_selected>=(int)target_count)return EFI_INVALID_PARAMETER;VOID *data=0;UINTN size=0;EFI_FILE_PROTOCOL *src=0,*root=0,*efi=0,*boot=0,*out=0;EFI_STATUS st=steveos_fs_open_volume(source_device,&src);if(EFI_ERROR(st))return st;st=steveos_fs_read_file(src,L"\\EFI\\BOOT\\BOOTX64.EFI",&data,&size);uefi_call_wrapper(src->Close,1,src);if(EFI_ERROR(st))return st;st=steveos_fs_open_volume(targets[target_selected],&root);if(EFI_ERROR(st)){FreePool(data);return st;}st=uefi_call_wrapper(root->Open,5,root,&efi,L"EFI",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);if(EFI_ERROR(st))goto done;st=uefi_call_wrapper(efi->Open,5,efi,&boot,L"BOOT",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);if(EFI_ERROR(st))goto done;st=uefi_call_wrapper(boot->Open,5,boot,&out,L"BOOTX64.EFI",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);if(!EFI_ERROR(st)){UINTN ws=size;st=uefi_call_wrapper(out->SetPosition,2,out,0);if(!EFI_ERROR(st))st=uefi_call_wrapper(out->Write,3,out,&ws,data);uefi_call_wrapper(out->Close,1,out);}done:if(boot)uefi_call_wrapper(boot->Close,1,boot);if(efi)uefi_call_wrapper(efi->Close,1,efi);if(root)uefi_call_wrapper(root->Close,1,root);FreePool(data);return st;}

static int window_open(APP_ID id){for(int i=0;i<MAX_WINDOWS;i++)if(windows[i].open&&windows[i].id==id)return i;return -1;}
static int open_window(APP_ID id,const char *title,int x,int y,int w,int h){int old=window_open(id);if(old>=0){windows[old].minimized=0;active_window=old;return old;}for(int i=0;i<MAX_WINDOWS;i++)if(!windows[i].open){windows[i].open=1;windows[i].minimized=0;windows[i].maximized=0;windows[i].drag=0;windows[i].x=x;windows[i].y=y;windows[i].w=w;windows[i].h=h;windows[i].id=id;int k=0;while(title[k]&&k<31){windows[i].title[k]=title[k];k++;}windows[i].title[k]=0;active_window=i;return i;}return -1;}
static void focus_window(int i){if(i>=0&&i<MAX_WINDOWS&&windows[i].open){active_window=i;}}
static void close_window(int i){if(i>=0&&i<MAX_WINDOWS)windows[i].open=0;if(active_window==i)active_window=-1;}

static void win_buttons(WINDOW *w,int i){int bx=w->x+w->w;rect((RECT){bx-21,w->y+7,14,14},accent());rect((RECT){bx-41,w->y+7,14,14},panel2());rect((RECT){bx-61,w->y+7,14,14},panel2());glyph(bx-18,w->y+9,'X',fg(),1);glyph(bx-38,w->y+9,'O',fg(),1);glyph(bx-58,w->y+9,'_',fg(),1);}
static RECT content_rect(WINDOW *w){return (RECT){w->x,w->y+30,w->w,w->h-30};}

static void draw_terminal(WINDOW *w){RECT c=content_rect(w);rect(c,0x080B10);for(int i=0;i<term_count;i++)text(c.x+12,c.y+10+i*15,(char*)term[i],0xC9D6E6,1);text(c.x+12,c.y+c.h-25,"STEVE>",0x7F9CC2,1);text(c.x+54,c.y+c.h-25,(char*)term_input,0xFFFFFF,1);rect((RECT){c.x+54+(int)term_len*6,c.y+c.h-27,2,12},0xFFFFFF);}
static void draw_tasks(WINDOW *w){RECT c=content_rect(w);text(c.x+16,c.y+15,"TASK MANAGER",fg(),2);text(c.x+16,c.y+52,"ID",sub(),1);text(c.x+60,c.y+52,"STATE",sub(),1);text(c.x+160,c.y+52,"INSTRUCTION POINTER",sub(),1);for(UINT32 i=0;i<steveos_task_count()&&i<10;i++){STEVEOS_TASK t;if(EFI_ERROR(steveos_task_get(i,&t)))continue;int yy=c.y+75+(int)i*28;num(c.x+16,yy,t.id,fg(),1);const char *st=t.state==STEVEOS_TASK_READY?"READY":t.state==STEVEOS_TASK_RUNNING?"RUNNING":t.state==STEVEOS_TASK_SLEEPING?"SLEEP":"EMPTY";text(c.x+60,yy,st,fg(),1);num(c.x+160,yy,t.instruction_pointer,fg(),1);}}
static void draw_system(WINDOW *w){RECT c=content_rect(w);text(c.x+18,c.y+18,"SYSTEM HEALTH",fg(),2);text(c.x+18,c.y+60,"MEMORY MB",sub(),1);num(c.x+180,c.y+60,total_memory/1024/1024,fg(),1);text(c.x+18,c.y+92,"DISKS",sub(),1);num(c.x+180,c.y+92,disk_count,fg(),1);text(c.x+18,c.y+124,"TASKS",sub(),1);num(c.x+180,c.y+124,steveos_task_count(),fg(),1);text(c.x+18,c.y+156,"DISPLAY",sub(),1);num(c.x+180,c.y+156,s.w,fg(),1);text(c.x+222,c.y+156,"X",sub(),1);num(c.x+238,c.y+156,s.h,fg(),1);text(c.x+18,c.y+188,"KERNEL",sub(),1);text(c.x+180,c.y+188,"UEFI HANDOFF READY",fg(),1);}
static void draw_network(WINDOW *w){RECT c=content_rect(w);text(c.x+18,c.y+18,"NETWORK",fg(),2);text(c.x+18,c.y+62,"FIRMWARE NIC",sub(),1);text(c.x+180,c.y+62,EFI_ERROR(network_status)?"NOT AVAILABLE":"AVAILABLE",fg(),1);text(c.x+18,c.y+96,"IP STACK",sub(),1);text(c.x+180,c.y+96,"FOUNDATION",fg(),1);text(c.x+18,c.y+130,"DNS",sub(),1);text(c.x+180,c.y+130,"READY FOR NATIVE STACK",fg(),1);text(c.x+18,c.y+164,"HTTP",sub(),1);text(c.x+180,c.y+164,"READY FOR NATIVE STACK",fg(),1);}
static void draw_files(WINDOW *w){RECT c=content_rect(w);text(c.x+16,c.y+15,"BOOT VOLUME",fg(),2);if(!boot_root){text(c.x+16,c.y+55,"NO FILESYSTEM",fg(),1);return;}if(!files_loaded)files_scan();for(int i=0;i<file_count;i++)text(c.x+18,c.y+55+i*22,files[i],fg(),1);}
static void draw_settings(WINDOW *w){RECT c=content_rect(w);text(c.x+18,c.y+18,"SETTINGS",fg(),2);text(c.x+18,c.y+62,"THEME",sub(),1);text(c.x+180,c.y+62,light_theme?"LIGHT":"DARK",fg(),1);round_rect((RECT){c.x+18,c.y+94,170,38},accent(),6);text(c.x+48,c.y+106,"TOGGLE THEME",fg(),1);text(c.x+18,c.y+162,"MOUSE SPEED",sub(),1);num(c.x+180,c.y+162,mouse_divisor,fg(),1);round_rect((RECT){c.x+18,c.y+194,75,38},panel2(),6);text(c.x+40,c.y+206,"SLOW",fg(),1);round_rect((RECT){c.x+110,c.y+194,95,38},panel2(),6);text(c.x+128,c.y+206,"FAST",fg(),1);}
static void draw_about(WINDOW *w){RECT c=content_rect(w);text(c.x+20,c.y+20,"STEVEOS",fg(),3);text(c.x+20,c.y+68,"CUSTOM UEFI DESKTOP",sub(),1);text(c.x+20,c.y+104,"GRAPHICS WINDOW SHELL",fg(),1);text(c.x+20,c.y+140,"TERMINAL TASKS FILES",fg(),1);text(c.x+20,c.y+176,"INSTALLER AND TOOLS",fg(),1);}
static void draw_image(WINDOW *w){RECT c=content_rect(w);rect(c,0x000000);const UINT8 *p=_binary_build_boot_raw_start;UINT32 iw=*(UINT32*)p,ih=*(UINT32*)(p+4);const UINT8 *pix=p+8;if(iw&&ih){UINT32 dw=(UINT32)c.w,dh=(UINT64)ih*dw/iw;if(dh>(UINT32)c.h){dh=c.h;dw=(UINT64)iw*dh/ih;}int ox=c.x+(c.w-(int)dw)/2,oy=c.y+(c.h-(int)dh)/2;for(UINT32 y=0;y<dh;y++){UINT32 sy=(UINT64)y*ih/dh;for(UINT32 x=0;x<dw;x++){UINT32 sx=(UINT64)x*iw/dw;const UINT8*q=pix+((UINTN)sy*iw+sx)*4;px(ox+(int)x,oy+(int)y,((UINT32)q[2]<<16)|((UINT32)q[1]<<8)|q[0]);}}}}
static void draw_installer(WINDOW *w){RECT c=content_rect(w);text(c.x+16,c.y+15,"INSTALL STEVEOS",fg(),2);text(c.x+16,c.y+50,"SELECT EFI VOLUME",sub(),1);for(UINTN i=0;i<target_count;i++){RECT r={c.x+15,c.y+75+(int)i*34,c.w-30,28};if((int)i==target_selected)rect(r,accent());text(r.x+10,r.y+8,"EFI VOLUME",fg(),1);num(r.x+105,r.y+8,i+1,fg(),1);if(targets[i]==source_device)text(r.x+135,r.y+8,"SOURCE",sub(),1);}round_rect((RECT){c.x+15,c.y+c.h-55,210,38},accent(),6);text(c.x+42,c.y+c.h-44,"OVERWRITE INSTALL",fg(),1);if(installer_message)text(c.x+240,c.y+c.h-44,installer_message==1?"INSTALL COMPLETE":"INSTALL FAILED",fg(),1);}

static void draw_window(WINDOW *w,int i){if(!w->open||w->minimized)return;shadow((RECT){w->x,w->y,w->w,w->h});rect((RECT){w->x,w->y,w->w,30},top());text(w->x+10,w->y+9,w->title,fg(),1);win_buttons(w,i);if(w->id==APP_TERMINAL)draw_terminal(w);else if(w->id==APP_TASKS)draw_tasks(w);else if(w->id==APP_SYSTEM)draw_system(w);else if(w->id==APP_NETWORK)draw_network(w);else if(w->id==APP_FILES)draw_files(w);else if(w->id==APP_SETTINGS)draw_settings(w);else if(w->id==APP_IMAGE)draw_image(w);else if(w->id==APP_INSTALLER)draw_installer(w);else if(w->id==APP_ABOUT)draw_about(w);}

static void desktop(void){rect((RECT){0,0,(int)s.w,(int)s.h},bg());for(int y=44;y<(int)s.h-56;y+=80)rect((RECT){0,y,(int)s.w,1},light_theme?0xDCE2E8:0x141C28);rect((RECT){0,0,(int)s.w,42},top());text(15,12,"STEVEOS",fg(),1);text(110,12,"DESKTOP",sub(),1);EFI_TIME t;if(!EFI_ERROR(uefi_call_wrapper(RT->GetTime,2,&t,NULL))){char c[9];c[0]='0'+t.Hour/10;c[1]='0'+t.Hour%10;c[2]=':';c[3]='0'+t.Minute/10;c[4]='0'+t.Minute%10;c[5]=':';c[6]='0'+t.Second/10;c[7]='0'+t.Second%10;c[8]=0;text((int)s.w-54,12,c,sub(),1);}text(34,76,"WELCOME",fg(),3);text(36,116,"STEVEOS IS RUNNING",sub(),1);round_rect((RECT){30,158,240,110},panel(),10);text(48,176,"SYSTEM",sub(),1);text(48,204,"MEMORY",sub(),1);num(150,202,total_memory/1024/1024,fg(),2);text(204,204,"MB",sub(),1);text(48,235,"TASKS",sub(),1);num(150,233,steveos_task_count(),fg(),2);round_rect((RECT){292,158,240,110},panel(),10);text(310,176,"NETWORK",sub(),1);text(310,206,EFI_ERROR(network_status)?"NIC OFFLINE":"NIC ONLINE",fg(),2);text(310,239,"FIRMWARE NETWORK",sub(),1);round_rect((RECT){30,292,502,58},panel2(),10);text(48,311,"DOUBLE CLICK OR USE START",fg(),1);text(48,335,"TERMINAL  TASKS  FILES  SETTINGS",sub(),1);round_rect((RECT){18,(int)s.h-52,155,38},accent(),8);text(50,(int)s.h-41,"START",fg(),1);}

static void start_menu(void){int h=70+(int)9*37;RECT m={18,(int)s.h-h-10,440,h};shadow(m);rect((RECT){m.x,m.y,m.w,42},top());text(m.x+16,m.y+13,"STEVEOS",fg(),2);const char *a[]={"BLEHHH VIEWER","SYSTEM INFO","NETWORK","FILES","TERMINAL","TASK MANAGER","SETTINGS","INSTALLER","ABOUT"};for(int i=0;i<9;i++){RECT r={m.x+10,m.y+48+i*37,m.w-20,30};if(i==start_selected)rect(r,accent());text(r.x+12,r.y+9,a[i],fg(),1);} }

static void redraw_all(void){desktop();for(int i=0;i<MAX_WINDOWS;i++)draw_window(&windows[i],i);if(start_open)start_menu();cursor();}

static void launch_app(int n){if(n<0||n>=9)return;APP_ID id=(APP_ID)n;const char *titles[]={"BLEHHH VIEWER","SYSTEM INFO","NETWORK","FILES","TERMINAL","TASK MANAGER","SETTINGS","INSTALLER","ABOUT"};int x=120+(n%3)*35,y=90+(n%3)*35;open_window(id,titles[n],x,y,500,330);if(id==APP_INSTALLER)discover_targets();start_open=0;redraw_all();}

static void mouse_click(int x,int y){if(start_open){int h=70+9*37;RECT m={18,(int)s.h-h-10,440,h};for(int i=0;i<9;i++){RECT r={m.x+10,m.y+48+i*37,m.w-20,30};if(inside(x,y,r)){start_selected=i;launch_app(i);return;}}return;}
for(int i=MAX_WINDOWS-1;i>=0;i--){WINDOW *w=&windows[i];if(!w->open||w->minimized)continue;RECT whole={w->x,w->y,w->w,w->h};if(!inside(x,y,whole))continue;focus_window(i);if(inside(x,y,(RECT){w->x+w->w-21,w->y+7,14,14})){close_window(i);redraw_all();return;}if(inside(x,y,(RECT){w->x+w->w-41,w->y+7,14,14})){w->maximized=!w->maximized;if(w->maximized){w->x=8;w->y=48;w->w=s.w-16;w->h=s.h-108;}else{w->x=120;w->y=110;w->w=500;w->h=330;}redraw_all();return;}if(inside(x,y,(RECT){w->x+w->w-61,w->y+7,14,14})){w->minimized=1;redraw_all();return;}if(inside(x,y,(RECT){w->x,w->y,w->w,30})){w->drag=1;w->dx=x-w->x;w->dy=y-w->y;return;}RECT c=content_rect(w);
if(w->id==APP_TERMINAL&&inside(x,y,c)){return;}if(w->id==APP_SETTINGS){if(inside(x,y,(RECT){c.x+18,c.y+94,170,38})){light_theme=!light_theme;redraw_all();return;}if(inside(x,y,(RECT){c.x+18,c.y+194,75,38})){mouse_divisor=3;redraw_all();return;}if(inside(x,y,(RECT){c.x+110,c.y+194,95,38})){mouse_divisor=1;redraw_all();return;}}
if(w->id==APP_INSTALLER){if(inside(x,y,(RECT){c.x+15,c.y+c.h-55,210,38})){if(target_selected>=0){EFI_STATUS st=write_install();installer_message=EFI_ERROR(st)?2:1;redraw_all();}return;}for(UINTN n=0;n<target_count;n++){RECT r={c.x+15,c.y+75+(int)n*34,c.w-30,28};if(inside(x,y,r)){target_selected=n;redraw_all();return;}}}
redraw_all();return;}
RECT start={18,(int)s.h-58,155,50};if(inside(x,y,start)){start_open=!start_open;start_selected=0;redraw_all();return;}
}

static void mouse_move(void){if(!mouse)return;EFI_SIMPLE_POINTER_STATE st;EFI_STATUS r=uefi_call_wrapper(mouse->GetState,2,mouse,&st);if(EFI_ERROR(r))return;mouse_x+=(INTN)st.RelativeMovementX/mouse_divisor;mouse_y+=(INTN)st.RelativeMovementY/mouse_divisor;if(mouse_x<0)mouse_x=0;if(mouse_y<0)mouse_y=0;if(mouse_x>=(int)s.w)mouse_x=s.w-1;if(mouse_y>=(int)s.h)mouse_y=s.h-1;int left=st.LeftButton?1:0;if(left&&!mouse_left){mouse_click(mouse_x,mouse_y);}if(left&&active_window>=0&&active_window<MAX_WINDOWS&&windows[active_window].drag&&!windows[active_window].maximized){WINDOW*w=&windows[active_window];w->x=mouse_x-w->dx;w->y=mouse_y-w->dy;if(w->x<0)w->x=0;if(w->y<42)w->y=42;redraw_all();}if(!left&&mouse_left&&active_window>=0)windows[active_window].drag=0;mouse_left=left;}

static void key(EFI_INPUT_KEY k){if(start_open){if(k.ScanCode==SCAN_UP){start_selected--;if(start_selected<0)start_selected=8;redraw_all();return;}if(k.ScanCode==SCAN_DOWN){start_selected++;if(start_selected>8)start_selected=0;redraw_all();return;}if(k.ScanCode==SCAN_ESC){start_open=0;redraw_all();return;}if(k.UnicodeChar==CHAR_CARRIAGE_RETURN||k.UnicodeChar==' '){launch_app(start_selected);return;}}
if(active_window>=0&&windows[active_window].open&&windows[active_window].id==APP_TERMINAL){if(k.UnicodeChar==CHAR_BACKSPACE){if(term_len)term_input[--term_len]=0;redraw_all();return;}if(k.UnicodeChar==CHAR_CARRIAGE_RETURN){terminal_command();redraw_all();return;}if(k.UnicodeChar>=32&&k.UnicodeChar<127&&term_len<95){CHAR8 c=(CHAR8)k.UnicodeChar;if(c>='a'&&c<='z')c=(CHAR8)(c-32);term_input[term_len++]=c;term_input[term_len]=0;redraw_all();return;}}
if(k.UnicodeChar=='s'||k.UnicodeChar=='S'){start_open=!start_open;start_selected=0;redraw_all();return;}if(k.UnicodeChar=='t'||k.UnicodeChar=='T'){open_window(APP_TERMINAL,"TERMINAL",100,100,560,350);redraw_all();return;}if(k.UnicodeChar=='k'||k.UnicodeChar=='K'){open_window(APP_TASKS,"TASK MANAGER",135,115,540,330);redraw_all();return;}if(k.UnicodeChar=='f'||k.UnicodeChar=='F'){open_window(APP_FILES,"FILES",150,100,500,350);redraw_all();return;}if(k.ScanCode==SCAN_ESC){if(active_window>=0)close_window(active_window);start_open=0;redraw_all();}}

EFI_STATUS steveos_shell_start(EFI_HANDLE ih,EFI_GRAPHICS_OUTPUT_PROTOCOL *gp){image_handle=ih;gop=gp;s.w=gop->Mode->Info->HorizontalResolution;s.h=gop->Mode->Info->VerticalResolution;s.stride=gop->Mode->Info->PixelsPerScanLine;s.format=gop->Mode->Info->PixelFormat;s.mask=gop->Mode->Info->PixelInformation;s.fb=(UINT32*)(UINTN)gop->Mode->FrameBufferBase;mouse_x=s.w/2;mouse_y=s.h/2;UINTN ms=0;EFI_LOADED_IMAGE_PROTOCOL *loaded=0;if(!EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol,3,image_handle,&gEfiLoadedImageProtocolGuid,(VOID**)&loaded))&&loaded)source_device=loaded->DeviceHandle;if(!EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol,3,source_device,&gEfiSimpleFileSystemProtocolGuid,(VOID**)&boot_root))==0&&boot_root){}else boot_root=0;
EFI_GUID sp={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};uefi_call_wrapper(BS->LocateProtocol,3,&sp,NULL,(VOID**)&mouse);if(mouse){mouse_event=mouse->WaitForInput;uefi_call_wrapper(mouse->Reset,2,mouse,FALSE);}
uefi_call_wrapper(BS->CreateEvent,5,EVT_TIMER,TPL_CALLBACK,NULL,NULL,&timer_event);uefi_call_wrapper(BS->SetTimer,3,timer_event,TimerPeriodic,250000);refresh_stats();task_add_shell();term_add("STEVEOS TERMINAL READY");term_add("TYPE HELP FOR COMMANDS");
EFI_EVENT events[3];UINTN ec=0;events[ec++]=ST->ConIn->WaitForKey;if(mouse_event)events[ec++]=mouse_event;events[ec++]=timer_event;redraw_all();for(;;){UINTN idx=0;EFI_STATUS st=uefi_call_wrapper(BS->WaitForEvent,3,ec,events,&idx);if(EFI_ERROR(st))continue;if(mouse_event&&idx==1){mouse_move();continue;}if(idx==ec-1){redraw_all();continue;}EFI_INPUT_KEY k;if(!EFI_ERROR(uefi_call_wrapper(ST->ConIn->ReadKeyStroke,2,ST->ConIn,&k)))key(k);}
return EFI_SUCCESS;}
