#include <stdint.h>
#include <stddef.h>
#include "../src/bootinfo.h"
#include "desktop.h"

#define EFI_CONVENTIONAL_MEMORY 7
#define NOTE_MAX 512
#define CALC_MAX 31
#define TERM_MAX 256

enum { APP_DESKTOP, APP_CALC, APP_NOTE, APP_FILES, APP_IMAGE, APP_SETTINGS, APP_TASKS, APP_TERMINAL, APP_ABOUT };

typedef struct { uint32_t a,b,c,d; } GUID;
typedef uint64_t (__attribute__((ms_abi)) *GETVAR)(uint16_t*,GUID*,uint32_t*,uint64_t*,void*);
typedef uint64_t (__attribute__((ms_abi)) *SETVAR)(uint16_t*,GUID*,uint32_t,uint64_t,void*);
typedef struct { uint32_t magic; uint8_t light; uint8_t scale; uint16_t reserved; } SETTINGS;

extern void native_reboot(void);
extern void native_halt(void);
extern void native_pointer_set_scale(uint8_t);
extern uint8_t native_keyboard_read_scancode(void);
extern uint32_t native_pointer_x(void), native_pointer_y(void);
extern uint8_t native_pointer_buttons(void);
extern void native_pointer_hide(void), native_pointer_show(void);
extern int native_usb_mouse_present(void), native_i2c_hid_present(void);
extern const unsigned char _binary_build_boot_raw_start[], _binary_build_boot_raw_end[];

static STEVEOS_BOOT_INFO *boot_info;
static STEVEOS_BOOT_FILE *boot_files;
static uint32_t *framebuffer,*backbuffer;
static uint32_t width,height,stride;
static uint64_t total_memory,largest_region;
static int current_app=APP_DESKTOP;
static int selected_file=-1,file_scroll;
static uint8_t previous_buttons,light_theme,pointer_scale=1,note_dirty;
static uint8_t note[NOTE_MAX+1];
static size_t note_len,note_cursor;
static char calc_input[CALC_MAX+1];
static size_t calc_len;
static uint64_t calc_value;
static char calc_op;
static uint8_t calc_pending;
static char terminal_input[TERM_MAX];
static size_t terminal_len;
static char terminal_lines[8][64];
static uint8_t terminal_count;

static const GUID note_guid={0x53544556,0x4F53,0x4E56,0x00010001};
static const GUID settings_guid={0x53544556,0x4F53,0x4E56,0x00010002};
static const uint16_t note_name[]={'S','t','e','v','e','O','S','N','o','t','e',0};
static const uint16_t settings_name[]={'S','t','e','v','e','O','S','S','e','t','t','i','n','g','s',0};

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

static uint32_t bg_color(void){return light_theme?0xF2F5F8u:0x0B1220u;}
static uint32_t panel_color(void){return light_theme?0xFFFFFFu:0x151F2Fu;}
static uint32_t panel2_color(void){return light_theme?0xE2E8F0u:0x1D2A3Eu;}
static uint32_t text_color(void){return light_theme?0x172033u:0xF4F7FBu;}
static uint32_t sub_color(void){return light_theme?0x5D6B7Eu:0xA8B5C7u;}
static uint32_t good_color(void){return light_theme?0x4D8B68u:0x8BC6A3u;}

static void fill_rect(int x,int y,int w,int h,uint32_t c){
    if(!backbuffer||w<=0||h<=0)return;
    int x0=x<0?0:x,y0=y<0?0:y,x1=x+w,y1=y+h;
    if(x1>(int)width)x1=(int)width;if(y1>(int)height)y1=(int)height;
    for(int yy=y0;yy<y1;yy++){
        uint32_t*p=backbuffer+(size_t)yy*stride+x0;
        for(int xx=x0;xx<x1;xx++)*p++=c;
    }
}
static void put_pixel(int x,int y,uint32_t c){if(backbuffer&&x>=0&&y>=0&&(uint32_t)x<width&&(uint32_t)y<height)backbuffer[(size_t)y*stride+(size_t)x]=c;}
static int hit(uint32_t x,uint32_t y,int a,int b,int w,int h){return x>=(uint32_t)a&&x<(uint32_t)(a+w)&&y>=(uint32_t)b&&y<(uint32_t)(b+h);}
static const uint8_t*glyph_data(char c){if(c>='A'&&c<='Z')return letters[c-'A'];if(c>='a'&&c<='z')return letters[c-'a'];if(c>='0'&&c<='9')return digits[c-'0'];return NULL;}
static void glyph(int x,int y,char c,uint32_t col,int s){
    const uint8_t*g=glyph_data(c);
    if(g){for(int a=0;a<5;a++)for(int b=0;b<7;b++)if(g[a]&(1u<<b))fill_rect(x+a*s,y+b*s,s,s,col);return;}
    if(c=='+'){fill_rect(x+2*s,y,s,7*s,col);fill_rect(x,y+3*s,5*s,s,col);}
    else if(c=='-')fill_rect(x,y+3*s,5*s,s,col);
    else if(c=='='){fill_rect(x,y+2*s,5*s,s,col);fill_rect(x,y+5*s,5*s,s,col);}
    else if(c=='*'){fill_rect(x+2*s,y,s,7*s,col);fill_rect(x,y+2*s,5*s,s,col);}
    else if(c=='/')for(int i=0;i<7;i++)fill_rect(x+(6-i)*s,y+i*s,s,s,col);
    else if(c==':'){fill_rect(x,y+s,s,s,col);fill_rect(x,y+5*s,s,s,col);}
    else if(c=='.')fill_rect(x,y+6*s,s,s,col);
    else if(c=='!'){fill_rect(x+2*s,y,s,5*s,col);fill_rect(x+2*s,y+6*s,s,s,col);}
}
static void text(int x,int y,const char*s,uint32_t col,int sc){while(*s){if(*s==' ')x+=6*sc;else{glyph(x,y,*s,col,sc);x+=6*sc;}s++;}}
static void number(int x,int y,uint64_t v,uint32_t col,int sc){char b[32];int n=0;if(!v)b[n++]='0';while(v&&n<31){b[n++]=(char)('0'+v%10);v/=10;}while(n){glyph(x,y,b[--n],col,sc);x+=6*sc;}}

static void present(void){
    if(!framebuffer||!backbuffer)return;
    native_pointer_hide();
    size_t n=(size_t)stride*height;
    for(size_t i=0;i<n;i++)framebuffer[i]=backbuffer[i];
    native_pointer_show();
}
static int init_backbuffer(void){
    uint64_t bytes=(uint64_t)stride*height*4ULL;
    if(!boot_info->memory_map||!boot_info->memory_descriptor_size){backbuffer=framebuffer;return 1;}
    uint8_t*p=(uint8_t*)(uintptr_t)boot_info->memory_map,*e=p+boot_info->memory_map_size;uint64_t best_end=0;
    while(p+boot_info->memory_descriptor_size<=e){
        uint32_t type=*(uint32_t*)p;uint64_t*q=(uint64_t*)(p+8),start=q[0],size=q[1]*4096ULL;
        if(type==EFI_CONVENTIONAL_MEMORY&&size>=bytes+0x100000ULL&&start+size>best_end)best_end=start+size;
        p+=boot_info->memory_descriptor_size;
    }
    if(!best_end){backbuffer=framebuffer;return 1;}
    backbuffer=(uint32_t*)(uintptr_t)((best_end-bytes)&~0xFFFULL);
    for(uint64_t i=0;i<(uint64_t)stride*height;i++)backbuffer[i]=bg_color();
    return 1;
}
static void memory_stats(void){
    total_memory=largest_region=0;
    uint8_t*p=(uint8_t*)(uintptr_t)boot_info->memory_map,*e=p+boot_info->memory_map_size;
    while(p+boot_info->memory_descriptor_size<=e){
        uint32_t type=*(uint32_t*)p;uint64_t*q=(uint64_t*)(p+8);
        if(type==EFI_CONVENTIONAL_MEMORY){uint64_t s=q[1]*4096ULL;total_memory+=s;if(s>largest_region)largest_region=s;}
        p+=boot_info->memory_descriptor_size;
    }
}
static void load_settings(void){
    light_theme=0;pointer_scale=1;
    if(boot_info->uefi_get_variable){
        GETVAR get=(GETVAR)(uintptr_t)boot_info->uefi_get_variable;
        SETTINGS s={0x53545653u,0,1,0};uint32_t a=0;uint64_t z=sizeof(s);
        if(get((uint16_t*)settings_name,(GUID*)&settings_guid,&a,&z,&s)==0&&s.magic==0x53545653u){light_theme=s.light?1:0;pointer_scale=s.scale<1?1:(s.scale>4?4:s.scale);}
    }
    native_pointer_set_scale(pointer_scale);
}
static void save_settings(void){
    if(!boot_info->uefi_set_variable)return;
    SETVAR set=(SETVAR)(uintptr_t)boot_info->uefi_set_variable;SETTINGS s={0x53545653u,light_theme,pointer_scale,0};
    set((uint16_t*)settings_name,(GUID*)&settings_guid,7,sizeof(s),&s);
}
static void load_note(void){
    note_len=note_cursor=0;note_dirty=0;
    if(!boot_info->uefi_get_variable)return;
    GETVAR get=(GETVAR)(uintptr_t)boot_info->uefi_get_variable;uint32_t a=0;uint64_t z=NOTE_MAX;
    if(get((uint16_t*)note_name,(GUID*)&note_guid,&a,&z,note)==0){if(z>NOTE_MAX)z=NOTE_MAX;note_len=(size_t)z;note_cursor=note_len;}note[note_len]=0;
}
static void save_note(void){
    if(!boot_info->uefi_set_variable)return;
    SETVAR set=(SETVAR)(uintptr_t)boot_info->uefi_set_variable;set((uint16_t*)note_name,(GUID*)&note_guid,7,note_len,note);note_dirty=0;
}
static void file_name(const STEVEOS_BOOT_FILE*f,char*out,size_t cap){size_t i=0;if(!cap)return;while(f&&i+1<cap&&i<STEVEOS_BOOT_FILE_NAME_MAX&&f->name[i]){uint16_t c=f->name[i++];out[i-1]=c<128?(char)c:'?';}out[i]=0;}
static void load_text_file(STEVEOS_BOOT_FILE*f){
    if(!f||!f->data||!f->size)return;size_t n=(size_t)f->size;if(n>NOTE_MAX)n=NOTE_MAX;const uint8_t*d=(const uint8_t*)(uintptr_t)f->data;
    for(size_t i=0;i<n;i++)note[i]=(d[i]>=32||d[i]=='\n'||d[i]=='\t')?d[i]:' ';
    note[n]=0;note_len=n;note_cursor=n;note_dirty=0;current_app=APP_NOTE;
}
static void topbar(const char*t){fill_rect(0,0,(int)width,56,light_theme?0xDDE4ECu:0x121B29u);text(24,18,"STEVEOS",text_color(),2);text(145,22,t,sub_color(),1);text((int)width-180,22,native_usb_mouse_present()?"USB MOUSE":"NATIVE INPUT",sub_color(),1);}
static void draw_desktop(void){
    fill_rect(0,0,(int)width,(int)height,bg_color());topbar("DESKTOP");text(38,82,"WELCOME TO STEVEOS",text_color(),3);text(40,114,"NATIVE KERNEL DESKTOP",sub_color(),1);
    int cw=width>=900?250:190,ch=78,g=14,x0=34,y0=150;const char*names[]={"CALCULATOR","NOTEPAD","FILES","SETTINGS","TASK MANAGER","TERMINAL","IMAGE VIEWER","ABOUT","SYSTEM"};const char*hints[]={"CALC","EDITOR","BROWSE","OPTIONS","PROCESSES","SHELL","PICTURES","CREDITS","HARDWARE"};
    for(int i=0;i<9;i++){int col=i%3,row=i/3,x=x0+col*(cw+g),y=y0+row*(ch+g);fill_rect(x+3,y+4,cw,ch,0x050A12u);fill_rect(x,y,cw,ch,panel_color());fill_rect(x+12,y+11,40,40,panel2_color());fill_rect(x+23,y+19,18,24,0x5F7593u);text(x+62,y+17,names[i],text_color(),1);text(x+62,y+38,hints[i],sub_color(),1);}
    fill_rect(34,(int)height-76,(int)width-68,44,panel2_color());text(50,(int)height-61,"F1 CALC F2 NOTE F3 FILES F4 SETTINGS F6 TASKS F7 TERMINAL",sub_color(),1);text(50,(int)height-41,"F8 ABOUT ESC DESKTOP CLICK",sub_color(),1);
}
static uint64_t calc_input_value(void){uint64_t v=0;for(size_t i=0;i<calc_len;i++)v=v*10+(uint64_t)(calc_input[i]-'0');return v;}
static void calc_apply_operator(char op){uint64_t b=calc_input_value();if(!calc_pending)calc_value=b;else if(calc_op=='+')calc_value+=b;else if(calc_op=='-')calc_value=calc_value>b?calc_value-b:0;else if(calc_op=='*')calc_value*=b;else if(calc_op=='/'&&b)calc_value/=b;calc_op=op;calc_pending=1;calc_len=0;calc_input[0]=0;}
static void calc_equals(void){uint64_t b=calc_input_value();if(!calc_pending)calc_value=b;else if(calc_op=='+')calc_value+=b;else if(calc_op=='-')calc_value=calc_value>b?calc_value-b:0;else if(calc_op=='*')calc_value*=b;else if(calc_op=='/'&&b)calc_value/=b;calc_len=0;calc_input[0]=0;calc_pending=0;}
static void calc_clear(void){calc_len=0;calc_input[0]=0;calc_value=0;calc_pending=0;calc_op=0;}
static void draw_calc(void){
    fill_rect(0,0,(int)width,(int)height,bg_color());topbar("CALCULATOR");fill_rect(38,88,(int)width-76,82,panel_color());if(calc_len)text(58,116,calc_input,text_color(),3);else number(58,116,calc_value,text_color(),3);
    const char*keys[]={"7","8","9","/","4","5","6","*","1","2","3","-","0","C","=","+"};int bw=100,bh=54,g=12,x0=38,y0=195;
    for(int i=0;i<16;i++){int x=x0+(i%4)*(bw+g),y=y0+(i/4)*(bh+g);fill_rect(x,y,bw,bh,panel_color());text(x+43,y+18,keys[i],text_color(),2);}text(38,490,"DIGITS + - * / ENTER = BACKSPACE C CLEAR",sub_color(),1);
}
static void draw_note(void){
    fill_rect(0,0,(int)width,(int)height,bg_color());topbar("NOTEPAD");fill_rect(34,78,(int)width-68,(int)height-150,light_theme?0xFBFCFEu:0x182231u);int x=50,y=98;
    for(size_t i=0;i<note_len&&y<(int)height-105;i++){char c=(char)note[i];if(c=='\n'){x=50;y+=16;continue;}glyph(x,y,c,light_theme?0x18202Bu:0xEAF0F6u,1);x+=6;if(x>(int)width-55){x=50;y+=16;}}
    fill_rect(34,(int)height-62,(int)width-68,36,panel2_color());text(50,(int)height-49,"F5 SAVE",text_color(),1);text(115,(int)height-49,note_dirty?"UNSAVED":"SAVED",note_dirty?0xD8A654u:good_color(),1);text(220,(int)height-49,"ENTER NEW LINE BACKSPACE DELETE ARROWS",sub_color(),1);
}
static uint32_t read32le(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);} static uint16_t read16le(const uint8_t*p){return(uint16_t)p[0]|((uint16_t)p[1]<<8);}
static void draw_builtin_image(void){const uint8_t*r=_binary_build_boot_raw_start,*e=_binary_build_boot_raw_end;if((size_t)(e-r)<8)return;uint32_t w=*(const uint32_t*)r,h=*(const uint32_t*)(r+4);const uint8_t*p=r+8;if(!w||!h||(size_t)w*h*4+8>(size_t)(e-r))return;uint32_t dw=520,dh=(uint64_t)h*dw/w;if(dh>470){dh=470;dw=(uint64_t)w*dh/h;}int ox=((int)width-(int)dw)/2,oy=92;for(uint32_t y=0;y<dh;y++){uint32_t sy=(uint64_t)y*h/dh;for(uint32_t x=0;x<dw;x++){uint32_t sx=(uint64_t)x*w/dw;const uint8_t*v=p+((size_t)sy*w+sx)*4;put_pixel(ox+(int)x,oy+(int)y,0xFF000000u|(uint32_t)v[2]|((uint32_t)v[1]<<8)|((uint32_t)v[0]<<16));}}}
static void draw_bmp(const uint8_t*d,size_t len){if(len<54||d[0]!='B'||d[1]!='M')return;uint32_t off=read32le(d+10),w=read32le(d+18),hraw=read32le(d+22);int32_t h=(int32_t)hraw;uint16_t planes=read16le(d+26),bpp=read16le(d+28);if(!w||!h||planes!=1||(bpp!=24&&bpp!=32))return;uint32_t ah=(uint32_t)(h<0?-h:h),row=((w*bpp+31)/32)*4;if((uint64_t)off+(uint64_t)row*ah>len)return;uint32_t dw=520,dh=(uint64_t)ah*dw/w;if(dh>470){dh=470;dw=(uint64_t)w*dh/ah;}int ox=((int)width-(int)dw)/2,oy=92,bytes=bpp/8;for(uint32_t y=0;y<dh;y++){uint32_t sy=(uint64_t)y*ah/dh;if(h>0)sy=ah-1-sy;for(uint32_t x=0;x<dw;x++){uint32_t sx=(uint64_t)x*w/dw;const uint8_t*v=d+off+(uint64_t)sy*row+(uint64_t)sx*bytes;put_pixel(ox+(int)x,oy+(int)y,0xFF000000u|(uint32_t)v[2]|((uint32_t)v[1]<<8)|((uint32_t)v[0]<<16));}}}
static void draw_image(void){fill_rect(0,0,(int)width,(int)height,bg_color());topbar("IMAGE VIEWER");if(selected_file>=0&&boot_files&&(uint64_t)selected_file<boot_info->boot_file_count){STEVEOS_BOOT_FILE*f=&boot_files[selected_file];char name[64];file_name(f,name,sizeof(name));text(30,74,name,sub_color(),1);if(f->data&&f->size){const uint8_t*d=(const uint8_t*)(uintptr_t)f->data;if(f->size>=2&&d[0]=='B'&&d[1]=='M')draw_bmp(d,(size_t)f->size);else draw_builtin_image();}else draw_builtin_image();}else draw_builtin_image();text(30,(int)height-44,"ESC BACK TO FILES BMP + BUILT-IN IMAGE",sub_color(),1);}
static void draw_files(void){fill_rect(0,0,(int)width,(int)height,bg_color());topbar("FILE EXPLORER");fill_rect(30,76,(int)width-60,38,panel2_color());text(46,89,"COMPUTER / BOOT VOLUME",text_color(),1);int shown=0;for(uint64_t i=(uint64_t)file_scroll;i<boot_info->boot_file_count&&shown<9;i++,shown++){STEVEOS_BOOT_FILE*f=&boot_files[i];char name[64];file_name(f,name,sizeof(name));int y=130+shown*45;fill_rect(30,y,(int)width-60,36,selected_file==(int)i?0x4F6582u:panel_color());text(46,y+11,f->attributes&0x10?"DIR":(f->kind==1?"IMG":(f->kind==2?"TXT":"FILE")),text_color(),1);text(92,y+11,name,text_color(),1);number(540,y+11,f->size,sub_color(),1);}text(34,(int)height-48,"UP DOWN SCROLL ENTER OPEN CLICK FILE ESC",sub_color(),1);}
static void draw_settings(void){fill_rect(0,0,(int)width,(int)height,bg_color());topbar("SETTINGS");text(38,84,"APPEARANCE",text_color(),2);fill_rect(34,120,(int)width-68,54,panel_color());text(52,137,"THEME",text_color(),1);text(330,137,light_theme?"LIGHT":"DARK",sub_color(),1);fill_rect(34,192,(int)width-68,54,panel_color());text(52,209,"POINTER SCALE",text_color(),1);number(330,209,pointer_scale,text_color(),1);text(52,223,"LEFT RIGHT TO CHANGE",sub_color(),1);fill_rect(34,264,(int)width-68,96,panel_color());text(52,281,"INPUT DEVICES",text_color(),1);text(52,304,native_usb_mouse_present()?"USB HID MOUSE ACTIVE":"USB HID MOUSE OFF",sub_color(),1);text(52,324,native_i2c_hid_present()?"I2C HID TOUCHPAD ACTIVE":"I2C HID TOUCHPAD OFF",sub_color(),1);text(52,405,"F5 SAVE SETTINGS TO NVRAM",text_color(),1);}
static void draw_tasks(void){fill_rect(0,0,(int)width,(int)height,bg_color());topbar("TASK MANAGER");text(34,82,"STEVEOS COMPONENTS",text_color(),2);const char*names[]={"KERNEL MAIN LOOP","FRAMEBUFFER","PS2 KEYBOARD","USB HID MOUSE","I2C HID TOUCHPAD","NVRAM","BOOT FILE SNAPSHOT"};for(int i=0;i<7;i++){int y=122+i*44;int active=i<3||(i==3&&native_usb_mouse_present())||(i==4&&native_i2c_hid_present())||i>4;fill_rect(30,y,(int)width-60,34,panel_color());text(46,y+11,names[i],text_color(),1);text((int)width-140,y+11,active?"ACTIVE":"OFF",active?good_color():sub_color(),1);}fill_rect(30,444,(int)width-60,76,panel2_color());text(46,460,"MEMORY",sub_color(),1);number(46,482,total_memory/1024/1024,text_color(),2);text(106,485,"MB",sub_color(),1);text(250,460,"LARGEST REGION",sub_color(),1);number(250,482,largest_region/1024/1024,text_color(),2);text(346,485,"MB",sub_color(),1);}
static void terminal_add(const char*s){if(terminal_count<8){size_t i=0;while(s[i]&&i<63){terminal_lines[terminal_count][i]=s[i];i++;}terminal_lines[terminal_count][i]=0;terminal_count++;}else{for(int r=1;r<8;r++)for(int c=0;c<64;c++)terminal_lines[r-1][c]=terminal_lines[r][c];size_t i=0;while(s[i]&&i<63){terminal_lines[7][i]=s[i];i++;}terminal_lines[7][i]=0;}}
static int str_eq(const char*a,const char*b){while(*a&&*b&&*a==*b){a++;b++;}return *a==0&&*b==0;}
static void terminal_exec(void){terminal_input[terminal_len]=0;if(str_eq(terminal_input,"HELP"))terminal_add("HELP MEM APPS CLEAR REBOOT HALT");else if(str_eq(terminal_input,"MEM"))terminal_add("OPEN TASK MANAGER FOR MEMORY");else if(str_eq(terminal_input,"APPS"))terminal_add("F1 F2 F3 F4 F6 F7 F8");else if(str_eq(terminal_input,"CLEAR"))terminal_count=0;else if(str_eq(terminal_input,"REBOOT"))native_reboot();else if(str_eq(terminal_input,"HALT"))native_halt();else if(terminal_len)terminal_add("UNKNOWN COMMAND");terminal_len=0;terminal_input[0]=0;}
static void draw_terminal(void){fill_rect(0,0,(int)width,(int)height,bg_color());topbar("TERMINAL");for(int i=0;i<8;i++)text(32,82+i*24,terminal_lines[i],text_color(),1);fill_rect(28,(int)height-62,(int)width-56,38,panel2_color());text(40,(int)height-50,terminal_input,text_color(),1);}
static void draw_about(void){fill_rect(0,0,(int)width,(int)height,bg_color());topbar("ABOUT");text(38,92,"STEVEOS",text_color(),3);text(40,132,"NATIVE KERNEL DESKTOP",sub_color(),1);text(40,174,"UEFI GOP FRAMEBUFFER",text_color(),1);text(40,202,"X86-64 GDT IDT PAGING",sub_color(),1);text(40,226,"PS2 USB HID I2C HID INPUT",sub_color(),1);text(40,250,"PERSISTENT NVRAM NOTES SETTINGS",sub_color(),1);fill_rect(36,300,(int)width-72,130,panel_color());text(54,322,"MINT-Y SOURCE",text_color(),1);text(54,344,"linuxmint/mint-y-icons",sub_color(),1);text(54,366,"OPEN SOURCE ASSET REFERENCE",sub_color(),1);text(54,388,"SEE THIRD_PARTY.MD",sub_color(),1);}
static void render(void){switch(current_app){case APP_DESKTOP:draw_desktop();break;case APP_CALC:draw_calc();break;case APP_NOTE:draw_note();break;case APP_FILES:draw_files();break;case APP_IMAGE:draw_image();break;case APP_SETTINGS:draw_settings();break;case APP_TASKS:draw_tasks();break;case APP_TERMINAL:draw_terminal();break;default:draw_about();break;}present();}
static void open_card(uint32_t x,uint32_t y){int cw=width>=900?250:190,ch=78,g=14,x0=34,y0=150;for(int i=0;i<9;i++){int col=i%3,row=i/3;if(hit(x,y,x0+col*(cw+g),y0+row*(ch+g),cw,ch)){if(i==0)current_app=APP_CALC;else if(i==1)current_app=APP_NOTE;else if(i==2)current_app=APP_FILES;else if(i==3)current_app=APP_SETTINGS;else if(i==4||i==8)current_app=APP_TASKS;else if(i==5)current_app=APP_TERMINAL;else if(i==6)current_app=APP_IMAGE;else current_app=APP_ABOUT;selected_file=-1;render();return;}}}
static void handle_click(uint32_t x,uint32_t y){
    if(current_app==APP_DESKTOP){open_card(x,y);return;}
    if(current_app==APP_CALC){int bw=100,bh=54,g=12,x0=38,y0=195;const char*k="789/456*123-0C=+";for(int i=0;i<16;i++){int bx=x0+(i%4)*(bw+g),by=y0+(i/4)*(bh+g);if(hit(x,y,bx,by,bw,bh)){char q=k[i];if(q>='0'&&q<='9'&&calc_len<CALC_MAX){calc_input[calc_len++]=q;calc_input[calc_len]=0;}else if(q=='C')calc_clear();else if(q=='=')calc_equals();else calc_apply_operator(q);render();return;}}return;}
    if(current_app==APP_FILES){for(int row=0;row<9;row++){uint64_t i=(uint64_t)file_scroll+row;if(i>=boot_info->boot_file_count)break;if(hit(x,y,30,130+row*45,(int)width-60,36)){selected_file=(int)i;STEVEOS_BOOT_FILE*f=&boot_files[i];if(f->kind==1)current_app=APP_IMAGE;else if(f->kind==2&&f->data)load_text_file(f);render();return;}}return;}
    if(current_app==APP_SETTINGS){if(hit(x,y,34,120,(int)width-68,54)){light_theme^=1;}else if(hit(x,y,34,192,(int)width-68,54)){pointer_scale=pointer_scale>=4?1:pointer_scale+1;native_pointer_set_scale(pointer_scale);}render();}
}
static void note_key(uint8_t s){if(s==0x0e){if(note_cursor){for(size_t i=note_cursor-1;i<note_len;i++)note[i]=note[i+1];note_cursor--;note_len--;note_dirty=1;}return;}if(s==0x1c){if(note_len<NOTE_MAX){for(size_t i=note_len;i>note_cursor;i--)note[i]=note[i-1];note[note_cursor++]='\n';note_len++;note_dirty=1;}return;}if(s==0x4b){if(note_cursor)note_cursor--;return;}if(s==0x4d){if(note_cursor<note_len)note_cursor++;return;}static const char map[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0};if(s<128&&map[s]&&note_len<NOTE_MAX){for(size_t i=note_len;i>note_cursor;i--)note[i]=note[i-1];note[note_cursor++]=(uint8_t)map[s];note_dirty=1;}}
static void handle_scan(uint8_t s){
    if(!s||s&0x80)return;if(s==1){current_app=APP_DESKTOP;selected_file=-1;render();return;}if(s==0x3b){current_app=APP_CALC;render();return;}if(s==0x3c){current_app=APP_NOTE;render();return;}if(s==0x3d){current_app=APP_FILES;render();return;}if(s==0x3e){current_app=APP_SETTINGS;render();return;}if(s==0x3f){if(current_app==APP_NOTE)save_note();else if(current_app==APP_SETTINGS)save_settings();render();return;}if(s==0x40){current_app=APP_TASKS;render();return;}if(s==0x41){current_app=APP_TERMINAL;render();return;}if(s==0x42){current_app=APP_ABOUT;render();return;}
    if(current_app==APP_CALC){static const char map[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0};if(s==0x0e){if(calc_len){calc_len--;calc_input[calc_len]=0;}else calc_clear();}else if(s==0x1c)calc_equals();else if(s==0x2e)calc_clear();else if(s<128){char q=map[s];if(q>='0'&&q<='9'&&calc_len<CALC_MAX){calc_input[calc_len++]=q;calc_input[calc_len]=0;}else if(q=='+'||q=='-'||q=='*'||q=='/')calc_apply_operator(q);}render();return;}
    if(current_app==APP_NOTE){note_key(s);render();return;}
    if(current_app==APP_FILES){if(s==0x48&&file_scroll>0)file_scroll--;else if(s==0x50&&file_scroll+9<(int)boot_info->boot_file_count)file_scroll++;else if(s==0x1c&&selected_file>=0){STEVEOS_BOOT_FILE*f=&boot_files[selected_file];if(f->kind==1)current_app=APP_IMAGE;else if(f->kind==2&&f->data)load_text_file(f);}render();return;}
    if(current_app==APP_IMAGE&&s==0x1c){current_app=APP_FILES;render();return;}
    if(current_app==APP_SETTINGS){if(s==0x4b&&pointer_scale>1)pointer_scale--;else if(s==0x4d&&pointer_scale<4)pointer_scale++;else if(s==0x39)light_theme^=1;native_pointer_set_scale(pointer_scale);render();return;}
    if(current_app==APP_TERMINAL){static const char map[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0};if(s==0x0e&&terminal_len){terminal_len--;terminal_input[terminal_len]=0;}else if(s==0x1c)terminal_exec();else if(s<128&&map[s]&&terminal_len<TERM_MAX-1)terminal_input[terminal_len++]=map[s];render();return;}
}
void steveos_desktop_init(STEVEOS_BOOT_INFO*b){boot_info=b;boot_files=(STEVEOS_BOOT_FILE*)(uintptr_t)b->boot_files;framebuffer=(uint32_t*)(uintptr_t)b->framebuffer_base;width=(uint32_t)b->width;height=(uint32_t)b->height;stride=(uint32_t)b->pixels_per_scanline;memory_stats();load_settings();load_note();init_backbuffer();}
void steveos_desktop_run(STEVEOS_BOOT_INFO*b){(void)b;render();for(;;){uint8_t s=native_keyboard_read_scancode();uint8_t btn=native_pointer_buttons();if(btn&&!previous_buttons)handle_click(native_pointer_x(),native_pointer_y());previous_buttons=btn;if(s)handle_scan(s);__asm__ __volatile__("pause");}}
