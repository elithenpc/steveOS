#include <stdint.h>
#include <stddef.h>
#include "../src/bootinfo.h"
#include "desktop.h"

#define EFI_CONVENTIONAL_MEMORY 7
#define NOTE_MAX 512
#define CALC_MAX 31
#define TERM_MAX 256
#define APP_DESKTOP 0
#define APP_CALC 1
#define APP_NOTEPAD 2
#define APP_FILES 3
#define APP_IMAGE 4
#define APP_SETTINGS 5
#define APP_TASKS 6
#define APP_TERMINAL 7
#define APP_ABOUT 8
#define CARD_COUNT 9

typedef struct { uint32_t a,b,c,d; } GUID;
typedef uint64_t (__attribute__((ms_abi)) *EFI_GET_VARIABLE)(uint16_t*, GUID*, uint32_t*, uint64_t*, void*);
typedef uint64_t (__attribute__((ms_abi)) *EFI_SET_VARIABLE)(uint16_t*, GUID*, uint32_t, uint64_t, void*);
typedef struct { uint32_t magic; uint8_t light; uint8_t scale; uint16_t reserved; } STEVE_SETTINGS;

extern void native_reboot(void);
extern void native_halt(void);
extern void native_pointer_set_scale(uint8_t scale);
extern uint8_t native_keyboard_read_scancode(void);
extern uint32_t native_pointer_x(void);
extern uint32_t native_pointer_y(void);
extern uint8_t native_pointer_buttons(void);
extern void native_pointer_hide(void);
extern void native_pointer_show(void);
extern int native_usb_mouse_present(void);
extern int native_i2c_hid_present(void);
extern const unsigned char _binary_build_boot_raw_start[];
extern const unsigned char _binary_build_boot_raw_end[];

static STEVEOS_BOOT_INFO *boot;
static STEVEOS_BOOT_FILE *boot_files;
static uint32_t *framebuffer;
static uint32_t *backbuffer;
static uint32_t fb_width,fb_height,fb_stride;
static uint64_t memory_total,largest_free;
static int app=APP_DESKTOP;
static int selected_file=-1,file_scroll;
static uint8_t previous_buttons,theme_light,pointer_scale=1,dirty_note;
static uint8_t note[NOTE_MAX+1];
static size_t note_len,note_cursor;
static char calc_input[CALC_MAX+1];
static size_t calc_len;
static uint64_t calc_value;
static char calc_op;
static uint8_t calc_pending;
static char term_input[TERM_MAX];
static size_t term_len;
static char term_lines[10][64];
static uint8_t term_count;

static const GUID note_guid={0x53544556,0x4F53,0x4E56,0x00010001};
static const GUID settings_guid={0x53544556,0x4F53,0x4E56,0x00010002};
static const uint16_t note_name[]={'S','t','e','v','e','O','S','N','o','t','e',0};
static const uint16_t settings_name[]={'S','t','e','v','e','O','S','S','e','t','t','i','n','g','s',0};
static const STEVE_SETTINGS default_settings={0x53545653u,0,1,0};

static const uint8_t letters[26][5]={
{0x3E,0x09,0x09,0x09,0x3E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
{0x41,0x41,0x7F,0x41,0x41},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}};
static const uint8_t digits[10][5]={
{0x3E,0x45,0x49,0x51,0x3E},{0x00,0x21,0x7F,0x01,0x00},{0x23,0x45,0x49,0x49,0x31},{0x22,0x41,0x49,0x49,0x36},{0x0C,0x14,0x24,0x7F,0x04},
{0x7A,0x49,0x49,0x49,0x46},{0x3E,0x49,0x49,0x49,0x26},{0x40,0x47,0x48,0x50,0x60},{0x36,0x49,0x49,0x49,0x36},{0x32,0x49,0x49,0x49,0x3E}};

static uint32_t bg(void){return theme_light?0xF2F5F8u:0x0B1220u;}
static uint32_t panel(void){return theme_light?0xFFFFFFu:0x151F2Fu;}
static uint32_t panel2(void){return theme_light?0xE2E8F0u:0x1D2A3Eu;}
static uint32_t fg(void){return theme_light?0x172033u:0xF4F7FBu;}
static uint32_t sub(void){return theme_light?0x5D6B7Eu:0xA8B5C7u;}
static uint32_t accent(void){return theme_light?0x6D7F95u:0x4F6582u;}
static uint32_t good(void){return theme_light?0x4D8B68u:0x8BC6A3u;}
static uint32_t text_light(void){return theme_light?0x18202Bu:0xEAF0F6u;}

static void px(int x,int y,uint32_t c){if(backbuffer&&x>=0&&y>=0&&(uint32_t)x<fb_width&&(uint32_t)y<fb_height)backbuffer[(size_t)y*fb_stride+(size_t)x]=c;}
static void rect(int x,int y,int w,int h,uint32_t c){if(!backbuffer||w<=0||h<=0)return;int x0=x<0?0:x,y0=y<0?0:y,x1=x+w,y1=y+h;if(x1>(int)fb_width)x1=(int)fb_width;if(y1>(int)fb_height)y1=(int)fb_height;for(int yy=y0;yy<y1;yy++){uint32_t*p=backbuffer+(size_t)yy*fb_stride+x0;for(int xx=x0;xx<x1;xx++)*p++=c;}}
static int inside(uint32_t x,uint32_t y,int a,int b,int w,int h){return x>=(uint32_t)a&&x<(uint32_t)(a+w)&&y>=(uint32_t)b&&y<(uint32_t)(b+h);}
static const uint8_t*glyph_data(char c){if(c>='A'&&c<='Z')return letters[c-'A'];if(c>='a'&&c<='z')return letters[c-'a'];if(c>='0'&&c<='9')return digits[c-'0'];return NULL;}
static void glyph(int x,int y,char c,uint32_t col,int s){const uint8_t*g=glyph_data(c);if(g){for(int a=0;a<5;a++)for(int b=0;b<7;b++)if(g[a]&(1u<<b))rect(x+a*s,y+b*s,s,s,col);return;}if(c=='+'){rect(x+2*s,y,s,7*s,col);rect(x,y+3*s,5*s,s,col);}else if(c=='-')rect(x,y+3*s,5*s,s,col);else if(c=='='){rect(x,y+2*s,5*s,s,col);rect(x,y+5*s,5*s,s,col);}else if(c=='*'){rect(x+2*s,y,s,7*s,col);rect(x,y+2*s,5*s,s,col);}else if(c=='/')for(int i=0;i<7;i++)rect(x+(6-i)*s,y+i*s,s,s,col);else if(c==':'){rect(x,y+s,s,s,col);rect(x,y+5*s,s,s,col);}else if(c=='.')rect(x,y+6*s,s,s,col);else if(c=='_')rect(x,y+6*s,5*s,s,col);else if(c=='!'){rect(x+2*s,y,s,5*s,col);rect(x+2*s,y+6*s,s,s,col);}else if(c=='?'){rect(x+s,y,3*s,s,col);rect(x+3*s,y+s,s,2*s,col);rect(x+2*s,y+3*s,s,s,col);rect(x+2*s,y+6*s,s,s,col);}}
static void text(int x,int y,const char*s,uint32_t col,int sc){while(*s){if(*s==' ')x+=6*sc;else{glyph(x,y,*s,col,sc);x+=6*sc;}s++;}}
static void number(int x,int y,uint64_t v,uint32_t col,int sc){char b[32];int n=0;if(!v)b[n++]='0';while(v&&n<31){b[n++]=(char)('0'+v%10);v/=10;}while(n){glyph(x,y,b[--n],col,sc);x+=6*sc;}}
static void present(void){if(!framebuffer||!backbuffer)return;native_pointer_hide();size_t count=(size_t)fb_stride*fb_height,q=count/8;uint64_t*dst=(uint64_t*)framebuffer;const uint64_t*src=(const uint64_t*)backbuffer;__asm__ __volatile__("cld; rep movsq":"+D"(dst),"+S"(src),"+c"(q):"memory");for(size_t i=q*8;i<count;i++)framebuffer[i]=backbuffer[i];native_pointer_show();}
static int init_backbuffer(void){uint64_t bytes=(uint64_t)fb_stride*fb_height*4ULL;if(!boot->memory_map||!boot->memory_descriptor_size){backbuffer=framebuffer;return 1;}uint8_t*p=(uint8_t*)(uintptr_t)boot->memory_map,*e=p+boot->memory_map_size;uint64_t best_end=0;while(p+boot->memory_descriptor_size<=e){uint32_t type=*(uint32_t*)p;uint64_t*q=(uint64_t*)(p+8);uint64_t start=q[0],size=q[1]*4096ULL;if(type==EFI_CONVENTIONAL_MEMORY&&size>=bytes+0x100000ULL&&start+size>best_end)best_end=start+size;p+=boot->memory_descriptor_size;}if(!best_end){backbuffer=framebuffer;return 1;}backbuffer=(uint32_t*)(uintptr_t)((best_end-bytes)&~0xFFFULL);for(uint64_t i=0;i<(uint64_t)fb_stride*fb_height;i++)backbuffer[i]=bg();return 1;}
static void memory_stats(void){memory_total=largest_free=0;uint8_t*p=(uint8_t*)(uintptr_t)boot->memory_map,*e=p+boot->memory_map_size;while(p+boot->memory_descriptor_size<=e){uint32_t type=*(uint32_t*)p;uint64_t*q=(uint64_t*)(p+8);if(type==EFI_CONVENTIONAL_MEMORY){uint64_t s=q[1]*4096ULL;memory_total+=s;if(s>largest_free)largest_free=s;}p+=boot->memory_descriptor_size;}}
static void load_settings(void){theme_light=default_settings.light;pointer_scale=default_settings.scale;if(boot->uefi_get_variable){EFI_GET_VARIABLE get=(EFI_GET_VARIABLE)(uintptr_t)boot->uefi_get_variable;STEVE_SETTINGS s=default_settings;uint32_t a=0;uint64_t sz=sizeof(s);if(get((uint16_t*)settings_name,(GUID*)&settings_guid,&a,&sz,&s)==0&&s.magic==default_settings.magic){theme_light=s.light?1:0;pointer_scale=s.scale<1?1:(s.scale>4?4:s.scale);}}native_pointer_set_scale(pointer_scale);}
static void save_settings(void){if(!boot->uefi_set_variable)return;EFI_SET_VARIABLE set=(EFI_SET_VARIABLE)(uintptr_t)boot->uefi_set_variable;STEVE_SETTINGS s={default_settings.magic,theme_light,pointer_scale,0};set((uint16_t*)settings_name,(GUID*)&settings_guid,7,sizeof(s),&s);}
static void load_note(void){note_len=note_cursor=0;dirty_note=0;if(!boot->uefi_get_variable)return;EFI_GET_VARIABLE get=(EFI_GET_VARIABLE)(uintptr_t)boot->uefi_get_variable;uint32_t a=0;uint64_t sz=NOTE_MAX;if(get((uint16_t*)note_name,(GUID*)&note_guid,&a,&sz,note)==0){if(sz>NOTE_MAX)sz=NOTE_MAX;note_len=(size_t)sz;note_cursor=note_len;}note[note_len]=0;}
static void save_note(void){if(!boot->uefi_set_variable)return;EFI_SET_VARIABLE set=(EFI_SET_VARIABLE)(uintptr_t)boot->uefi_set_variable;set((uint16_t*)note_name,(GUID*)&note_guid,7,note_len,note);dirty_note=0;}
static void file_name(const STEVEOS_BOOT_FILE*f,char*out,size_t cap){size_t i=0;if(!out||!cap)return;while(f&&i+1<cap&&i<STEVEOS_BOOT_FILE_NAME_MAX&&f->name[i]){uint16_t c=f->name[i++];out[i-1]=(c<128)?(char)c:'?';}out[i]=0;}
static void load_text_file(STEVEOS_BOOT_FILE*f){if(!f||!f->data||!f->size)return;size_t n=(size_t)f->size;if(n>NOTE_MAX)n=NOTE_MAX;const uint8_t*d=(const uint8_t*)(uintptr_t)f->data;for(size_t i=0;i<n;i++)note[i]=(d[i]>=32||d[i]=='\n'||d[i]=='\t')?d[i]:' ';note[n]=0;note_len=n;note_cursor=n;dirty_note=0;app=APP_NOTEPAD;}
static void topbar(const char*title){rect(0,0,(int)fb_width,56,theme_light?0xDDE4ECu:0x121B29u);text(24,18,"STEVEOS",fg(),2);text(145,22,title,sub(),1);text((int)fb_width-180,22,native_usb_mouse_present()?"USB MOUSE":"NATIVE INPUT",sub(),1);}
static void draw_desktop(void){rect(0,0,(int)fb_width,(int)fb_height,bg());topbar("DESKTOP");text(38,82,"WELCOME TO STEVEOS",fg(),3);text(40,114,"NATIVE KERNEL DESKTOP",sub(),1);int cw=(int)(fb_width>=900?255:190),ch=78,g=14,x0=34,y0=150;const char*names[CARD_COUNT]={"CALCULATOR","NOTEPAD","FILES","SETTINGS","TASK MANAGER","TERMINAL","IMAGE VIEWER","ABOUT","SYSTEM"};const char*hints[CARD_COUNT]={"CALC","EDITOR","BROWSE","OPTIONS","PROCESSES","SHELL","PICTURES","CREDITS","HARDWARE"};for(int i=0;i<CARD_COUNT;i++){int col=i%3,row=i/3,x=x0+col*(cw+g),y=y0+row*(ch+g);rect(x+3,y+4,cw,ch,0x050A12u);rect(x,y,cw,ch,panel());rect(x+12,y+11,40,40,panel2());rect(x+22,y+19,20,24,accent());text(x+62,y+17,names[i],fg(),1);text(x+62,y+38,hints[i],sub(),1);}rect(34,(int)fb_height-76,(int)fb_width-68,44,panel2());text(50,(int)fb_height-61,"F1 CALC F2 NOTE F3 FILES F4 SETTINGS F6 TASKS F7 TERMINAL",sub(),1);text(50,(int)fb_height-41,"F8 ABOUT ESC DESKTOP CLICK CARDS",sub(),1);}
static uint64_t calc_input_value(void){uint64_t v=0;for(size_t i=0;i<calc_len;i++)v=v*10+(uint64_t)(calc_input[i]-'0');return v;}
static void calc_apply(char op){uint64_t b=calc_input_value();if(!calc_pending)calc_value=b;else if(calc_op=='+')calc_value+=b;else if(calc_op=='-')calc_value=calc_value>b?calc_value-b:0;else if(calc_op=='*')calc_value*=b;else if(calc_op=='/'&&b)calc_value/=b;calc_op=op;calc_pending=1;calc_len=0;calc_input[0]=0;}
static void calc_equals(void){uint64_t b=calc_input_value();if(!calc_pending)calc_value=b;else if(calc_op=='+')calc_value+=b;else if(calc_op=='-')calc_value=calc_value>b?calc_value-b:0;else if(calc_op=='*')calc_value*=b;else if(calc_op=='/'&&b)calc_value/=b;calc_len=0;calc_input[0]=0;calc_pending=0;}
static void draw_calc(void){rect(0,0,(int)fb_width,(int)fb_height,bg());topbar("CALCULATOR");rect(38,88,(int)fb_width-76,82,panel());if(calc_len)text(58,116,calc_input,fg(),3);else number(58,116,calc_value,fg(),3);const char*keys[16]={"7","8","9","/","4","5","6","*","1","2","3","-","0","C","=","+"};int bw=100,bh=54,g=12,x0=38,y0=195;for(int i=0;i<16;i++){int x=x0+(i%4)*(bw+g),y=y0+(i/4)*(bh+g);rect(x,y,bw,bh,panel());text(x+42,y+18,keys[i],fg(),2);}text(38,490,"DIGITS + - * / ENTER = BACKSPACE DELETE C CLEAR",sub(),1);}
static void draw_notepad(void){rect(0,0,(int)fb_width,(int)fb_height,bg());topbar("NOTEPAD");rect(34,78,(int)fb_width-68,(int)fb_height-150,theme_light?0xFBFCFEu:0x182231u);int x=50,y=98;for(size_t i=0;i<note_len&&y<(int)fb_height-105;i++){char c=(char)note[i];if(c=='\n'){x=50;y+=16;continue;}glyph(x,y,c,text_light(),1);x+=6;if(x>(int)fb_width-55){x=50;y+=16;}}rect(34,(int)fb_height-62,(int)fb_width-68,36,panel2());text(50,(int)fb_height-49,"F5 SAVE",fg(),1);text(115,(int)fb_height-49,dirty_note?"UNSAVED":"SAVED",dirty_note?0xD8A654u:good(),1);text(220,(int)fb_height-49,"ENTER NEW LINE BACKSPACE DELETE ARROWS MOVE",sub(),1);}
static uint32_t r16p(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8);}static uint32_t r32p(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void draw_builtin_image(void){const uint8_t*raw=_binary_build_boot_raw_start,*end=_binary_build_boot_raw_end;if((size_t)(end-raw)<8)return;uint32_t w=*(const uint32_t*)raw,h=*(const uint32_t*)(raw+4);const uint8_t*p=raw+8;if(!w||!h||(size_t)w*h*4+8>(size_t)(end-raw))return;uint32_t dw=520,dh=(uint64_t)h*dw/w;if(dh>470){dh=470;dw=(uint64_t)w*dh/h;}int ox=((int)fb_width-(int)dw)/2,oy=92;for(uint32_t y=0;y<dh;y++){uint32_t sy=(uint64_t)y*h/dh;for(uint32_t x=0;x<dw;x++){uint32_t sx=(uint64_t)x*w/dw;const uint8_t*v=p+((size_t)sy*w+sx)*4;px(ox+(int)x,oy+(int)y,0xFF000000u|(uint32_t)v[2]|((uint32_t)v[1]<<8)|((uint32_t)v[0]<<16));}}}
static void draw_bmp(const uint8_t*d,size_t len){if(len<54||d[0]!='B'||d[1]!='M')return;uint32_t off=r32p(d+10),w=r32p(d+18),hraw=r32p(d+22);int32_t h=(int32_t)hraw;uint16_t planes=(uint16_t)r16p(d+26),bpp=(uint16_t)r16p(d+28);if(!w||!h||planes!=1||(bpp!=24&&bpp!=32)||r32p(d+30)!=0)return;uint32_t hh=(uint32_t)(h<0?-h:h),row=((w*bpp+31)/32)*4;if((uint64_t)off+(uint64_t)row*hh>len)return;uint32_t dw=520,dh=(uint64_t)hh*dw/w;if(dh>470){dh=470;dw=(uint64_t)w*dh/hh;}int ox=((int)fb_width-(int)dw)/2,oy=92,bb=bpp/8;for(uint32_t y=0;y<dh;y++){uint32_t sy=(uint64_t)y*hh/dh;if(h>0)sy=hh-1-sy;for(uint32_t x=0;x<dw;x++){uint32_t sx=(uint64_t)x*w/dw;const uint8_t*v=d+off+(uint64_t)sy*row+(uint64_t)sx*bb;px(ox+(int)x,oy+(int)y,0xFF000000u|(uint32_t)v[2]|((uint32_t)v[1]<<8)|((uint32_t)v[0]<<16));}}}
static void draw_image(void){rect(0,0,(int)fb_width,(int)fb_height,bg());topbar("IMAGE VIEWER");if(selected_file>=0&&boot_files&&(uint64_t)selected_file<boot->boot_file_count){STEVEOS_BOOT_FILE*f=&boot_files[selected_file];char n[64];file_name(f,n,sizeof(n));text(30,74,n,sub(),1);if(f->data&&f->size){const uint8_t*d=(const uint8_t*)(uintptr_t)f->data;if(f->size>=2&&d[0]=='B'&&d[1]=='M')draw_bmp(d,(size_t)f->size);else draw_builtin_image();}else draw_builtin_image();}else draw_builtin_image();text(30,(int)fb_height-44,"ESC BACK TO FILES BMP + BUILT-IN IMAGE",sub(),1);}
static void draw_files(void){rect(0,0,(int)fb_width,(int)fb_height,bg());topbar("FILE EXPLORER");rect(30,76,(int)fb_width-60,38,panel2());text(46,89,"COMPUTER / BOOT VOLUME",fg(),1);int shown=0;for(uint64_t i=(uint64_t)file_scroll;i<boot->boot_file_count&&shown<9;i++,shown++){STEVEOS_BOOT_FILE*f=&boot_files[i];char n[64];file_name(f,n,sizeof(n));int y=130+shown*45;rect(30,y,(int)fb_width-60,36,selected_file==(int)i?accent():panel());text(46,y+11,f->kind==1?"IMG":(f->kind==2?"TXT":"FILE"),fg(),1);text(92,y+11,n,fg(),1);number(540,y+11,f->size,sub(),1);}text(34,(int)fb_height-48,"UP DOWN SCROLL ENTER OPEN CLICK FILE ESC DESKTOP",sub(),1);}
static void draw_settings(void){rect(0,0,(int)fb_width,(int)fb_height,bg());topbar("SETTINGS");text(38,84,"APPEARANCE",fg(),2);rect(34,120,(int)fb_width-68,54,panel());text(52,137,"THEME",fg(),1);text(330,137,theme_light?"LIGHT":"DARK",sub(),1);rect(34,192,(int)fb_width-68,54,panel());text(52,209,"POINTER SCALE",fg(),1);number(330,209,pointer_scale,fg(),1);text(52,223,"LEFT RIGHT TO CHANGE",sub(),1);rect(34,264,(int)fb_width-68,96,panel());text(52,281,"INPUT DEVICES",fg(),1);text(52,304,native_usb_mouse_present()?"USB HID MOUSE ACTIVE":"USB HID MOUSE OFF",sub(),1);text(52,324,native_i2c_hid_present()?"I2C HID TOUCHPAD ACTIVE":"I2C HID TOUCHPAD OFF",sub(),1);text(52,405,"F5 SAVE SETTINGS TO NVRAM",fg(),1);}
static void draw_tasks(void){rect(0,0,(int)fb_width,(int)fb_height,bg());topbar("TASK MANAGER");text(34,82,"STEVEOS COMPONENTS",fg(),2);const char*names[]={"KERNEL MAIN LOOP","FRAMEBUFFER","PS2 KEYBOARD","USB HID MOUSE","I2C HID TOUCHPAD","NVRAM","BOOT FILE SNAPSHOT"};for(int i=0;i<7;i++){int y=122+i*44;int active=(i<3)||(i==3&&native_usb_mouse_present())||(i==4&&native_i2c_hid_present())||i>4;rect(30,y,(int)fb_width-60,34,panel());text(46,y+11,names[i],fg(),1);text((int)fb_width-140,y+11,active?"ACTIVE":"OFF",active?good():sub(),1);}rect(30,444,(int)fb_width-60,76,panel2());text(46,460,"MEMORY",sub(),1);number(46,482,memory_total/1024/1024,fg(),2);text(106,485,"MB",sub(),1);text(250,460,"LARGEST REGION",sub(),1);number(250,482,largest_free/1024/1024,fg(),2);text(346,485,"MB",sub(),1);}
static void term_add(const char*s){if(term_count<10){size_t i=0;while(s[i]&&i<63){term_lines[term_count][i]=s[i];i++;}term_lines[term_count][i]=0;term_count++;}else{for(int r=1;r<10;r++)for(int c=0;c<64;c++)term_lines[r-1][c]=term_lines[r][c];size_t i=0;while(s[i]&&i<63){term_lines[9][i]=s[i];i++;}term_lines[9][i]=0;}}
static int streq(const char*a,const char*b){while(*a&&*b&&*a==*b){a++;b++;}return *a==0&&*b==0;}
static void terminal_exec(void){term_input[term_len]=0;term_add(term_input);if(streq(term_input,"HELP"))term_add("HELP MEM APPS CLEAR REBOOT HALT");else if(streq(term_input,"MEM"))term_add("MEMORY IS SHOWN IN TASK MANAGER");else if(streq(term_input,"APPS"))term_add("F1 F2 F3 F4 F6 F7 F8");else if(streq(term_input,"CLEAR"))term_count=0;else if(streq(term_input,"REBOOT"))native_reboot();else if(streq(term_input,"HALT"))native_halt();else if(term_len)term_add("UNKNOWN COMMAND");term_len=0;term_input[0]=0;}
static void draw_terminal(void){rect(0,0,(int)fb_width,(int)fb_height,bg());topbar("TERMINAL");for(int i=0;i<10;i++)text(32,82+i*24,term_lines[i],text_light(),1);rect(28,(int)fb_height-62,(int)fb_width-56,38,panel2());text(40,(int)fb_height-50,term_input,text_light(),1);}
static void draw_about(void){rect(0,0,(int)fb_width,(int)fb_height,bg());topbar("ABOUT");text(38,92,"STEVEOS",fg(),3);text(40,132,"NATIVE KERNEL DESKTOP",sub(),1);text(40,174,"UEFI GOP FRAMEBUFFER",fg(),1);text(40,202,"X86-64 GDT IDT PAGING",sub(),1);text(40,226,"PS2 USB HID I2C HID INPUT",sub(),1);text(40,250,"PERSISTENT NVRAM NOTES + SETTINGS",sub(),1);rect(36,300,(int)fb_width-72,130,panel());text(54,322,"MINT-Y SOURCE",fg(),1);text(54,344,"linuxmint/mint-y-icons",sub(),1);text(54,366,"source tracked under third_party",sub(),1);text(54,388,"SEE THIRD_PARTY.MD FOR CREDITS",sub(),1);}
static void render_app(void){switch(app){case APP_DESKTOP:draw_desktop();break;case APP_CALC:draw_calc();break;case APP_NOTEPAD:draw_notepad();break;case APP_FILES:draw_files();break;case APP_IMAGE:draw_image();break;case APP_SETTINGS:draw_settings();break;case APP_TASKS:draw_tasks();break;case APP_TERMINAL:draw_terminal();break;default:draw_about();break;}present();}
static void open_card(uint32_t x,uint32_t y){int cw=(int)(fb_width>=900?255:190),ch=78,g=14,x0=34,y0=150;for(int i=0;i<CARD_COUNT;i++){int col=i%3,row=i/3;if(inside(x,y,x0+col*(cw+g),y0+row*(ch+g),cw,ch)){if(i==0)app=APP_CALC;else if(i==1)app=APP_NOTEPAD;else if(i==2)app=APP_FILES;else if(i==3)app=APP_SETTINGS;else if(i==4||i==8)app=APP_TASKS;else if(i==5)app=APP_TERMINAL;else if(i==6)app=APP_IMAGE;else app=APP_ABOUT;selected_file=-1;render_app();return;}}}
static void handle_click(uint32_t x,uint32_t y){if(app==APP_DESKTOP){open_card(x,y);return;}if(app==APP_CALC){int bw=100,bh=54,g=12,x0=38,y0=195;const char*keys="789/456*123-0C=+";for(int i=0;i<16;i++){int bx=x0+(i%4)*(bw+g),by=y0+(i/4)*(bh+g);if(inside(x,y,bx,by,bw,bh)){char k=keys[i];if(k>='0'&&k<='9'&&calc_len<CALC_MAX){calc_input[calc_len++]=k;calc_input[calc_len]=0;}else if(k=='C'){calc_len=0;calc_input[0]=0;calc_value=0;calc_pending=0;}else if(k=='=')calc_equals();else calc_apply(k);render_app();return;}}return;}if(app==APP_FILES){for(int row=0;row<9;row++){uint64_t idx=(uint64_t)file_scroll+row;if(idx>=boot->boot_file_count)break;if(inside(x,y,30,130+row*45,(int)fb_width-60,36)){selected_file=(int)idx;STEVEOS_BOOT_FILE*f=&boot_files[selected_file];if(f->kind==1)app=APP_IMAGE;else if(f->kind==2&&f->data)load_text_file(f);render_app();return;}}return;}if(app==APP_SETTINGS){if(inside(x,y,34,120,(int)fb_width-68,54)){theme_light^=1;save_settings();render_app();}else if(inside(x,y,34,192,(int)fb_width-68,54)){pointer_scale=pointer_scale>=4?1:pointer_scale+1;native_pointer_set_scale(pointer_scale);save_settings();render_app();}}}
static void note_key(uint8_t scan){if(scan==0x0E){if(note_cursor){for(size_t i=note_cursor-1;i<note_len;i++)note[i]=note[i+1];--note_cursor;--note_len;dirty_note=1;}return;}if(scan==0x1C){if(note_len<NOTE_MAX){for(size_t i=note_len;i>note_cursor;i--)note[i]=note[i-1];note[note_cursor++]='\n';++note_len;dirty_note=1;}return;}if(scan==0x4B){if(note_cursor)--note_cursor;return;}if(scan==0x4D){if(note_cursor<note_len)++note_cursor;return;}static const char map[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0};if(scan<128&&map[scan]&&note_len<NOTE_MAX){for(size_t i=note_len;i>note_cursor;i--)note[i]=note[i-1];note[note_cursor++]=(uint8_t)map[scan];dirty_note=1;}}
static void handle_scan(uint8_t scan){if(!scan||scan&0x80)return;if(scan==0x01){app=APP_DESKTOP;selected_file=-1;render_app();return;}if(scan==0x3B){app=APP_CALC;render_app();return;}if(scan==0x3C){app=APP_NOTEPAD;render_app();return;}if(scan==0x3D){app=APP_FILES;render_app();return;}if(scan==0x3E){app=APP_SETTINGS;render_app();return;}if(scan==0x3F){if(app==APP_NOTEPAD){save_note();render_app();}else if(app==APP_SETTINGS){save_settings();render_app();}return;}if(scan==0x40){app=APP_TASKS;render_app();return;}if(scan==0x41){app=APP_TERMINAL;render_app();return;}if(scan==0x42){app=APP_ABOUT;render_app();return;}if(app==APP_CALC){if(scan==0x0E){if(calc_len){--calc_len;calc_input[calc_len]=0;}else{calc_value=0;calc_pending=0;}render_app();return;}if(scan==0x1C){calc_equals();render_app();return;}if(scan==0x2E){calc_len=0;calc_input[0]=0;calc_value=0;calc_pending=0;render_app();return;}if(scan<128){static const char map[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0};char c=map[scan];if(c>='0'&&c<='9'&&calc_len<CALC_MAX){calc_input[calc_len++]=c;calc_input[calc_len]=0;}else if(c=='+'||c=='-'||c=='*'||c=='/')calc_apply(c);render_app();}return;}if(app==APP_NOTEPAD){note_key(scan);render_app();return;}if(app==APP_FILES){if(scan==0x48&&file_scroll>0)--file_scroll;else if(scan==0x50&&file_scroll+9<(int)boot->boot_file_count)++file_scroll;else if(scan==0x1C&&selected_file>=0){STEVEOS_BOOT_FILE*f=&boot_files[selected_file];if(f->kind==1)app=APP_IMAGE;else if(f->kind==2&&f->data)load_text_file(f);}render_app();return;}if(app==APP_IMAGE&&scan==0x1C){app=APP_FILES;render_app();return;}if(app==APP_SETTINGS){if(scan==0x4B&&pointer_scale>1)--pointer_scale;else if(scan==0x4D&&pointer_scale<4)++pointer_scale;else if(scan==0x39)theme_light^=1;native_pointer_set_scale(pointer_scale);render_app();return;}if(app==APP_TERMINAL){if(scan==0x0E&&term_len){--term_len;term_input[term_len]=0;}else if(scan==0x1C)terminal_exec();else{static const char map[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0};if(scan<128&&map[scan]&&term_len<TERM_MAX-1)term_input[term_len++]=map[scan];}render_app();return;}}
void steveos_desktop_init(STEVEOS_BOOT_INFO*b){boot=b;framebuffer=(uint32_t*)(uintptr_t)b->framebuffer_base;fb_width=(uint32_t)b->width;fb_height=(uint32_t)b->height;fb_stride=(uint32_t)b->pixels_per_scanline;boot_files=(STEVEOS_BOOT_FILE*)(uintptr_t)b->boot_files;memory_stats();load_settings();load_note();init_backbuffer();}
void steveos_desktop_run(STEVEOS_BOOT_INFO*b){(void)b;render_app();for(;;){uint8_t scan=native_keyboard_read_scancode();uint8_t buttons=native_pointer_buttons();if(buttons&&!previous_buttons)handle_click(native_pointer_x(),native_pointer_y());previous_buttons=buttons;if(scan)handle_scan(scan);__asm__ __volatile__("pause");}}
