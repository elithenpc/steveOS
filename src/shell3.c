#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "shell.h"
#include "memory.h"
#include "storage.h"
#include "network.h"
#include "kernel.h"
#include "tasks.h"
#include "fs.h"

extern const unsigned char _binary_build_boot_raw_start[];
extern const unsigned char _binary_build_boot_raw_end[];
#define MAXW 8
#define MAX_TARGETS 8
#define MAX_FILES 12
#define MAX_LINES 14
#define COLS 74
#define WEB_LINES 10

typedef struct { UINT32 w,h,stride; UINT32 *fb; } SCREEN;
typedef struct { int x,y,w,h; } RECT;
typedef enum { W_TERM, W_TASKS, W_FILES, W_SYSTEM, W_NETWORK, W_IMAGE, W_SETTINGS, W_INSTALL } APP;
typedef struct { int open,min,drag; int x,y,w,h,ox,oy; APP id; char title[24]; } WIN;

static SCREEN sc; static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop; static EFI_SIMPLE_POINTER_PROTOCOL *mouse;
static EFI_EVENT mouse_event; static EFI_HANDLE source_device; static EFI_FILE_PROTOCOL *root;
static EFI_HANDLE targets[MAX_TARGETS]; static UINTN target_count; static int target_pick=-1,install_result;
static WIN wins[MAXW]; static int active=-1,start_open,start_pick; static int mx,my,left_old; static int light; static UINT32 mouse_scale=2;
static UINT64 ram_bytes; static UINTN disks; static EFI_STATUS net_status; static CHAR8 lines[MAX_LINES][COLS]; static int line_count;
static CHAR8 input[90]; static UINTN input_len; static CHAR8 file_names[MAX_FILES][64]; static int file_count,files_ready;
static CHAR8 web_input[128]; static UINTN web_input_len; static CHAR8 web_body[4096]; static UINTN web_body_len; static UINT32 web_status; static CHAR8 web_lines[WEB_LINES][COLS]; static int web_ready;

static int w_open(APP id); static void discover_targets(void); static EFI_STATUS install_selected(void); static void redraw(void); static void launch(int id);
static void click(int x,int y); static void keyboard(EFI_INPUT_KEY k); static void termcmd(void); static void web_fetch(void);

static UINT32 bgc(void){return light?0xEEF3F8:0x0B1020;} static UINT32 panelc(void){return light?0xFAFCFF:0x151F34;}
static UINT32 barc(void){return light?0xDDE6F0:0x111A2D;} static UINT32 cardc(void){return light?0xE4EBF3:0x1B2942;}
static UINT32 fgc(void){return light?0x101720:0xF6F9FF;} static UINT32 subc(void){return light?0x53657A:0xAEBBD0;} static UINT32 acc(void){return light?0x547BBD:0x5E8DFF;}
static void px(int x,int y,UINT32 c){if(x>=0&&y>=0&&(UINT32)x<sc.w&&(UINT32)y<sc.h)sc.fb[(UINTN)y*sc.stride+(UINTN)x]=c;}
static void rr(RECT a,UINT32 c){int x0=a.x<0?0:a.x,y0=a.y<0?0:a.y,x1=a.x+a.w,y1=a.y+a.h;if(x1>(int)sc.w)x1=sc.w;if(y1>(int)sc.h)y1=sc.h;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)px(x,y,c);}
static void roundr(RECT a,UINT32 c,int q){if(q<2){rr(a,c);return;}for(int y=0;y<a.h;y++){int d=y<q?q-1-y:y>=a.h-q?y-(a.h-q):0;int in=(d*d)/q;rr((RECT){a.x+in,a.y+y,a.w-2*in,1},c);}}
static int hit(int x,int y,RECT a){return x>=a.x&&y>=a.y&&x<a.x+a.w&&y<a.y+a.h;}

static const UINT8 L[26][5]={{0x7e,0x11,0x11,0x7e,0},{0x7f,0x49,0x49,0x36,0},{0x3e,0x41,0x41,0x22,0},{0x7f,0x41,0x41,0x3e,0},{0x7f,0x49,0x49,0x41,0},{0x7f,9,9,1,0},{0x3e,0x41,0x51,0x72,0},{0x7f,8,8,0x7f,0},{0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0},{0x7f,8,0x14,0x63,0},{0x7f,0x40,0x40,0x40,0},{0x7f,6,0x18,6,0x7f},{0x7f,6,0x18,0x7f,0},{0x3e,0x41,0x41,0x3e,0},{0x7f,9,9,6,0},{0x3e,0x41,0x61,0x7e,0},{0x7f,9,0x19,0x66,0},{0x26,0x49,0x49,0x32,0},{1,0x7f,1,1,0},{0x3f,0x40,0x40,0x3f,0},{0x1f,0x60,0x60,0x1f,0},{0x7f,0x30,0x0c,0x30,0x7f},{0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}};
static const UINT8 N[10][5]={{0x3e,0x45,0x49,0x51,0x3e},{0x21,0x7f,1,0,0},{0x23,0x45,0x49,0x49,0x31},{0x22,0x41,0x49,0x49,0x36},{0x0c,0x14,0x24,0x7f,4},{0x7a,0x49,0x49,0x49,0x46},{0x3e,0x49,0x49,0x49,0x26},{0x40,0x47,0x48,0x50,0x60},{0x36,0x49,0x49,0x49,0x36},{0x32,0x49,0x49,0x49,0x3e}};
static void glyph(int x,int y,char c,UINT32 col,int z){const UINT8*q=0;if(c>='A'&&c<='Z')q=L[c-'A'];else if(c>='a'&&c<='z')q=L[c-'a'];else if(c>='0'&&c<='9')q=N[c-'0'];if(q){for(int a=0;a<5;a++)for(int b=0;b<7;b++)if(q[a]&(1u<<b))rr((RECT){x+a*z,y+b*z,z,z},col);return;}if(c=='.')rr((RECT){x,y+6*z,z,z},col);else if(c=='-')rr((RECT){x,y+3*z,5*z,z},col);else if(c==':'){rr((RECT){x,y+2*z,z,z},col);rr((RECT){x,y+6*z,z,z},col);}else if(c=='_')rr((RECT){x,y+6*z,5*z,z},col);else if(c=='>'){rr((RECT){x,y+2*z,z,z},col);rr((RECT){x+z,y+3*z,z,z},col);rr((RECT){x,y+4*z,z,z},col);}else if(c=='/'){rr((RECT){x+4*z,y, z,z},col);rr((RECT){x+3*z,y+z,z,z},col);rr((RECT){x+2*z,y+2*z,z,z},col);rr((RECT){x+z,y+3*z,z,z},col);rr((RECT){x,y+4*z,z,z},col);}else if(c=='?'){rr((RECT){x+z,y,z,z},col);rr((RECT){x+2*z,y,z,z},col);rr((RECT){x+3*z,y+z,z,z},col);rr((RECT){x+2*z,y+2*z,z,z},col);rr((RECT){x+2*z,y+5*z,z,z},col);}else if(c=='&'){rr((RECT){x+z,y+z,z,z},col);rr((RECT){x,y+2*z,z,z},col);rr((RECT){x+z,y+3*z,z,z},col);rr((RECT){x+3*z,y+4*z,z,z},col);rr((RECT){x+2*z,y+5*z,z,z},col);}else if(c=='=')rr((RECT){x,y+2*z,5*z,z},col),rr((RECT){x,y+5*z,5*z,z},col);else if(c=='#'){rr((RECT){x+z,y, z,z},col);rr((RECT){x+3*z,y,z,z},col);rr((RECT){x,y+3*z,5*z,z},col);rr((RECT){x+z,y+6*z,z,z},col);rr((RECT){x+3*z,y+6*z,z,z},col);}else if(c=='%'){rr((RECT){x+3*z,y,z,z},col);rr((RECT){x+z,y+2*z,z,z},col);rr((RECT){x+4*z,y+5*z,z,z},col);}else if(c=='!')rr((RECT){x+2*z,y, z,5*z},col),rr((RECT){x+2*z,y+6*z,z,z},col);}
static void txt(int x,int y,const char*s,UINT32 col,int z){while(*s){if(*s==' ')x+=6*z;else{glyph(x,y,*s,col,z);x+=6*z;}s++;}}
static void num(int x,int y,UINT64 v,UINT32 col,int z){char b[32];int k=0;if(!v)b[k++]='0';while(v&&k<31){b[k++]=(char)('0'+v%10);v/=10;}while(k--){glyph(x,y,b[k],col,z);x+=6*z;}}
static void cursor(void){for(int i=0;i<15;i++){px(mx,my+i,0xFFFFFF);if(i<9)px(mx+i,my+i,0xFFFFFF);}}

static void stats(void){
    EFI_STATUS ms=steveos_memory_init();
    if(!EFI_ERROR(ms)){ram_bytes=steveos_total_memory_bytes();steveos_memory_shutdown();}
    disks=steveos_count_disks();
    net_status=EFI_UNSUPPORTED;
}
static void addline(const char*s){int i=0;if(line_count<MAX_LINES){while(s[i]&&i<COLS-1){lines[line_count][i]=s[i];i++;}lines[line_count][i]=0;line_count++;}else{for(int a=1;a<MAX_LINES;a++)for(int b=0;b<COLS;b++)lines[a-1][b]=lines[a][b];while(s[i]&&i<COLS-1){lines[MAX_LINES-1][i]=s[i];i++;}lines[MAX_LINES-1][i]=0;}}
static int same(const char*a,const char*b){while(*a&&*b&&*a==*b){a++;b++;}return *a==0&&*b==0;}
static int has_scheme(const char*s){for(int i=0;i<120&&s[i];i++)if(s[i]==':'&&s[i+1]=='/'&&s[i+2]=='/')return 1;return 0;}

static void web_plain(void){
    ZeroMem(web_lines,sizeof(web_lines));
    int line=0,col=0,in_tag=0,entity=0;
    for(UINTN i=0;i<web_body_len&&line<WEB_LINES;i++){
        unsigned char c=(unsigned char)web_body[i];
        if(c=='<'){in_tag=1;continue;}
        if(in_tag){if(c=='>')in_tag=0;continue;}
        if(c=='&'){entity=1;continue;}
        if(entity){if(c==';')entity=0;continue;}
        if(c=='\r'||c=='\n'){if(col){line++;col=0;}continue;}
        if(c=='\t'||c==' '){if(col&&web_lines[line][col-1]!=' '&&col<COLS-1)web_lines[line][col++]=' ';continue;}
        if(c<32||c>126)continue;
        if(col>=COLS-1){line++;col=0;if(line>=WEB_LINES)break;}
        web_lines[line][col++]=(CHAR8)c;
    }
    for(int i=0;i<WEB_LINES;i++)web_lines[i][COLS-1]=0;
    web_ready=1;
}
static void web_fetch(void){
    CHAR16 url[160];
    UINTN p=0;
    if(!web_input_len){web_input_len=16;while("http://example.com"[p]&&p<127){web_input[p]=(CHAR8)"http://example.com"[p];p++;}web_input_len=p;}
    p=0;
    if(!has_scheme((char*)web_input)){const char*prefix="http://";for(UINTN i=0;prefix[i]&&p<159;i++)url[p++]=(CHAR16)prefix[i];}
    for(UINTN i=0;i<web_input_len&&p<159;i++)url[p++]=(CHAR16)web_input[i];
    url[p]=0;
    web_body_len=0;web_status=0;ZeroMem(web_body,sizeof(web_body));
    EFI_STATUS st=steveos_http_get(url,web_body,sizeof(web_body)-1,&web_body_len,&web_status);
    if(EFI_ERROR(st)){
        ZeroMem(web_lines,sizeof(web_lines));
        const char*msg="HTTP REQUEST FAILED";UINTN i=0;while(msg[i]&&i<COLS-1){web_lines[0][i]=msg[i];i++;}web_lines[0][i]=0;
        const char*msg2="FIRMWARE HTTP UNAVAILABLE";i=0;while(msg2[i]&&i<COLS-1){web_lines[1][i]=msg2[i];i++;}web_lines[1][i]=0;
        web_ready=1;return;
    }
    web_plain();
}

static void files_scan(void){if(!root)return;file_count=0;uefi_call_wrapper(root->SetPosition,2,root,0);UINT8*b=AllocatePool(4096);if(!b)return;while(file_count<MAX_FILES){UINTN z=4096;if(EFI_ERROR(uefi_call_wrapper(root->Read,3,root,&z,b))||!z)break;EFI_FILE_INFO*f=(EFI_FILE_INFO*)b;int j=0;while(j<63&&f->FileName[j]){file_names[file_count][j]=f->FileName[j]<128?(char)f->FileName[j]:'?';j++;}file_names[file_count][j]=0;file_count++;}FreePool(b);files_ready=1;}
static void discover_targets(void){target_count=0;target_pick=-1;UINTN sz=0;if(uefi_call_wrapper(BS->LocateHandle,5,ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&sz,NULL)!=EFI_BUFFER_TOO_SMALL)return;EFI_HANDLE*h=AllocatePool(sz);if(!h)return;if(!EFI_ERROR(uefi_call_wrapper(BS->LocateHandle,5,ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&sz,h)))for(UINTN i=0;i<sz/sizeof(EFI_HANDLE)&&target_count<MAX_TARGETS;i++)if(h[i]!=source_device)targets[target_count++]=h[i];FreePool(h);}
static EFI_STATUS install_selected(void){if(target_pick<0)return EFI_INVALID_PARAMETER;EFI_FILE_PROTOCOL*src=0,*tr=0,*ed=0,*bd=0,*f=0;VOID*data=0;UINTN size=0;EFI_STATUS st=steveos_fs_open_volume(source_device,&src);if(EFI_ERROR(st))return st;st=steveos_fs_read_file(src,L"\\EFI\\BOOT\\BOOTX64.EFI",&data,&size);uefi_call_wrapper(src->Close,1,src);if(EFI_ERROR(st))return st;st=steveos_fs_open_volume(targets[target_pick],&tr);if(!EFI_ERROR(st))st=uefi_call_wrapper(tr->Open,5,tr,&ed,L"EFI",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);if(!EFI_ERROR(st))st=uefi_call_wrapper(ed->Open,5,ed,&bd,L"BOOT",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);if(!EFI_ERROR(st))st=uefi_call_wrapper(bd->Open,5,bd,&f,L"BOOTX64.EFI",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);if(!EFI_ERROR(st)){UINTN wr=size;st=uefi_call_wrapper(f->SetPosition,2,f,0);if(!EFI_ERROR(st))st=uefi_call_wrapper(f->Write,3,f,&wr,data);}if(f)uefi_call_wrapper(f->Close,1,f);if(bd)uefi_call_wrapper(bd->Close,1,bd);if(ed)uefi_call_wrapper(ed->Close,1,ed);if(tr)uefi_call_wrapper(tr->Close,1,tr);FreePool(data);return st;}

static void termcmd(void){input[input_len]=0;addline((char*)input);if(same((char*)input,"HELP")){addline("HELP SYSINFO TASKS LS PWD DATE NET");addline("BROWSER CLEAR WINDOWS REBOOT SHUTDOWN INSTALL");}else if(same((char*)input,"SYSINFO")){stats();addline("STEVEOS SYSTEM INFORMATION");addline("MEMORY DISKS TASKS DISPLAY");}else if(same((char*)input,"TASKS")){w_open(W_TASKS);addline("TASK MANAGER OPENED");}else if(same((char*)input,"LS")){files_ready=0;w_open(W_FILES);addline("FILE MANAGER OPENED");}else if(same((char*)input,"PWD"))addline("\\");else if(same((char*)input,"NET"))addline("NETWORK SERVICES ON DEMAND");else if(same((char*)input,"BROWSER")||same((char*)input,"WEB")){w_open(W_NETWORK);addline("WEB BROWSER OPENED");}else if(same((char*)input,"CLEAR"))line_count=0;else if(same((char*)input,"WINDOWS"))addline("WINDOW MANAGER ACTIVE");else if(same((char*)input,"INSTALL")){w_open(W_INSTALL);discover_targets();addline("INSTALLER OPENED");}else if(same((char*)input,"REBOOT"))uefi_call_wrapper(RT->ResetSystem,4,EfiResetCold,EFI_SUCCESS,0,NULL);else if(same((char*)input,"SHUTDOWN"))uefi_call_wrapper(RT->ResetSystem,4,EfiResetShutdown,EFI_SUCCESS,0,NULL);else if(input_len)addline("UNKNOWN COMMAND - TYPE HELP");input_len=0;input[0]=0;}

static int w_open(APP id){for(int i=0;i<MAXW;i++)if(wins[i].open&&wins[i].id==id){wins[i].min=0;active=i;return i;}for(int i=0;i<MAXW;i++)if(!wins[i].open){wins[i].open=1;wins[i].min=0;wins[i].drag=0;wins[i].id=id;wins[i].x=100+(i%3)*35;wins[i].y=80+(i%3)*35;wins[i].w=520;wins[i].h=340;const char*z[]={"TERMINAL","TASK MANAGER","FILES","SYSTEM INFO","WEB BROWSER","BLEHHH VIEWER","SETTINGS","INSTALLER"};int j=0;while(z[id][j]&&j<23){wins[i].title[j]=z[id][j];j++;}wins[i].title[j]=0;active=i;if(id==W_NETWORK){web_input_len=0;web_body_len=0;web_status=0;web_ready=0;const char*d="http://example.com";while(d[web_input_len]&&web_input_len<127){web_input[web_input_len]=(CHAR8)d[web_input_len];web_input_len++;}web_input[web_input_len]=0;}return i;}return -1;}
static void draw_term(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};rr(c,0x070B10);for(int i=0;i<line_count;i++)txt(c.x+12,c.y+10+i*18,(char*)lines[i],0xD5DFEF,1);txt(c.x+12,c.y+c.h-27,"STEVE>",0x6F9BFF,1);txt(c.x+55,c.y+c.h-27,(char*)input,0xFFFFFF,1);}
static void draw_tasks(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};rr(c,panelc());txt(c.x+15,c.y+15,"TASK MANAGER",fgc(),2);txt(c.x+15,c.y+55,"ID",subc(),1);txt(c.x+60,c.y+55,"STATE",subc(),1);txt(c.x+170,c.y+55,"IP",subc(),1);for(UINT32 i=0;i<steveos_task_count()&&i<8;i++){STEVEOS_TASK q;if(!EFI_ERROR(steveos_task_get(i,&q))){int y=c.y+80+i*28;num(c.x+15,y,q.id,fgc(),1);txt(c.x+60,y,q.state==STEVEOS_TASK_READY?"READY":q.state==STEVEOS_TASK_SLEEPING?"SLEEP":"RUNNING",fgc(),1);num(c.x+170,y,q.instruction_pointer,fgc(),1);}}}
static void draw_files(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};rr(c,panelc());txt(c.x+15,c.y+15,"BOOT VOLUME",fgc(),2);if(!root){txt(c.x+15,c.y+55,"NO FILESYSTEM",fgc(),1);return;}if(!files_ready)files_scan();for(int i=0;i<file_count;i++)txt(c.x+18,c.y+55+i*22,file_names[i],fgc(),1);}
static void draw_system(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};rr(c,panelc());txt(c.x+18,c.y+18,"SYSTEM INFORMATION",fgc(),2);txt(c.x+18,c.y+60,"RAM MB",subc(),1);num(c.x+170,c.y+60,ram_bytes/1024/1024,fgc(),1);txt(c.x+18,c.y+92,"DISKS",subc(),1);num(c.x+170,c.y+92,disks,fgc(),1);txt(c.x+18,c.y+124,"TASKS",subc(),1);num(c.x+170,c.y+124,steveos_task_count(),fgc(),1);txt(c.x+18,c.y+156,"DISPLAY",subc(),1);num(c.x+170,c.y+156,sc.w,fgc(),1);txt(c.x+212,c.y+156,"X",subc(),1);num(c.x+230,c.y+156,sc.h,fgc(),1);txt(c.x+18,c.y+188,"KERNEL",subc(),1);txt(c.x+170,c.y+188,"UEFI HANDOFF",fgc(),1);}
static void draw_net(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};rr(c,panelc());txt(c.x+18,c.y+18,"WEB BROWSER",fgc(),2);txt(c.x+18,c.y+56,"URL",subc(),1);roundr((RECT){c.x+60,c.y+47,c.w-78,28},cardc(),7);txt(c.x+70,c.y+57,(char*)web_input,fgc(),1);txt(c.x+18,c.y+91,"HTTP STATUS",subc(),1);if(web_status)num(c.x+170,c.y+91,web_status,fgc(),1);else txt(c.x+170,c.y+91,"READY",fgc(),1);if(!web_ready){txt(c.x+18,c.y+126,"TYPE URL THEN PRESS ENTER",fgc(),1);txt(c.x+18,c.y+151,"DEFAULT: HTTP EXAMPLE COM",subc(),1);return;}for(int i=0;i<WEB_LINES;i++)if(web_lines[i][0])txt(c.x+18,c.y+126+i*19,(char*)web_lines[i],fgc(),1);txt(c.x+18,c.y+c.h-22,"ENTER LOAD   BACKSPACE EDIT URL",subc(),1);}
static void draw_settings(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};rr(c,panelc());txt(c.x+18,c.y+18,"SETTINGS",fgc(),2);txt(c.x+18,c.y+60,"THEME",subc(),1);txt(c.x+170,c.y+60,light?"LIGHT":"DARK",fgc(),1);roundr((RECT){c.x+18,c.y+92,180,36},acc(),9);txt(c.x+48,c.y+103,"TOGGLE THEME",fgc(),1);roundr((RECT){c.x+18,c.y+188,78,36},cardc(),9);roundr((RECT){c.x+110,c.y+188,78,36},cardc(),9);txt(c.x+40,c.y+199,"SLOW",fgc(),1);txt(c.x+128,c.y+199,"FAST",fgc(),1);}
static void draw_image(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};rr(c,0x05070D);const UINT8*q=_binary_build_boot_raw_start;UINT32 iw=*(UINT32*)q,ih=*(UINT32*)(q+4);const UINT8*pix=q+8;if(iw&&ih){UINT32 dw=c.w,dh=(UINT64)ih*dw/iw;if(dh>(UINT32)c.h){dh=c.h;dw=(UINT64)iw*dh/ih;}int ox=c.x+(c.w-(int)dw)/2,oy=c.y+(c.h-(int)dh)/2;for(UINT32 y=0;y<dh;y++)for(UINT32 x=0;x<dw;x++){UINT32 sx=(UINT64)x*iw/dw,sy=(UINT64)y*ih/dh;const UINT8*v=pix+((UINTN)sy*iw+sx)*4;px(ox+x,oy+y,((UINT32)v[2]<<16)|((UINT32)v[1]<<8)|v[0]);}}}
static void draw_install(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};rr(c,panelc());txt(c.x+15,c.y+15,"OVERWRITE STEVEOS",fgc(),2);txt(c.x+15,c.y+50,"SELECT TARGET VOLUME",subc(),1);for(UINTN i=0;i<target_count;i++){RECT q={c.x+15,c.y+76+i*30,c.w-30,25};if((int)i==target_pick)rr(q,acc());txt(q.x+10,q.y+7,"EFI VOLUME",fgc(),1);num(q.x+106,q.y+7,i+1,fgc(),1);}roundr((RECT){c.x+15,c.y+c.h-52,190,38},acc(),9);txt(c.x+34,c.y+c.h-41,"OVERWRITE INSTALL",fgc(),1);if(install_result)txt(c.x+225,c.y+c.h-41,install_result==1?"COMPLETE":"FAILED",fgc(),1);}
static void draw_win(WIN*w){if(!w->open||w->min)return;roundr((RECT){w->x+7,w->y+7,w->w,w->h},0x050811,11);roundr((RECT){w->x,w->y,w->w,w->h},panelc(),11);rr((RECT){w->x,w->y,w->w,30},barc());rr((RECT){w->x,w->y+29,w->w,1},acc());txt(w->x+12,w->y+9,w->title,fgc(),1);rr((RECT){w->x+w->w-60,w->y+7,14,14},cardc());rr((RECT){w->x+w->w-40,w->y+7,14,14},cardc());rr((RECT){w->x+w->w-20,w->y+7,14,14},acc());glyph(w->x+w->w-57,w->y+9,'_',fgc(),1);glyph(w->x+w->w-37,w->y+9,'O',fgc(),1);glyph(w->x+w->w-17,w->y+9,'X',fgc(),1);if(w->id==W_TERM)draw_term(w);else if(w->id==W_TASKS)draw_tasks(w);else if(w->id==W_FILES)draw_files(w);else if(w->id==W_SYSTEM)draw_system(w);else if(w->id==W_NETWORK)draw_net(w);else if(w->id==W_IMAGE)draw_image(w);else if(w->id==W_SETTINGS)draw_settings(w);else if(w->id==W_INSTALL)draw_install(w);}

static void desktop(void){
    rr((RECT){0,0,sc.w,sc.h},bgc());
    rr((RECT){0,0,sc.w,48},barc());
    rr((RECT){0,46,sc.w,2},acc());
    roundr((RECT){16,10,28,28},acc(),8);
    txt(23,18,"S",0xFFFFFF,1);
    txt(56,12,"STEVEOS",fgc(),2);
    txt(122,15,"DESKTOP",subc(),1);
    roundr((RECT){sc.w-188,10,166,28},cardc(),9);
    txt(sc.w-174,18,"SYSTEM ONLINE",fgc(),1);
    txt(sc.w-78,18,"UEFI",subc(),1);
    txt(34,78,"WELCOME BACK",fgc(),3);
    txt(36,116,"YOUR CUSTOM OPERATING SYSTEM",subc(),1);
    rr((RECT){36,137,82,4},acc());
    roundr((RECT){30,162,242,112},panelc(),12);
    txt(48,180,"MEMORY",subc(),1);
    num(48,209,ram_bytes/1024/1024,fgc(),2);
    txt(128,211,"MB",subc(),1);
    txt(48,246,"TASKS",subc(),1);
    num(128,244,steveos_task_count(),fgc(),2);
    roundr((RECT){288,162,242,112},panelc(),12);
    txt(306,180,"NETWORK",subc(),1);
    txt(306,210,"HTTP ON DEMAND",fgc(),2);
    txt(306,246,"UEFI NETWORK STACK",subc(),1);
    for(int i=0;i<9;i++)rr((RECT){30+i*((int)sc.w/9),294,(int)sc.w/18,1},0x202D42);
    roundr((RECT){30,316,500,56},cardc(),12);
    txt(48,337,"T  TERMINAL",fgc(),1);
    txt(190,337,"K  TASKS",fgc(),1);
    txt(312,337,"F  FILES",fgc(),1);
    txt(417,337,"S  START",fgc(),1);
    roundr((RECT){18,sc.h-58,172,42},acc(),10);
    txt(58,sc.h-46,"START MENU",fgc(),1);
    txt(sc.w-142,sc.h-46,"STEVEOS",subc(),1);
}
static void menu(void){int h=55+8*36+10;RECT m={18,(int)sc.h-h-10,440,h};roundr((RECT){m.x+7,m.y+7,m.w,m.h},0x050811,12);roundr(m,panelc(),12);rr((RECT){m.x,m.y,m.w,44},barc());txt(m.x+16,m.y+13,"STEVEOS",fgc(),2);const char*a[]={"TERMINAL","TASK MANAGER","FILES","SYSTEM INFO","WEB BROWSER","BLEHHH VIEWER","SETTINGS","INSTALLER"};for(int i=0;i<8;i++){RECT q={m.x+10,m.y+50+i*36,m.w-20,29};if(i==start_pick)roundr(q,acc(),8);txt(q.x+12,q.y+8,a[i],fgc(),1);}}
static void redraw(void){desktop();for(int i=0;i<MAXW;i++)draw_win(&wins[i]);if(start_open)menu();cursor();}
static void launch(int id){if(id<0||id>7)return;w_open((APP)id);if(id==W_FILES)files_ready=0;if(id==W_INSTALL){discover_targets();install_result=0;}start_open=0;redraw();}

static void click(int x,int y){if(start_open){int h=55+8*36+10;RECT m={18,(int)sc.h-h-10,440,h};for(int i=0;i<8;i++)if(hit(x,y,(RECT){m.x+10,m.y+50+i*36,m.w-20,29})){start_pick=i;launch(i);return;}return;}for(int i=MAXW-1;i>=0;i--){WIN*w=&wins[i];if(!w->open||w->min)continue;if(!hit(x,y,(RECT){w->x,w->y,w->w,w->h}))continue;active=i;if(hit(x,y,(RECT){w->x+w->w-20,w->y+7,14,14})){w->open=0;active=-1;redraw();return;}if(hit(x,y,(RECT){w->x+w->w-60,w->y+7,14,14})){w->min=1;redraw();return;}if(hit(x,y,(RECT){w->x+w->w-40,w->y+7,14,14})){w->x=8;w->y=58;w->w=sc.w-16;w->h=sc.h-115;redraw();return;}if(hit(x,y,(RECT){w->x,w->y,w->w,30})){w->drag=1;w->ox=x-w->x;w->oy=y-w->y;return;}RECT c={w->x,w->y+30,w->w,w->h-30};if(w->id==W_SETTINGS){if(hit(x,y,(RECT){c.x+18,c.y+92,180,36})){light=!light;redraw();return;}if(hit(x,y,(RECT){c.x+18,c.y+188,78,36})){mouse_scale=3;redraw();return;}if(hit(x,y,(RECT){c.x+110,c.y+188,78,36})){mouse_scale=1;redraw();return;}}if(w->id==W_NETWORK){if(hit(x,y,(RECT){c.x+60,c.y+47,c.w-78,28})){return;}}if(w->id==W_INSTALL){for(UINTN q=0;q<target_count;q++)if(hit(x,y,(RECT){c.x+15,c.y+76+q*30,c.w-30,25})){target_pick=q;redraw();return;}if(hit(x,y,(RECT){c.x+15,c.y+c.h-52,190,38})&&target_pick>=0){install_result=EFI_ERROR(install_selected())?2:1;redraw();return;}}return;}if(hit(x,y,(RECT){18,(int)sc.h-64,172,54})){start_open=!start_open;start_pick=0;redraw();}}
static void motion(void){if(!mouse)return;EFI_SIMPLE_POINTER_STATE q;if(EFI_ERROR(uefi_call_wrapper(mouse->GetState,2,mouse,&q)))return;mx+=(INTN)q.RelativeMovementX/mouse_scale;my+=(INTN)q.RelativeMovementY/mouse_scale;if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)sc.w)mx=sc.w-1;if(my>=(int)sc.h)my=sc.h-1;int l=q.LeftButton?1:0;if(l&&!left_old)click(mx,my);if(l&&active>=0&&wins[active].drag){wins[active].x=mx-wins[active].ox;wins[active].y=my-wins[active].oy;if(wins[active].x<0)wins[active].x=0;if(wins[active].y<48)wins[active].y=48;redraw();}if(!l&&left_old&&active>=0)wins[active].drag=0;left_old=l;}
static void keyboard(EFI_INPUT_KEY k){if(start_open){if(k.ScanCode==SCAN_UP){start_pick--;if(start_pick<0)start_pick=7;redraw();return;}if(k.ScanCode==SCAN_DOWN){start_pick++;if(start_pick>7)start_pick=0;redraw();return;}if(k.UnicodeChar==CHAR_CARRIAGE_RETURN){launch(start_pick);return;}if(k.ScanCode==SCAN_ESC){start_open=0;redraw();return;}}if(active>=0&&wins[active].open&&wins[active].id==W_TERM){if(k.UnicodeChar==CHAR_BACKSPACE){if(input_len)input[--input_len]=0;redraw();return;}if(k.UnicodeChar==CHAR_CARRIAGE_RETURN){termcmd();redraw();return;}if(k.UnicodeChar>=32&&k.UnicodeChar<127&&input_len<89){CHAR8 c=(CHAR8)k.UnicodeChar;if(c>='a'&&c<='z')c-=32;input[input_len++]=c;input[input_len]=0;redraw();return;}}if(active>=0&&wins[active].open&&wins[active].id==W_NETWORK){if(k.UnicodeChar==CHAR_BACKSPACE){if(web_input_len)web_input[--web_input_len]=0;web_ready=0;redraw();return;}if(k.UnicodeChar==CHAR_CARRIAGE_RETURN){web_fetch();redraw();return;}if(k.UnicodeChar>=32&&k.UnicodeChar<127&&web_input_len<127){web_input[web_input_len++]=(CHAR8)k.UnicodeChar;web_input[web_input_len]=0;web_ready=0;redraw();return;}}if(k.UnicodeChar=='s'||k.UnicodeChar=='S'){start_open=!start_open;start_pick=0;redraw();return;}if(k.UnicodeChar=='t'||k.UnicodeChar=='T'){launch(W_TERM);return;}if(k.UnicodeChar=='k'||k.UnicodeChar=='K'){launch(W_TASKS);return;}if(k.UnicodeChar=='f'||k.UnicodeChar=='F'){launch(W_FILES);return;}if(k.UnicodeChar=='w'||k.UnicodeChar=='W'){launch(W_NETWORK);return;}if(k.ScanCode==SCAN_ESC&&active>=0){wins[active].open=0;active=-1;redraw();}}

EFI_STATUS steveos_shell_start(EFI_HANDLE ih,EFI_GRAPHICS_OUTPUT_PROTOCOL *gp){
    gop=gp;
    sc.w=gp->Mode->Info->HorizontalResolution;
    sc.h=gp->Mode->Info->VerticalResolution;
    sc.stride=gp->Mode->Info->PixelsPerScanLine;
    sc.fb=(UINT32*)(UINTN)gp->Mode->FrameBufferBase;
    mx=sc.w/2; my=sc.h/2;

    EFI_LOADED_IMAGE_PROTOCOL*li=0;
    if(!EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol,3,ih,&gEfiLoadedImageProtocolGuid,(VOID**)&li))&&li)
        source_device=li->DeviceHandle;
    if(source_device)
        uefi_call_wrapper(BS->HandleProtocol,3,source_device,&gEfiSimpleFileSystemProtocolGuid,(VOID**)&root);

    EFI_GUID sp={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0,0x90,0x27,0x3f,0xc1,0x4d}};
    uefi_call_wrapper(BS->LocateProtocol,3,&sp,NULL,(VOID**)&mouse);
    if(mouse) mouse_event=mouse->WaitForInput;

    steveos_tasks_init();
    if(steveos_task_count()==0) steveos_task_create((UINT64)(UINTN)&steveos_shell_start);
    net_status=EFI_UNSUPPORTED;
    ram_bytes=0;
    disks=0;
    addline("STEVEOS TERMINAL READY");
    addline("TYPE HELP FOR COMMANDS");

    redraw();

    EFI_EVENT ev[2];
    UINTN ec=0;
    ev[ec++]=ST->ConIn->WaitForKey;
    if(mouse_event) ev[ec++]=mouse_event;
    for(;;){
        UINTN ix=0;
        if(EFI_ERROR(uefi_call_wrapper(BS->WaitForEvent,3,ec,ev,&ix)))continue;
        if(mouse_event&&ix==1){motion();continue;}
        EFI_INPUT_KEY k;
        if(!EFI_ERROR(uefi_call_wrapper(ST->ConIn->ReadKeyStroke,2,ST->ConIn,&k)))keyboard(k);
    }
    return EFI_SUCCESS;
}
