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

#define NW 9
#define MAXW 8
#define MAX_TARGETS 8
#define MAX_FILES 12

typedef struct { UINT32 w,h,stride; UINT32 *fb; } SCREEN;
typedef struct { int x,y,w,h; } RECT;
typedef enum { W_TERM, W_TASKS, W_FILES, W_SYSTEM, W_NETWORK, W_IMAGE, W_SETTINGS, W_INSTALL } APP;
typedef struct { int open,min,drag; int x,y,w,h; int ox,oy; APP id; char title[24]; } WIN;

static SCREEN sc;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
static EFI_SIMPLE_POINTER_PROTOCOL *mouse;
static EFI_EVENT mouse_event,timer_event;
static EFI_HANDLE source_device;
static EFI_FILE_PROTOCOL *root;
static EFI_HANDLE targets[MAX_TARGETS];
static UINTN target_count;
static int target_pick=-1,install_result;
static WIN win[MAXW];
static int active=-1,start_open,start_pick;
static int mx,my,left_old;
static int light;
static UINT32 mouse_scale=2;
static UINT64 ram_bytes;
static UINTN disks;
static EFI_STATUS net_status;
static CHAR8 out[14][74];
static int out_count;
static CHAR8 input[90];
static UINTN input_len;
static CHAR8 file_names[MAX_FILES][64];
static int file_count;
static int files_ready;

static UINT32 C_BG(){return light?0xE8EDF3:0x0E1420;}
static UINT32 C_PANEL(){return light?0xF8FAFC:0x202938;}
static UINT32 C_BAR(){return light?0xD5DCE5:0x171F2C;}
static UINT32 C_CARD(){return light?0xDEE5EC:0x293547;}
static UINT32 C_FG(){return light?0x111820:0xF8FAFC;}
static UINT32 C_SUB(){return light?0x4C5969:0xAEB9C9;}
static UINT32 C_ACC(){return light?0x75879B:0x455872;}

static void p(int x,int y,UINT32 c){if(x>=0&&y>=0&&(UINT32)x<sc.w&&(UINT32)y<sc.h)sc.fb[(UINTN)y*sc.stride+(UINTN)x]=c;}
static void r(RECT a,UINT32 c){int x0=a.x<0?0:a.x,y0=a.y<0?0:a.y,x1=a.x+a.w,y1=a.y+a.h;if(x1>(int)sc.w)x1=sc.w;if(y1>(int)sc.h)y1=sc.h;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)p(x,y,c);}
static void rr(RECT a,UINT32 c,int q){if(q<2){r(a,c);return;}for(int y=0;y<a.h;y++){int d=y<q?q-1-y:y>=a.h-q?y-(a.h-q):0;int in=(d*d)/q;r((RECT){a.x+in,a.y+y,a.w-2*in,1},c);}}
static int hit(int x,int y,RECT a){return x>=a.x&&y>=a.y&&x<a.x+a.w&&y<a.y+a.h;}

static const UINT8 A[26][5]={{0x7e,0x11,0x11,0x7e,0},{0x7f,0x49,0x49,0x36,0},{0x3e,0x41,0x41,0x22,0},{0x7f,0x41,0x41,0x3e,0},{0x7f,0x49,0x49,0x41,0},{0x7f,9,9,1,0},{0x3e,0x41,0x51,0x72,0},{0x7f,8,8,0x7f,0},{0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0},{0x7f,8,0x14,0x63,0},{0x7f,0x40,0x40,0x40,0},{0x7f,6,0x18,6,0x7f},{0x7f,6,0x18,0x7f,0},{0x3e,0x41,0x41,0x3e,0},{0x7f,9,9,6,0},{0x3e,0x41,0x61,0x7e,0},{0x7f,9,0x19,0x66,0},{0x26,0x49,0x49,0x32,0},{1,0x7f,1,1,0},{0x3f,0x40,0x40,0x3f,0},{0x1f,0x60,0x60,0x1f,0},{0x7f,0x30,0x0c,0x30,0x7f},{0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}};
static const UINT8 D[10][5]={{0x3e,0x45,0x49,0x51,0x3e},{0x21,0x7f,1,0,0},{0x23,0x45,0x49,0x49,0x31},{0x22,0x41,0x49,0x49,0x36},{0x0c,0x14,0x24,0x7f,4},{0x7a,0x49,0x49,0x49,0x46},{0x3e,0x49,0x49,0x49,0x26},{0x40,0x47,0x48,0x50,0x60},{0x36,0x49,0x49,0x49,0x36},{0x32,0x49,0x49,0x49,0x3e}};
static void g(int x,int y,char c,UINT32 col,int z){const UINT8*q=0;if(c>='A'&&c<='Z')q=A[c-'A'];else if(c>='a'&&c<='z')q=A[c-'a'];else if(c>='0'&&c<='9')q=D[c-'0'];if(q){for(int i=0;i<5;i++)for(int j=0;j<7;j++)if(q[i]&(1u<<j))r((RECT){x+i*z,y+j*z,z,z},col);return;}if(c=='.')r((RECT){x,y+6*z,z,z},col);else if(c=='-')r((RECT){x,y+3*z,5*z,z},col);else if(c==':'){r((RECT){x,y+2*z,z,z},col);r((RECT){x,y+6*z,z,z},col); }else if(c=='>'){r((RECT){x,y+2*z,z,z},col);r((RECT){x+z,y+3*z,z,z},col);r((RECT){x,y+4*z,z,z},col);}else if(c=='_')r((RECT){x,y+6*z,5*z,z},col);}
static void t(int x,int y,const char*s,UINT32 col,int z){while(*s){if(*s==' ')x+=6*z;else{g(x,y,*s,col,z);x+=6*z;}s++;}}
static void n(int x,int y,UINT64 v,UINT32 col,int z){char b[32];int k=0;if(!v)b[k++]='0';while(v&&k<31){b[k++]=(char)('0'+v%10);v/=10;}while(k--){g(x,y,b[k],col,z);x+=6*z;}}
static void cur(){for(int i=0;i<15;i++){p(mx,my+i,0xFFFFFF);if(i<9)p(mx+i,my+i,0xFFFFFF);}}

static void stats(){disks=steveos_count_disks();steveos_memory_init();ram_bytes=steveos_total_memory_bytes();net_status=steveos_http_test();}
static void addline(const char*s){int i=0;if(out_count<14){while(s[i]&&i<73){out[out_count][i]=s[i];i++;}out[out_count][i]=0;out_count++;}else{for(int q=1;q<14;q++)for(int z=0;z<74;z++)out[q-1][z]=out[q][z];while(s[i]&&i<73){out[13][i]=s[i];i++;}out[13][i]=0;}}
static int same(const char*a,const char*b){while(*a&&*b&&*a==*b){a++;b++;}return *a==0&&*b==0;}
static void termcmd(){input[input_len]=0;addline((char*)input);if(same((char*)input,"HELP")){addline("HELP SYSINFO TASKS LS PWD DATE NET");addline("CLEAR WINDOWS REBOOT SHUTDOWN INSTALL");}
else if(same((char*)input,"SYSINFO")){addline("STEVEOS SYSTEM");addline("MEMORY MB IS SHOWN ON DESKTOP");}
else if(same((char*)input,"TASKS")){addline("OPEN TASK MANAGER OR PRESS K");}
else if(same((char*)input,"LS")){files_ready=0;addline("OPEN FILES TO LIST THE BOOT VOLUME");}
else if(same((char*)input,"PWD")){addline("\\");}
else if(same((char*)input,"DATE")){EFI_TIME q;if(!EFI_ERROR(uefi_call_wrapper(RT->GetTime,2,&q,NULL))){char z[11];z[0]='0'+q.Day/10;z[1]='0'+q.Day%10;z[2]='-';z[3]='0'+q.Month/10;z[4]='0'+q.Month%10;z[5]='-';z[6]='0'+(q.Year/1000)%10;z[7]='0'+(q.Year/100)%10;z[8]='0'+(q.Year/10)%10;z[9]='0'+q.Year%10;z[10]=0;addline(z);}}
else if(same((char*)input,"NET"))addline(EFI_ERROR(net_status)?"NIC NOT AVAILABLE":"NIC AVAILABLE");
else if(same((char*)input,"CLEAR"))out_count=0;
else if(same((char*)input,"WINDOWS"))addline("WINDOW MANAGER ACTIVE");
else if(same((char*)input,"REBOOT"))uefi_call_wrapper(RT->ResetSystem,4,EfiResetCold,EFI_SUCCESS,0,NULL);
else if(same((char*)input,"SHUTDOWN"))uefi_call_wrapper(RT->ResetSystem,4,EfiResetShutdown,EFI_SUCCESS,0,NULL);
else if(same((char*)input,"INSTALL")){start_open=0;open_window(W_INSTALL,"INSTALLER");discover_targets();}
else addline("UNKNOWN COMMAND - TYPE HELP");input_len=0;input[0]=0;}

static void scan_files(){if(!root)return;file_count=0;uefi_call_wrapper(root->SetPosition,2,root,0);UINT8*b=AllocatePool(4096);if(!b)return;while(file_count<MAX_FILES){UINTN sz=4096;if(EFI_ERROR(uefi_call_wrapper(root->Read,3,root,&sz,b))||!sz)break;EFI_FILE_INFO*f=(EFI_FILE_INFO*)b;int j=0;while(j<63&&f->FileName[j]){file_names[file_count][j]=(f->FileName[j]<128)?(char)f->FileName[j]:'?';j++;}file_names[file_count][j]=0;file_count++;}FreePool(b);files_ready=1;}
static void discover_targets(){target_count=0;target_pick=-1;UINTN sz=0;if(uefi_call_wrapper(BS->LocateHandle,5,ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&sz,NULL)!=EFI_BUFFER_TOO_SMALL)return;EFI_HANDLE*h=AllocatePool(sz);if(!h)return;if(!EFI_ERROR(uefi_call_wrapper(BS->LocateHandle,5,ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&sz,h)))for(UINTN i=0;i<sz/sizeof(EFI_HANDLE)&&target_count<MAX_TARGETS;i++)if(h[i]!=source_device)targets[target_count++]=h[i];FreePool(h);}
static EFI_STATUS install(){if(target_pick<0)return EFI_INVALID_PARAMETER;EFI_FILE_PROTOCOL*src=0,*tr=0,*ed=0,*bd=0,*f=0;VOID*data=0;UINTN sz=0;EFI_STATUS st=steveos_fs_open_volume(source_device,&src);if(EFI_ERROR(st))return st;st=steveos_fs_read_file(src,L"\\EFI\\BOOT\\BOOTX64.EFI",&data,&sz);uefi_call_wrapper(src->Close,1,src);if(EFI_ERROR(st))return st;st=steveos_fs_open_volume(targets[target_pick],&tr);if(EFI_ERROR(st)){FreePool(data);return st;}st=uefi_call_wrapper(tr->Open,5,tr,&ed,L"EFI",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);if(!EFI_ERROR(st))st=uefi_call_wrapper(ed->Open,5,ed,&bd,L"BOOT",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);if(!EFI_ERROR(st))st=uefi_call_wrapper(bd->Open,5,bd,&f,L"BOOTX64.EFI",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);if(!EFI_ERROR(st)){UINTN ws=sz;st=uefi_call_wrapper(f->SetPosition,2,f,0);if(!EFI_ERROR(st))st=uefi_call_wrapper(f->Write,3,f,&ws,data);}if(f)uefi_call_wrapper(f->Close,1,f);if(bd)uefi_call_wrapper(bd->Close,1,bd);if(ed)uefi_call_wrapper(ed->Close,1,ed);if(tr)uefi_call_wrapper(tr->Close,1,tr);FreePool(data);return st;}

static int w_open(APP id){for(int i=0;i<MAXW;i++)if(win[i].open&&win[i].id==id)return i;for(int i=0;i<MAXW;i++)if(!win[i].open){win[i].open=1;win[i].min=0;win[i].drag=0;win[i].id=id;win[i].x=100+(i%3)*35;win[i].y=85+(i%3)*35;win[i].w=520;win[i].h=340;const char*z[]={"TERMINAL","TASK MANAGER","FILES","SYSTEM INFO","NETWORK","BLEHHH VIEWER","SETTINGS","INSTALLER"};int j=0;while(z[id][j]&&j<23){win[i].title[j]=z[id][j];j++;}win[i].title[j]=0;active=i;return i;}return -1;}
static void draw_term(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};r(c,0x080C12);for(int i=0;i<out_count;i++)t(c.x+12,c.y+10+i*18,(char*)out[i],0xD1DCEC,1);t(c.x+12,c.y+c.h-28,"STEVE>",0x7898C2,1);t(c.x+55,c.y+c.h-28,(char*)input,0xFFFFFF,1);}
static void draw_tasks(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};r(c,C_PANEL());t(c.x+15,c.y+15,"RUNNING TASKS",C_FG(),2);t(c.x+15,c.y+55,"ID",C_SUB(),1);t(c.x+60,c.y+55,"STATE",C_SUB(),1);t(c.x+170,c.y+55,"INSTRUCTION POINTER",C_SUB(),1);for(UINT32 i=0;i<steveos_task_count()&&i<8;i++){STEVEOS_TASK q;if(!EFI_ERROR(steveos_task_get(i,&q))){int y=c.y+80+(int)i*28;n(c.x+15,y,q.id,C_FG(),1);t(c.x+60,y,q.state==STEVEOS_TASK_READY?"READY":"RUNNING",C_FG(),1);n(c.x+170,y,q.instruction_pointer,C_FG(),1);}}}
static void draw_files(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};r(c,C_PANEL());t(c.x+15,c.y+15,"BOOT VOLUME",C_FG(),2);if(!root){t(c.x+15,c.y+55,"NO FILESYSTEM",C_FG(),1);return;}if(!files_ready)scan_files();for(int i=0;i<file_count;i++)t(c.x+18,c.y+55+i*22,file_names[i],C_FG(),1);}
static void draw_system(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};r(c,C_PANEL());t(c.x+18,c.y+18,"SYSTEM",C_FG(),2);t(c.x+18,c.y+58,"RAM MB",C_SUB(),1);n(c.x+170,c.y+58,ram_bytes/1024/1024,C_FG(),1);t(c.x+18,c.y+90,"DISKS",C_SUB(),1);n(c.x+170,c.y+90,disks,C_FG(),1);t(c.x+18,c.y+122,"TASKS",C_SUB(),1);n(c.x+170,c.y+122,steveos_task_count(),C_FG(),1);t(c.x+18,c.y+154,"DISPLAY",C_SUB(),1);n(c.x+170,c.y+154,sc.w,C_FG(),1);t(c.x+212,c.y+154,"X",C_SUB(),1);n(c.x+230,c.y+154,sc.h,C_FG(),1);t(c.x+18,c.y+186,"KERNEL",C_SUB(),1);t(c.x+170,c.y+186,"UEFI HANDOFF",C_FG(),1);}
static void draw_net(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};r(c,C_PANEL());t(c.x+18,c.y+18,"NETWORK",C_FG(),2);t(c.x+18,c.y+60,"NIC",C_SUB(),1);t(c.x+170,c.y+60,EFI_ERROR(net_status)?"OFFLINE":"ONLINE",C_FG(),1);t(c.x+18,c.y+94,"TCP IP",C_SUB(),1);t(c.x+170,c.y+94,"FOUNDATION",C_FG(),1);t(c.x+18,c.y+128,"DNS",C_SUB(),1);t(c.x+170,c.y+128,"NEXT LAYER",C_FG(),1);t(c.x+18,c.y+162,"HTTP",C_SUB(),1);t(c.x+170,c.y+162,"NEXT LAYER",C_FG(),1);}
static void draw_settings(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};r(c,C_PANEL());t(c.x+18,c.y+18,"SETTINGS",C_FG(),2);t(c.x+18,c.y+60,"THEME",C_SUB(),1);t(c.x+170,c.y+60,light?"LIGHT":"DARK",C_FG(),1);rr((RECT){c.x+18,c.y+92,180,36},C_ACC(),7);t(c.x+48,c.y+103,"TOGGLE THEME",C_FG(),1);t(c.x+18,c.y+156,"MOUSE SPEED",C_SUB(),1);rr((RECT){c.x+18,c.y+188,78,36},C_CARD(),7);rr((RECT){c.x+110,c.y+188,78,36},C_CARD(),7);t(c.x+40,c.y+199,"SLOW",C_FG(),1);t(c.x+128,c.y+199,"FAST",C_FG(),1);}
static void draw_image(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};r(c,0);const UINT8*q=_binary_build_boot_raw_start;UINT32 iw=*(UINT32*)q,ih=*(UINT32*)(q+4);const UINT8*pix=q+8;if(iw&&ih){UINT32 dw=c.w,dh=(UINT64)ih*dw/iw;if(dh>(UINT32)c.h){dh=c.h;dw=(UINT64)iw*dh/ih;}int ox=c.x+(c.w-(int)dw)/2,oy=c.y+(c.h-(int)dh)/2;for(UINT32 y=0;y<dh;y++)for(UINT32 x=0;x<dw;x++){UINT32 sx=(UINT64)x*iw/dw,sy=(UINT64)y*ih/dh;const UINT8*v=pix+((UINTN)sy*iw+sx)*4;p(ox+x,oy+y,((UINT32)v[2]<<16)|((UINT32)v[1]<<8)|v[0]);}}}
static void draw_install(WIN*w){RECT c={w->x,w->y+30,w->w,w->h-30};r(c,C_PANEL());t(c.x+16,c.y+15,"OVERWRITE STEVEOS",C_FG(),2);t(c.x+16,c.y+52,"SELECT EFI VOLUME",C_SUB(),1);for(UINTN i=0;i<target_count;i++){RECT q={c.x+15,c.y+76+(int)i*30,c.w-30,25};if((int)i==target_pick)r(q,C_ACC());t(q.x+10,q.y+7,"EFI VOLUME",C_FG(),1);n(q.x+106,q.y+7,i+1,C_FG(),1);}rr((RECT){c.x+15,c.y+c.h-52,190,38},C_ACC(),7);t(c.x+34,c.y+c.h-41,"OVERWRITE INSTALL",C_FG(),1);if(install_result)t(c.x+225,c.y+c.h-41,install_result==1?"COMPLETE":"FAILED",C_FG(),1);}
static void draw_win(WIN*w){if(!w->open||w->min)return;rr((RECT){w->x+5,w->y+6,w->w,w->h},0x080C12,8);r((RECT){w->x,w->y,w->w,w->h},C_PANEL());r((RECT){w->x,w->y,w->w,30},C_BAR());t(w->x+10,w->y+9,w->title,C_FG(),1);r((RECT){w->x+w->w-60,w->y+7,14,14},C_CARD());r((RECT){w->x+w->w-40,w->y+7,14,14},C_CARD());r((RECT){w->x+w->w-20,w->y+7,14,14},C_ACC());g(w->x+w->w-57,w->y+9,'_',C_FG(),1);g(w->x+w->w-37,w->y+9,'O',C_FG(),1);g(w->x+w->w-17,w->y+9,'X',C_FG(),1);if(w->id==W_TERM)draw_term(w);else if(w->id==W_TASKS)draw_tasks(w);else if(w->id==W_FILES)draw_files(w);else if(w->id==W_SYSTEM)draw_system(w);else if(w->id==W_NETWORK)draw_net(w);else if(w->id==W_IMAGE)draw_image(w);else if(w->id==W_SETTINGS)draw_settings(w);else if(w->id==W_INSTALL)draw_install(w);}
static void desktop(){r((RECT){0,0,sc.w,sc.h},C_BG());for(int y=50;y<(int)sc.h-55;y+=75)r((RECT){0,y,sc.w,1},light?0xDCE2E9:0x141C28);r((RECT){0,0,sc.w,42},C_BAR());t(15,12,"STEVEOS",C_FG(),1);t(105,12,"WINDOWS DESKTOP",C_SUB(),1);t(32,70,"WELCOME",C_FG(),3);t(34,110,"CUSTOM UEFI OPERATING SYSTEM",C_SUB(),1);rr((RECT){30,155,235,110},C_PANEL(),10);t(48,174,"MEMORY",C_SUB(),1);n(48,204,ram_bytes/1024/1024,C_FG(),2);t(128,206,"MB",C_SUB(),1);t(48,238,"TASKS",C_SUB(),1);n(128,236,steveos_task_count(),C_FG(),2);rr((RECT){285,155,235,110},C_PANEL(),10);t(303,174,"NETWORK",C_SUB(),1);t(303,207,EFI_ERROR(net_status)?"NIC OFFLINE":"NIC ONLINE",C_FG(),2);t(303,238,"FIRMWARE",C_SUB(),1);rr((RECT){30,287,490,58},C_CARD(),10);t(48,305,"T TERMINAL",C_FG(),1);t(190,305,"K TASKS",C_FG(),1);t(310,305,"F FILES",C_FG(),1);t(415,305,"S START",C_FG(),1);rr((RECT){18,sc.h-52,155,38},C_ACC(),8);t(50,sc.h-41,"START",C_FG(),1);}
static void start(){int h=55+9*36+12;RECT m={18,(int)sc.h-h-10,440,h};rr(m,C_PANEL(),10);r((RECT){m.x,m.y,m.w,42},C_BAR());t(m.x+16,m.y+13,"STEVEOS",C_FG(),2);const char*a[]={"TERMINAL","TASK MANAGER","FILES","SYSTEM INFO","NETWORK","BLEHHH VIEWER","SETTINGS","INSTALLER","ABOUT"};for(int i=0;i<9;i++){RECT q={m.x+10,m.y+48+i*36,m.w-20,29};if(i==start_pick)r(q,C_ACC());t(q.x+12,q.y+8,a[i],C_FG(),1);}}
static void redraw(){desktop();for(int i=0;i<MAXW;i++)draw_win(&win[i]);if(start_open)start();cur();}
static void launch(int id){if(id<0||id>7)return;if(id==8){return;}APP a=(APP)id;open_window(a,a==W_TERM?"TERMINAL":a==W_TASKS?"TASK MANAGER":a==W_FILES?"FILES":a==W_SYSTEM?"SYSTEM INFO":a==W_NETWORK?"NETWORK":a==W_IMAGE?"BLEHHH VIEWER":a==W_SETTINGS?"SETTINGS":"INSTALLER");if(a==W_INSTALL){discover_targets();install_result=0;}start_open=0;redraw();}
static void click(int x,int y){if(start_open){int h=55+9*36+12;RECT m={18,(int)sc.h-h-10,440,h};for(int i=0;i<9;i++){RECT q={m.x+10,m.y+48+i*36,m.w-20,29};if(hit(x,y,q)){if(i<8)launch(i);return;}}return;}for(int i=MAXW-1;i>=0;i--){WIN*w=&win[i];if(!w->open||w->min)continue;if(!hit(x,y,(RECT){w->x,w->y,w->w,w->h}))continue;active=i;if(hit(x,y,(RECT){w->x+w->w-20,w->y+7,14,14})){w->open=0;redraw();return;}if(hit(x,y,(RECT){w->x+w->w-60,w->y+7,14,14})){w->min=1;redraw();return;}if(hit(x,y,(RECT){w->x+w->w-40,w->y+7,14,14})){w->x=8;w->y=48;w->w=sc.w-16;w->h=sc.h-105;redraw();return;}if(hit(x,y,(RECT){w->x,w->y,w->w,30})){w->drag=1;w->ox=x-w->x;w->oy=y-w->y;return;}RECT c={w->x,w->y+30,w->w,w->h-30};if(w->id==W_SETTINGS){if(hit(x,y,(RECT){c.x+18,c.y+92,180,36})){light=!light;redraw();return;}if(hit(x,y,(RECT){c.x+18,c.y+188,78,36})){mouse_scale=3;redraw();return;}if(hit(x,y,(RECT){c.x+110,c.y+188,78,36})){mouse_scale=1;redraw();return;}}if(w->id==W_INSTALL){for(UINTN n0=0;n0<target_count;n0++){if(hit(x,y,(RECT){c.x+15,c.y+76+(int)n0*30,c.w-30,25})){target_pick=n0;redraw();return;}}if(hit(x,y,(RECT){c.x+15,c.y+c.h-52,190,38})&&target_pick>=0){install_result=EFI_ERROR(install())?2:1;redraw();return;}}return;}if(hit(x,y,(RECT){18,(int)sc.h-58,155,50})){start_open=!start_open;start_pick=0;redraw();}}
static void motion(){if(!mouse)return;EFI_SIMPLE_POINTER_STATE q;if(EFI_ERROR(uefi_call_wrapper(mouse->GetState,2,mouse,&q)))return;mx+=(INTN)q.RelativeMovementX/mouse_scale;my+=(INTN)q.RelativeMovementY/mouse_scale;if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)sc.w)mx=sc.w-1;if(my>=(int)sc.h)my=sc.h-1;int l=q.LeftButton?1:0;if(l&&!left_old)click(mx,my);if(l&&active>=0&&win[active].drag){win[active].x=mx-win[active].ox;win[active].y=my-win[active].oy;if(win[active].x<0)win[active].x=0;if(win[active].y<42)win[active].y=42;redraw();}if(!l&&left_old&&active>=0)win[active].drag=0;left_old=l;}
static void keyboard(EFI_INPUT_KEY k){if(start_open){if(k.ScanCode==SCAN_UP){start_pick--;if(start_pick<0)start_pick=8;redraw();return;}if(k.ScanCode==SCAN_DOWN){start_pick++;if(start_pick>8)start_pick=0;redraw();return;}if(k.UnicodeChar==CHAR_CARRIAGE_RETURN){if(start_pick<8)launch(start_pick);else uefi_call_wrapper(RT->ResetSystem,4,EfiResetShutdown,EFI_SUCCESS,0,NULL);return;}if(k.ScanCode==SCAN_ESC){start_open=0;redraw();return;}}
if(active>=0&&win[active].open&&win[active].id==W_TERM){if(k.UnicodeChar==CHAR_BACKSPACE){if(input_len)input[--input_len]=0;redraw();return;}if(k.UnicodeChar==CHAR_CARRIAGE_RETURN){termcmd();redraw();return;}if(k.UnicodeChar>=32&&k.UnicodeChar<127&&input_len<89){CHAR8 c=(CHAR8)k.UnicodeChar;if(c>='a'&&c<='z')c-=32;input[input_len++]=c;input[input_len]=0;redraw();return;}}
if(k.UnicodeChar=='s'||k.UnicodeChar=='S'){start_open=!start_open;start_pick=0;redraw();return;}if(k.UnicodeChar=='t'||k.UnicodeChar=='T'){launch(0);return;}if(k.UnicodeChar=='k'||k.UnicodeChar=='K'){launch(1);return;}if(k.UnicodeChar=='f'||k.UnicodeChar=='F'){launch(2);return;}if(k.ScanCode==SCAN_ESC&&active>=0){win[active].open=0;active=-1;redraw();}}

EFI_STATUS steveos_shell_start(EFI_HANDLE ih,EFI_GRAPHICS_OUTPUT_PROTOCOL *gp){gop=gp;sc.w=gp->Mode->Info->HorizontalResolution;sc.h=gp->Mode->Info->VerticalResolution;sc.stride=gp->Mode->Info->PixelsPerScanLine;sc.fb=(UINT32*)(UINTN)gp->Mode->FrameBufferBase;mx=sc.w/2;my=sc.h/2;EFI_LOADED_IMAGE_PROTOCOL*li=0;if(!EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol,3,ih,&gEfiLoadedImageProtocolGuid,(VOID**)&li))&&li)source_device=li->DeviceHandle;if(source_device)uefi_call_wrapper(BS->HandleProtocol,3,source_device,&gEfiSimpleFileSystemProtocolGuid,(VOID**)&root);EFI_GUID sp={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0,0x90,0x27,0x3f,0xc1,0x4d}};uefi_call_wrapper(BS->LocateProtocol,3,&sp,NULL,(VOID**)&mouse);if(mouse)mouse_event=mouse->WaitForInput;uefi_call_wrapper(BS->CreateEvent,5,EVT_TIMER,TPL_CALLBACK,NULL,NULL,&timer_event);uefi_call_wrapper(BS->SetTimer,3,timer_event,TimerPeriodic,200000);stats();steveos_tasks_init();task_add_shell();addline("STEVEOS TERMINAL READY");addline("TYPE HELP FOR COMMANDS");redraw();EFI_EVENT ev[3];UINTN ec=0;ev[ec++]=ST->ConIn->WaitForKey;if(mouse_event)ev[ec++]=mouse_event;ev[ec++]=timer_event;for(;;){UINTN ix=0;if(EFI_ERROR(uefi_call_wrapper(BS->WaitForEvent,3,ec,ev,&ix)))continue;if(mouse_event&&ix==1){motion();continue;}if(ix==ec-1){stats();redraw();continue;}EFI_INPUT_KEY k;if(!EFI_ERROR(uefi_call_wrapper(ST->ConIn->ReadKeyStroke,2,ST->ConIn,&k)))keyboard(k);}return EFI_SUCCESS;}
