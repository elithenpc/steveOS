#include <stdint.h>
#include <stddef.h>
#include "../src/bootinfo.h"
#include "desktop.h"

#define NOTE_MAX 768
#define CALC_MAX 191
#define TERM_MAX 191
#define TERM_LINES 12
#define BROWSER_RAW_MAX 32768
#define BROWSER_TEXT_MAX 16384
#define BROWSER_URL_MAX 191
#define BROWSER_LINKS 8
#define BROWSER_LINK_MAX 159
#define EFI_CONVENTIONAL_MEMORY 7

enum {
    APP_DESKTOP, APP_BROWSER, APP_CALC, APP_EDITOR, APP_FILES, APP_IMAGE,
    APP_SETTINGS, APP_TASKS, APP_TERMINAL, APP_CALENDAR, APP_CONTROL,
    APP_ABOUT, APP_SYSINFO, APP_DEVICES, APP_INSTALLER, APP_STORE,
    APP_SERVER, APP_ADVANCED
};

typedef struct { uint32_t a,b,c,d; } GUID;
typedef uint64_t (__attribute__((ms_abi)) *GETVAR)(uint16_t*,GUID*,uint32_t*,uint64_t*,void*);
typedef uint64_t (__attribute__((ms_abi)) *SETVAR)(uint16_t*,GUID*,uint32_t,uint64_t,void*);
typedef uint64_t (__attribute__((ms_abi)) *GETTIME)(void*,void*);
typedef uint64_t (__attribute__((ms_abi)) *HTTPGET)(const uint16_t*,char*,uint64_t,uint64_t*,uint32_t*);
typedef uint64_t (__attribute__((ms_abi)) *WRITEFILE)(const uint16_t*,const void*,uint64_t);
typedef uint64_t (__attribute__((ms_abi)) *LISTTARGETS)(STEVEOS_INSTALL_TARGET*,uint64_t);
typedef uint64_t (__attribute__((ms_abi)) *INSTALLSELF)(uint64_t);
typedef uint64_t (__attribute__((ms_abi)) *INSTALLSERVER)(uint64_t);
typedef uint64_t (__attribute__((ms_abi)) *LAUNCHSERVER)(void);
typedef uint64_t (__attribute__((ms_abi)) *INSTALLAPP)(const uint16_t*);
typedef uint64_t (__attribute__((ms_abi)) *DOWNLOADAPP)(const uint16_t*,const uint16_t*);
typedef uint64_t (__attribute__((ms_abi)) *LAUNCHAPP)(const uint16_t*);
typedef uint64_t (__attribute__((ms_abi)) *INSTALLWINDOWSAPP)(const uint16_t*);
typedef uint64_t (__attribute__((ms_abi)) *RUNWINDOWSAPP)(const uint16_t*);
typedef uint64_t (__attribute__((ms_abi)) *UPDATECHECK)(STEVEOS_UPDATE_INFO*);
typedef uint64_t (__attribute__((ms_abi)) *UPDATEAPPLY)(void);
typedef uint64_t (__attribute__((ms_abi)) *NETINFO)(STEVEOS_NETWORK_INFO*);
typedef struct { uint32_t magic; uint8_t light; uint8_t scale; uint8_t accent; uint8_t service_flags; uint8_t logging; uint8_t boot_delay; uint8_t reserved0; uint32_t reserved; } SETTINGS;

typedef struct {
    uint16_t year;
    uint8_t month,day,hour,minute,second,pad1;
    uint32_t nanosecond;
    int16_t timezone;
    uint8_t daylight,pad2;
} EFI_TIME_STEV;

extern void native_reboot(void);
extern void native_halt(void);
extern void native_pointer_set_scale(uint8_t);
extern uint8_t native_keyboard_read_scancode(void);
extern uint32_t native_pointer_x(void), native_pointer_y(void);
extern uint8_t native_pointer_buttons(void);
extern void native_pointer_hide(void), native_pointer_show(void);
extern int native_usb_mouse_present(void), native_i2c_hid_present(void);
extern const unsigned char _binary_build_boot_raw_start[], _binary_build_boot_raw_end[];
extern const unsigned char _binary_build_mint_icons_raw_start[], _binary_build_mint_icons_raw_end[];

static STEVEOS_BOOT_INFO *boot_info;
static STEVEOS_BOOT_FILE *boot_files;
static uint32_t *framebuffer,*backbuffer;
static uint32_t width,height,stride;
static uint64_t total_memory,largest_region;
static int current_app=APP_DESKTOP,previous_app=APP_DESKTOP;
static uint8_t pci_device_count;
static STEVEOS_INSTALL_TARGET install_targets[16];
static uint64_t install_target_count;
static int install_target_pick=-1;
static uint8_t install_armed;
static int app_package_indices[24];
static int app_package_count,app_package_pick=-1;
static uint8_t app_install_done,app_download_focus;
static char app_download_url[BROWSER_URL_MAX+1];
static STEVEOS_NETWORK_INFO network_info;
static uint8_t network_info_valid;
static STEVEOS_UPDATE_INFO update_info;
static uint8_t update_checked;
static uint8_t service_flags,logging_level,boot_delay,net_test_state,server_install_state;
typedef struct {uint8_t bus,dev,fn,class_code,subclass;uint16_t vendor,device;} PCI_VIEW;
static PCI_VIEW pci_devices[24];
static int selected_file=-1,file_scroll,file_filter;
static uint8_t previous_buttons,light_theme,pointer_scale=1,accent_id,note_dirty;
static uint8_t menu_open,power_menu,menu_search_len;
static char menu_search[64];
static uint8_t note[NOTE_MAX+1];
static size_t note_len,note_cursor;
static uint16_t editor_target_path[128];
static char calc_input[CALC_MAX+1];
static size_t calc_len;
static int64_t calc_result;
static uint8_t calc_has_result;
static char terminal_input[TERM_MAX+1];
static size_t terminal_len;
static char terminal_lines[TERM_LINES][64];
static uint8_t terminal_count;
static char browser_url[BROWSER_URL_MAX+1]="http://neverssl.com/";
static char browser_raw[BROWSER_RAW_MAX];
static size_t browser_raw_len;
static char browser_image_raw[131072];
static size_t browser_image_len;
static uint8_t browser_image_loaded;
static char browser_image_url[BROWSER_URL_MAX+1];
static char browser_image_alt[64];
static char browser_text[BROWSER_TEXT_MAX];
static char browser_title[96];
static char browser_link_urls[BROWSER_LINKS][BROWSER_LINK_MAX+1];
static char browser_link_text[BROWSER_LINKS][48];
static uint8_t browser_link_count;
static int browser_scroll;
static uint32_t browser_status;
static uint8_t browser_loaded;
static uint8_t browser_focus;
static uint8_t browser_history_count;
static uint8_t browser_history_pos;
static uint8_t browser_history_lock;
static char browser_history[8][BROWSER_URL_MAX+1];
static uint8_t browser_tab_count=1,browser_current_tab;
static char browser_tabs[4][BROWSER_URL_MAX+1];
static uint8_t browser_bookmark_count;
static char browser_bookmarks[8][BROWSER_URL_MAX+1];
static uint8_t ctrl_down,alt_down;
static uint8_t shift_down;
static uint8_t dirty=1;

static void browser_copy_url(char*out,const char*in);
static void browser_load_local_file(const STEVEOS_BOOT_FILE*f);
static int browser_fetch(void);
static void terminal_add(const char*s);

static const GUID note_guid={0x53544556,0x4F53,0x4E56,0x00010001};
static const GUID settings_guid={0x53544556,0x4F53,0x4E56,0x00010002};
static const GUID bookmarks_guid={0x53544556,0x4F53,0x4E56,0x00010003};
static const uint16_t note_name[]={'S','t','e','v','e','O','S','N','o','t','e',0};
static const uint16_t settings_name[]={'S','t','e','v','e','O','S','S','e','t','t','i','n','g','s',0};
static const uint16_t bookmarks_name[]={'S','t','e','v','e','O','S','B','o','o','k','m','a','r','k','s',0};
static const uint16_t note_file_name[]={L'\\',L'S',L't',L'e',L'v',L'e',L'O',L'S',L'N',L'o',L't',L'e',L'.',L't',L'x',L't',0};
static const uint16_t page_file_name[]={L'\\',L'S',L't',L'e',L'v',L'e',L'O',L'S',L'P',L'a',L'g',L'e',L'.',L'h',L't',L'm',L'l',0};

static const uint8_t letters[26][5]={
{0x3E,0x09,0x09,0x09,0x3E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
{0x41,0x41,0x7F,0x41,0x41},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}};
static const uint8_t digits[10][5]={
{0x3E,0x45,0x49,0x51,0x3E},{0x00,0x21,0x7F,0x01,0x00},{0x23,0x45,0x49,0x49,0x31},{0x22,0x41,0x49,0x49,0x36},
{0x0C,0x14,0x24,0x7F,0x04},{0x7A,0x49,0x49,0x49,0x46},{0x3E,0x49,0x49,0x49,0x26},{0x40,0x47,0x48,0x50,0x60},
{0x36,0x49,0x49,0x49,0x36},{0x32,0x49,0x49,0x49,0x3E}};

static uint32_t bg_color(void){return light_theme?0xF1F4F6u:0x0B1016u;}
static uint32_t panel_color(void){return light_theme?0xFFFFFFu:0x171E27u;}
static uint32_t panel2_color(void){return light_theme?0xE2E8ECu:0x202A36u;}
static uint32_t text_color(void){return light_theme?0x17202Au:0xF2F6F8u;}
static uint32_t sub_color(void){return light_theme?0x5D6B75u:0xA5B2BCu;}
static uint32_t accent_color(void){
    static const uint32_t light[]={0x5FAF3Fu,0x3984D4u,0x8A63C7u,0xD8873Cu};
    static const uint32_t dark[]={0x8BCF4Fu,0x68A7E8u,0xB18CE8u,0xF0A35Fu};
    return (light_theme?light:dark)[accent_id&3u];
}
static uint32_t accent_dark(void){
    static const uint32_t light[]={0x4B8E31u,0x2D69A9u,0x704BA7u,0xAD6828u};
    static const uint32_t dark[]={0x5A972Du,0x3F79B2u,0x805AB3u,0xB9793Au};
    return (light_theme?light:dark)[accent_id&3u];
}
static uint32_t danger_color(void){return light_theme?0xB64A4Au:0xE27F80u;}
static uint32_t good_color(void){return light_theme?0x3E8A50u:0x88CF98u;}

static void mark_dirty(void){dirty=1;}
static void fill_rect(int x,int y,int w,int h,uint32_t c){
    if(!backbuffer||w<=0||h<=0)return;
    int x0=x<0?0:x,y0=y<0?0:y,x1=x+w,y1=y+h;
    if(x1>(int)width)x1=(int)width;
    if(y1>(int)height)y1=(int)height;
    for(int yy=y0;yy<y1;yy++){
        uint32_t*p=backbuffer+(size_t)yy*stride+x0;
        for(int xx=x0;xx<x1;xx++)*p++=c;
    }
}
static void stroke_rect(int x,int y,int w,int h,uint32_t c){fill_rect(x,y,w,1,c);fill_rect(x,y+h-1,w,1,c);fill_rect(x,y,1,h,c);fill_rect(x+w-1,y,1,h,c);}
static void put_pixel(int x,int y,uint32_t c){if(backbuffer&&x>=0&&y>=0&&(uint32_t)x<width&&(uint32_t)y<height)backbuffer[(size_t)y*stride+(size_t)x]=c;}
static int hit(uint32_t x,uint32_t y,int a,int b,int w,int h){return x>=(uint32_t)a&&x<(uint32_t)(a+w)&&y>=(uint32_t)b&&y<(uint32_t)(b+h);}
static const uint8_t*glyph_data(char c){if(c>='A'&&c<='Z')return letters[c-'A'];if(c>='a'&&c<='z')return letters[c-'a'];if(c>='0'&&c<='9')return digits[c-'0'];return NULL;}
static void glyph(int x,int y,char c,uint32_t col,int s){
    const uint8_t*g=glyph_data(c);
    if(g){for(int a=0;a<5;a++)for(int b=0;b<7;b++)if(g[a]&(1u<<b))fill_rect(x+a*s,y+b*s,s,s,col);return;}
    if(c=='+'){fill_rect(x+2*s,y,s,7*s,col);fill_rect(x,y+3*s,5*s,s,col);}
    else if(c=='-')fill_rect(x,y+3*s,5*s,s,col);
    else if(c=='='){fill_rect(x,y+2*s,5*s,s,col);fill_rect(x,y+5*s,5*s,s,col);}
    else if(c=='*'){fill_rect(x+2*s,y,s,7*s,col);fill_rect(x,y+2*s,5*s,s,col);fill_rect(x+1*s,y+4*s,3*s,s,col);}
    else if(c=='/')for(int i=0;i<7;i++)fill_rect(x+(6-i)*s,y+i*s,s,s,col);
    else if(c==':'){fill_rect(x,y+s,s,s,col);fill_rect(x,y+5*s,s,s,col);}
    else if(c=='.')fill_rect(x,y+6*s,s,s,col);
    else if(c==','){fill_rect(x,y+6*s,s,s,col);fill_rect(x-s,y+7*s,s,s,col);}
    else if(c=='!'){fill_rect(x+2*s,y,s,5*s,col);fill_rect(x+2*s,y+6*s,s,s,col);}
    else if(c=='?'){fill_rect(x,y,s*5,s,col);fill_rect(x+4*s,y,s,s*3,col);fill_rect(x+2*s,y+3*s,s,s*2,col);fill_rect(x+2*s,y+6*s,s,s,col);}
    else if(c=='_')fill_rect(x,y+7*s,5*s,s,col);
    else if(c=='|')fill_rect(x+2*s,y,s,8*s,col);
    else if(c=='('){fill_rect(x+s,y+s,s,s*6,col);fill_rect(x+2*s,y,s,s,col);fill_rect(x+2*s,y+7*s,s,s,col);}
    else if(c==')'){fill_rect(x+3*s,y+s,s,s*6,col);fill_rect(x+2*s,y,s,s,col);fill_rect(x+2*s,y+7*s,s,s,col);}
    else if(c=='['){fill_rect(x,y,5*s,s,col);fill_rect(x,y, s,8*s,col);fill_rect(x,y+7*s,5*s,s,col);}
    else if(c==']'){fill_rect(x,y,5*s,s,col);fill_rect(x+4*s,y,s,8*s,col);fill_rect(x,y+7*s,5*s,s,col);}
    else if(c=='#'){fill_rect(x+s,y,s,s*8,col);fill_rect(x+3*s,y,s,s*8,col);fill_rect(x,y+2*s,5*s,s,col);fill_rect(x,y+5*s,5*s,s,col);}
    else if(c=='%'){fill_rect(x,y,s,s,col);fill_rect(x+4*s,y+6*s,s,s,col);for(int i=0;i<5;i++)fill_rect(x+(4-i)*s,y+i*s,s,s,col);}
}
static void text(int x,int y,const char*s,uint32_t col,int sc){while(*s){if(*s==' ')x+=6*sc;else{glyph(x,y,*s,col,sc);x+=6*sc;}s++;}}
static void text_clip(int x,int y,const char*s,uint32_t col,int sc,int maxw){int n=0;while(*s&&n+6*sc<=maxw){if(*s==' ')x+=6*sc;else{glyph(x,y,*s,col,sc);x+=6*sc;}s++;n+=6*sc;}}
static void u64_text(int x,int y,uint64_t v,uint32_t col,int sc){char b[32];int n=0;if(!v)b[n++]='0';while(v&&n<31){b[n++]=(char)('0'+v%10);v/=10;}while(n){glyph(x,y,b[--n],col,sc);x+=6*sc;}}
static void s64_text(int x,int y,int64_t v,uint32_t col,int sc){if(v<0){glyph(x,y,'-',col,sc);x+=6*sc;u64_text(x,y,(uint64_t)(-v),col,sc);}else u64_text(x,y,(uint64_t)v,col,sc);}
static int str_eq(const char*a,const char*b){while(*a&&*b&&*a==*b){a++;b++;}return *a==0&&*b==0;}
static int ci_eq(const char*a,const char*b){while(*a&&*b){char x=*a,y=*b;if(x>='A'&&x<='Z')x=(char)(x-'A'+'a');if(y>='A'&&y<='Z')y=(char)(y-'A'+'a');if(x!=y)return 0;a++;b++;}return *a==0&&*b==0;}
static int begins_ci(const char*a,const char*b){while(*b){char x=*a++,y=*b++;if(x>='A'&&x<='Z')x=(char)(x-'A'+'a');if(y>='A'&&y<='Z')y=(char)(y-'A'+'a');if(x!=y)return 0;}return 1;}
static const char*find_ci(const char*s,const char*needle){if(!*needle)return s;for(;*s;s++)if(begins_ci(s,needle))return s;return NULL;}

static void present(void){
    if(!framebuffer||!backbuffer)return;
    native_pointer_hide();
    size_t n=(size_t)stride*height;
    for(size_t i=0;i<n;i++)framebuffer[i]=backbuffer[i];
    native_pointer_show();
}
static void init_backbuffer(void){
    if(boot_info->backbuffer_base&&boot_info->backbuffer_size>=(uint64_t)stride*height*4ULL)
        backbuffer=(uint32_t*)(uintptr_t)boot_info->backbuffer_base;
    else backbuffer=framebuffer;
}
static void memory_stats(void){
    total_memory=largest_region=0;
    if(!boot_info->memory_map||!boot_info->memory_descriptor_size)return;
    uint8_t*p=(uint8_t*)(uintptr_t)boot_info->memory_map,*e=p+boot_info->memory_map_size;
    while(p+boot_info->memory_descriptor_size<=e){
        uint32_t type=*(uint32_t*)p;uint64_t*q=(uint64_t*)(p+8);
        if(type==EFI_CONVENTIONAL_MEMORY){uint64_t s=q[1]*4096ULL;total_memory+=s;if(s>largest_region)largest_region=s;}
        p+=boot_info->memory_descriptor_size;
    }
}
static void load_settings(void){
    light_theme=0;pointer_scale=1;accent_id=0;service_flags=0;logging_level=0;boot_delay=3;
    if(boot_info->uefi_get_variable){
        GETVAR get=(GETVAR)(uintptr_t)boot_info->uefi_get_variable;uint32_t a=0;uint64_t z=sizeof(SETTINGS);SETTINGS s={0};
        if(get((uint16_t*)settings_name,(GUID*)&settings_guid,&a,&z,&s)==0&&s.magic==0x53545654u){light_theme=s.light?1:0;pointer_scale=s.scale<1?1:(s.scale>4?4:s.scale);accent_id=s.accent&3u;service_flags=s.service_flags;logging_level=s.logging;boot_delay=s.boot_delay>10?10:s.boot_delay;}
    }
    native_pointer_set_scale(pointer_scale);
}
static void save_settings(void){
    if(!boot_info->uefi_set_variable)return;
    SETVAR set=(SETVAR)(uintptr_t)boot_info->uefi_set_variable;SETTINGS s={0x53545654u,light_theme,pointer_scale,accent_id,service_flags,logging_level,boot_delay,0,0};set((uint16_t*)settings_name,(GUID*)&settings_guid,7,sizeof(s),&s);
}
static void browser_copy_url(char*out,const char*in);
static int browser_fetch(void);
static void browser_tab_switch(int index);
static void browser_load_first_image(void);
static void terminal_add(const char*s);

static void load_bookmarks(void){
    browser_bookmark_count=0;
    for(int i=0;i<8;i++)browser_bookmarks[i][0]=0;
    if(!boot_info->uefi_get_variable)return;
    struct {uint32_t magic;uint8_t count;uint8_t pad[3];char url[8][BROWSER_URL_MAX+1];} data={0};
    GETVAR get=(GETVAR)(uintptr_t)boot_info->uefi_get_variable;uint32_t a=0;uint64_t z=sizeof(data);
    if(get((uint16_t*)bookmarks_name,(GUID*)&bookmarks_guid,&a,&z,&data)==0&&data.magic==0x53545642u){
        browser_bookmark_count=data.count>8?8:data.count;
        for(int i=0;i<browser_bookmark_count;i++)browser_copy_url(browser_bookmarks[i],data.url[i]);
    }
}
static void save_bookmarks(void){
    if(!boot_info->uefi_set_variable)return;
    struct {uint32_t magic;uint8_t count;uint8_t pad[3];char url[8][BROWSER_URL_MAX+1];} data={0};
    data.magic=0x53545642u;data.count=browser_bookmark_count>8?8:browser_bookmark_count;
    for(int i=0;i<data.count;i++)browser_copy_url(data.url[i],browser_bookmarks[i]);
    SETVAR set=(SETVAR)(uintptr_t)boot_info->uefi_set_variable;
    set((uint16_t*)bookmarks_name,(GUID*)&bookmarks_guid,7,sizeof(data),&data);
}
static void bookmark_current(void){
    if(!browser_url[0])return;
    for(int i=0;i<browser_bookmark_count;i++)if(ci_eq(browser_bookmarks[i],browser_url)){terminal_add("BOOKMARK ALREADY SAVED");return;}
    int slot=browser_bookmark_count<8?browser_bookmark_count:7;
    if(browser_bookmark_count<8)browser_bookmark_count++;
    for(int i=slot;i>0&&browser_bookmark_count==8;i--)browser_copy_url(browser_bookmarks[i],browser_bookmarks[i-1]);
    browser_copy_url(browser_bookmarks[slot],browser_url);
    save_bookmarks();
}
static void open_first_bookmark(void){
    if(!browser_bookmark_count)return;
    browser_copy_url(browser_url,browser_bookmarks[0]);browser_focus=0;browser_fetch();
}

static void load_note(void){
    note_len=note_cursor=0;note_dirty=0;editor_target_path[0]=0;
    if(!boot_info->uefi_get_variable)return;
    GETVAR get=(GETVAR)(uintptr_t)boot_info->uefi_get_variable;uint32_t a=0;uint64_t z=NOTE_MAX;
    if(get((uint16_t*)note_name,(GUID*)&note_guid,&a,&z,note)==0){if(z>NOTE_MAX)z=NOTE_MAX;note_len=(size_t)z;note_cursor=note_len;}
    note[note_len]=0;
}
static void save_note(void){
    if(boot_info->uefi_set_variable){
        SETVAR set=(SETVAR)(uintptr_t)boot_info->uefi_set_variable;
        set((uint16_t*)note_name,(GUID*)&note_guid,7,note_len,note);
    }
    if(boot_info->uefi_write_text&&note_len){
        WRITEFILE write=(WRITEFILE)(uintptr_t)boot_info->uefi_write_text;
        if(editor_target_path[0])write(editor_target_path,note,note_len);
        else write((const uint16_t*)note_file_name,note,note_len);
    }
    note_dirty=0;
}

static void file_name(const STEVEOS_BOOT_FILE*f,char*out,size_t cap){size_t i=0;if(!cap)return;while(f&&i+1<cap&&i<STEVEOS_BOOT_FILE_NAME_MAX&&f->name[i]){uint16_t c=f->name[i++];out[i-1]=c<128?(char)c:'?';}out[i]=0;}
static void load_text_file(STEVEOS_BOOT_FILE*f){
    if(!f||!f->data||!f->size)return;
    size_t n=(size_t)f->size;if(n>NOTE_MAX)n=NOTE_MAX;
    const uint8_t*d=(const uint8_t*)(uintptr_t)f->data;
    for(size_t i=0;i<n;i++)note[i]=(d[i]>=32||d[i]=='\n'||d[i]=='\t')?d[i]:' ';
    note[n]=0;note_len=n;note_cursor=n;note_dirty=0;
    size_t p=0;while(p<127&&f->name[p]){editor_target_path[p]=f->name[p];p++;}editor_target_path[p]=0;
    current_app=APP_EDITOR;mark_dirty();
}
static void internet_test(void){
    net_test_state=0;
    if(!boot_info->uefi_http_get)return;
    HTTPGET get=(HTTPGET)(uintptr_t)boot_info->uefi_http_get;
    const char*p="http://example.com/";
    uint16_t url[32];size_t n=0;while(p[n]&&n+1<sizeof(url)/sizeof(url[0])){url[n]=(uint16_t)(unsigned char)p[n];n++;}url[n]=0;
    char body[128];uint64_t len=0;uint32_t status=0;uint64_t st=get(url,body,sizeof(body)-1,&len,&status);
    if(st==0&&status>=200&&status<500)net_test_state=1;else net_test_state=2;
}
static void refresh_network_info(void){
    network_info_valid=0;
    if(!boot_info->uefi_network_info)return;
    NETINFO fn=(NETINFO)(uintptr_t)boot_info->uefi_network_info;
    if(fn(&network_info)==0)network_info_valid=1;
}
static void refresh_update_info(void){
    update_checked=1;
    if(!boot_info->uefi_update_check)return;
    UPDATECHECK fn=(UPDATECHECK)(uintptr_t)boot_info->uefi_update_check;
    fn(&update_info);
}
static void apply_system_update(void){
    if(!boot_info->uefi_update_apply){terminal_add("UPDATE BRIDGE UNAVAILABLE");return;}
    UPDATEAPPLY fn=(UPDATEAPPLY)(uintptr_t)boot_info->uefi_update_apply;
    uint64_t st=fn();
    if(st==0){
        update_info.available=0;
        update_info.state=3;
        terminal_add("UPDATE INSTALLED - REBOOT STEVEOS TO USE IT");
        update_checked=1;
    }else{
        update_info.state=2;
        terminal_add("UPDATE FAILED - CHECK NETWORK + RELEASE ASSET");
    }
}
static const char*service_state(uint8_t bit){
    return (service_flags&bit)?"ARMED":"OFF";
}
static int is_app_package(const STEVEOS_BOOT_FILE*f){
    if(!f||!f->name[0])return 0;
    size_t n=0;
    while(n<STEVEOS_BOOT_FILE_NAME_MAX&&f->name[n])n++;
    int in_apps=0,efi=0;
    for(size_t i=0;i<n;i++){
        if((f->name[i]=='/'||f->name[i]=='\\')&&i>=4){
            char a=(char)f->name[i-4],b=(char)f->name[i-3],d=(char)f->name[i-2],e=(char)f->name[i-1];
            if((a=='A'||a=='a')&&(b=='p'||b=='P')&&(d=='p'||d=='P')&&(e=='s'||e=='S'))in_apps=1;
        }
    }
    if(n>=4){
        char a=(char)f->name[n-4],b=(char)f->name[n-3],d=(char)f->name[n-2],e=(char)f->name[n-1];
        efi=(a=='.'&&(b=='e'||b=='E')&&(d=='f'||d=='F')&&(e=='i'||e=='I'));
        if(!efi){
            efi=(a=='.'&&(b=='e'||b=='E')&&(d=='x'||d=='X')&&(e=='e'||e=='E'));
        }
    }
    return in_apps&&efi;
}
static int is_windows_package(const STEVEOS_BOOT_FILE*f){
    if(!f)return 0;
    size_t n=0;while(n<STEVEOS_BOOT_FILE_NAME_MAX&&f->name[n])n++;
    if(n<4)return 0;
    char a=(char)f->name[n-4],b=(char)f->name[n-3],d=(char)f->name[n-2],e=(char)f->name[n-1];
    return a=='.'&&(b=='e'||b=='E')&&(d=='x'||d=='X')&&(e=='e'||e=='E');
}
static void refresh_install_targets(void){
    install_target_count=0;install_target_pick=-1;install_armed=0;
    if(!boot_info->uefi_list_install_targets)return;
    LISTTARGETS fn=(LISTTARGETS)(uintptr_t)boot_info->uefi_list_install_targets;
    install_target_count=fn(install_targets,16);
}
static void scan_app_packages(void){
    app_package_count=0;app_package_pick=-1;app_install_done=0;
    if(!boot_files||!boot_info)return;
    for(uint64_t i=0;i<boot_info->boot_file_count&&app_package_count<24;i++)
        if(is_app_package(&boot_files[i]))app_package_indices[app_package_count++]=(int)i;
}
static void app_download_selected(void){
    if(!boot_info->uefi_download_app||!app_download_url[0])return;
    uint16_t url[ BROWSER_URL_MAX+1 ];
    for(size_t i=0;i<BROWSER_URL_MAX&&app_download_url[i];i++)url[i]=(uint16_t)(unsigned char)app_download_url[i],url[i+1]=0;
    const char*p=app_download_url;size_t last=0;for(size_t i=0;p[i];i++)if(p[i]=='/'||p[i]=='\\')last=i+1;
    uint16_t name[96];size_t n=0;for(size_t i=last;p[i]&&n+1<sizeof(name)/sizeof(name[0]);i++)name[n++]=(uint16_t)(unsigned char)p[i];name[n]=0;
    if(!n)return;
    DOWNLOADAPP fn=(DOWNLOADAPP)(uintptr_t)boot_info->uefi_download_app;
    uint64_t st=fn(url,name);
    app_install_done=(st==0)?1:2;
    if(st==0)scan_app_packages();
}
static void app_install_selected(void){
    if(app_package_pick<0||app_package_pick>=app_package_count)return;
    int idx=app_package_indices[app_package_pick];
    uint64_t st;
    if(is_windows_package(&boot_files[idx])){
        if(!boot_info->uefi_install_windows_app){app_install_done=2;return;}
        INSTALLWINDOWSAPP fn=(INSTALLWINDOWSAPP)(uintptr_t)boot_info->uefi_install_windows_app;
        st=fn(boot_files[idx].name);
    }else{
        if(!boot_info->uefi_install_app){app_install_done=2;return;}
        INSTALLAPP fn=(INSTALLAPP)(uintptr_t)boot_info->uefi_install_app;
        st=fn(boot_files[idx].name);
    }
    app_install_done=(st==0)?1:2;
}
static void app_launch_selected(void){
    if(app_package_pick<0||app_package_pick>=app_package_count)return;
    int idx=app_package_indices[app_package_pick];
    if(is_windows_package(&boot_files[idx])){
        if(!boot_info->uefi_run_windows_app)return;
        RUNWINDOWSAPP fn=(RUNWINDOWSAPP)(uintptr_t)boot_info->uefi_run_windows_app;
        uint16_t path[STEVEOS_BOOT_FILE_NAME_MAX+16];
        size_t n=0;while(n<STEVEOS_BOOT_FILE_NAME_MAX&&boot_files[idx].name[n])n++;
        size_t start=0;for(size_t i=0;i<n;i++)if(boot_files[idx].name[i]=='/'||boot_files[idx].name[i]=='\\')start=i+1;
        const uint16_t prefix[]={L'\\',L'S',L't',L'e',L'v',L'e',L'O',L'S',L'\\',L'A',L'p',L'p',L's',L'\\'};
        size_t p=0;for(size_t i=0;i<sizeof(prefix)/sizeof(prefix[0]);i++)path[p++]=prefix[i];
        for(size_t i=start;i<n&&p+1<sizeof(path)/sizeof(path[0]);i++)path[p++]=boot_files[idx].name[i];
        path[p]=0;
        fn(path);
        return;
    }
    if(!boot_info->uefi_launch_app)return;
    uint16_t path[STEVEOS_BOOT_FILE_NAME_MAX+16];
    size_t n=0;while(n<STEVEOS_BOOT_FILE_NAME_MAX&&boot_files[idx].name[n])n++;
    size_t start=0;for(size_t i=0;i<n;i++)if(boot_files[idx].name[i]=='/'||boot_files[idx].name[i]=='\\')start=i+1;
    const uint16_t prefix[]={L'\\',L'S',L't',L'e',L'v',L'e',L'O',L'S',L'\\',L'A',L'p',L'p',L's',L'\\'};
    size_t p=0;for(size_t i=0;i<sizeof(prefix)/sizeof(prefix[0]);i++)path[p++]=prefix[i];
    for(size_t i=start;i<n&&p+1<sizeof(path)/sizeof(path[0]);i++)path[p++]=boot_files[idx].name[i];
    path[p]=0;
    LAUNCHAPP fn=(LAUNCHAPP)(uintptr_t)boot_info->uefi_launch_app;
    fn(path);
}
static void install_self_now(void){
    if(install_target_pick<0||!boot_info->uefi_install_self)return;
    INSTALLSELF fn=(INSTALLSELF)(uintptr_t)boot_info->uefi_install_self;
    uint64_t st=fn((uint64_t)install_target_pick);
    install_armed=(st==0)?2:3;
}
static char hex_upper(uint8_t v){return v<10?(char)('0'+v):(char)('A'+v-10);}
static void mac_text(char*out,size_t cap){
    if(!out||cap<3){if(cap)out[0]=0;return;}
    if(!network_info_valid||network_info.mac_size<6){out[0]=0;return;}
    size_t p=0;
    for(uint32_t i=0;i<6&&p+3<cap;i++){
        out[p++]=hex_upper((uint8_t)(network_info.mac[i]>>4));
        out[p++]=hex_upper((uint8_t)(network_info.mac[i]&15));
        if(i<5)out[p++]=':';
    }
    out[p]=0;
}
static void open_server_config(void);
static int server_runtime_detected(void);

static void panel(void){
    fill_rect(0,0,(int)width,(int)height,bg_color());
    fill_rect(0,0,(int)width,42,light_theme?0xDCE3E7u:0x151D26u);
    fill_rect(0,42,(int)width,2,accent_dark());
    text(18,13,"STEVEOS",text_color(),2);
    text(105,16,"SYSTEM",sub_color(),1);
    text((int)width-230,15,native_usb_mouse_present()?"USB":"INPUT",sub_color(),1);
    EFI_TIME_STEV t={0};
    GETTIME gt=(GETTIME)(uintptr_t)boot_info->uefi_get_time;
    if(gt){gt(&t,NULL);char clk[6];clk[0]=(char)('0'+(t.hour/10));clk[1]=(char)('0'+t.hour%10);clk[2]=':';clk[3]=(char)('0'+(t.minute/10));clk[4]=(char)('0'+t.minute%10);clk[5]=0;text((int)width-72,15,clk,text_color(),1);}
}
static void taskbar(void){
    int h=54,y=(int)height-h;
    fill_rect(0,y,(int)width,h,light_theme?0xD5DCE1u:0x161D26u);
    fill_rect(0,y,(int)width,1,light_theme?0xBAC4CBu:0x2A3643u);
    fill_rect(12,y+8,58,38,menu_open?accent_dark():accent_color());
    text(25,y+20,"MENU",0xFFFFFFu,1);
    const char*icons[]={"WEB","CALC","NOTE","FILES","TERM","SET"};
    const int apps[]={APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_TERMINAL,APP_SETTINGS};
    for(int i=0;i<6;i++){
        int x=84+i*72;fill_rect(x,y+8,64,38,(current_app==apps[i])?panel2_color():panel_color());
        text(x+10,y+20,icons[i],text_color(),1);
    }
    text((int)width-154,y+20,update_info.available?"UPDATE READY":"STEVEOS",update_info.available?danger_color():sub_color(),1);
    fill_rect((int)width-76,y+8,64,38,power_menu?accent_dark():panel_color());
    text((int)width-64,y+20,"POWER",text_color(),1);
}
static void draw_power_menu(void){
    if(!power_menu)return;
    int w=300,h=154,x=(int)width-w-18,y=(int)height-54-h-12;
    fill_rect(x+4,y+4,w,h,0x05080Bu);fill_rect(x,y,w,h,panel_color());
    text(x+20,y+18,"SESSION",text_color(),2);text(x+20,y+46,"STEVEOS POWER",sub_color(),1);
    fill_rect(x+20,y+72,76,44,danger_color());text(x+38,y+86,"HALT",0xFFFFFFu,1);
    fill_rect(x+112,y+72,76,44,accent_dark());text(x+126,y+86,"REBOOT",0xFFFFFFu,1);
    fill_rect(x+204,y+72,76,44,panel2_color());text(x+224,y+86,"CANCEL",text_color(),1);
}
static void window_bar(const char*title,const char*hint){
    fill_rect(18,58,(int)width-36,(int)height-128,panel_color());
    fill_rect(18,58,(int)width-36,40,panel2_color());
    text(34,72,title,text_color(),1);text_clip((int)width-260,72,hint,sub_color(),1,215);
    fill_rect((int)width-62,66,30,24,danger_color());text((int)width-52,74,"X",0xFFFFFFu,1);
}

static int mint_icon_info(int type,const uint8_t**pixels,uint16_t*w,uint16_t*h){
    const uint8_t*p=_binary_build_mint_icons_raw_start,*e=_binary_build_mint_icons_raw_end;
    if((size_t)(e-p)<8||*(const uint32_t*)p!=0x43494D59u)return 0;
    uint32_t count=*(const uint32_t*)(p+4);p+=8;
    if(type<0||(uint32_t)type>=count)return 0;
    for(int i=0;i<=type;i++){
        if(p+24>e)return 0;
        uint16_t iw=*(const uint16_t*)p,ih=*(const uint16_t*)(p+2);
        uint32_t len=*(const uint32_t*)(p+4);
        p+=24;
        if(!iw||!ih||p+(size_t)len>e)return 0;
        if(i==type){*w=iw;*h=ih;*pixels=p;return 1;}
        p+=len;
    }
    return 0;
}
static void draw_icon(int x,int y,int type){
    const uint8_t*p=NULL;uint16_t iw=0,ih=0;
    fill_rect(x,y,52,52,panel2_color());
    if(mint_icon_info(type,&p,&iw,&ih)){
        int dw=44,dh=(int)((uint32_t)ih*dw/iw);
        if(dh>44){dh=44;dw=(int)((uint32_t)iw*dh/ih);}
        int ox=x+(52-dw)/2,oy=y+(52-dh)/2;
        for(int yy=0;yy<dh;yy++)for(int xx=0;xx<dw;xx++){
            uint32_t sx=(uint32_t)xx*iw/dw,sy=(uint32_t)yy*ih/dh;
            const uint8_t*q=p+((size_t)sy*iw+sx)*4;
            if(q[3]>=24)put_pixel(ox+xx,oy+yy,0xFF000000u|(uint32_t)q[2]|((uint32_t)q[1]<<8)|((uint32_t)q[0]<<16));
        }
        return;
    }
    uint32_t a=accent_color();
    if(type==0){fill_rect(x+14,y+12,24,25,a);fill_rect(x+9,y+21,34,16,a);}
    else if(type==1){fill_rect(x+10,y+10,32,32,a);text(x+18,y+17,"+",0xFFFFFFu,2);}
    else if(type==2){fill_rect(x+11,y+9,30,34,0xFFFFFFu);fill_rect(x+15,y+14,22,2,a);fill_rect(x+15,y+21,22,2,a);fill_rect(x+15,y+28,16,2,a);}
    else if(type==3){fill_rect(x+8,y+15,36,27,a);fill_rect(x+13,y+11,19,7,a);}
    else if(type==4){fill_rect(x+11,y+10,30,34,0x101820u);text(x+16,y+19,"_",a,2);}
    else if(type==5){stroke_rect(x+10,y+10,32,32,a);fill_rect(x+17,y+17,18,18,a);}
    else {fill_rect(x+12,y+12,28,28,a);fill_rect(x+20,y+7,12,8,a);}
}

static int contains_ci(const char*a,const char*b){
    if(!b||!b[0])return 1;
    for(const char*p=a;*p;p++){
        const char*x=p,*y=b;
        while(*x&&*y){
            char cx=*x,cy=*y;
            if(cx>='A'&&cx<='Z')cx=(char)(cx-'A'+'a');
            if(cy>='A'&&cy<='Z')cy=(char)(cy-'A'+'a');
            if(cx!=cy)break;
            x++;y++;
        }
        if(!*y)return 1;
    }
    return 0;
}
static const char*menu_names[]={"Web Browser","Calculator","Text Editor","File Manager","Image Viewer","Settings","Task Manager","Terminal","Calendar","Control Center","About SteveOS","System Information","Device Manager","Installer","App Store","Server Manager","Advanced Settings"};
static const int menu_apps[]={APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_IMAGE,APP_SETTINGS,APP_TASKS,APP_TERMINAL,APP_CALENDAR,APP_CONTROL,APP_ABOUT,APP_SYSINFO,APP_DEVICES,APP_INSTALLER,APP_STORE,APP_SERVER,APP_ADVANCED};
static int menu_filtered_app(int visible){
    int n=0;
    for(int i=0;i<17;i++)if(contains_ci(menu_names[i],menu_search)){if(n==visible)return menu_apps[i];n++;}
    return APP_DESKTOP;
}
static void draw_start_menu(void){
    int mw=430,mh=(int)height-76,x=12,y=(int)height-62-mh;
    fill_rect(x+4,y+4,mw,mh,0x06090Du);fill_rect(x,y,mw,mh,panel_color());
    text(x+22,y+20,"STEVEOS APPLICATIONS",text_color(),2);
    text(x+22,y+48,"MINT-STYLE DESKTOP",sub_color(),1);
    fill_rect(x+18,y+60,mw-36,30,panel2_color());
    text(x+30,y+70,menu_search[0]?menu_search:"SEARCH APPLICATIONS",menu_search[0]?text_color():sub_color(),1);
    int shown=0;
    for(int i=0;i<17;i++)if(contains_ci(menu_names[i],menu_search)){
        int row=shown%9,col=shown/9,bx=x+18+col*196,by=y+98+row*55;
        fill_rect(bx,by,180,45,(current_app==menu_apps[i])?panel2_color():bg_color());
        static const int icon_map[]={0,1,2,3,7,6,5,4,7,7,7,6,15,14,15,15,6};draw_icon(bx+5,by-4,icon_map[i]);
        text(bx+66,by+12,menu_names[i],text_color(),1);
        shown++;
    }
    fill_rect(x+18,y+mh-44,mw-36,28,panel2_color());
    text(x+30,y+mh-36,menu_search[0]?"TYPE TO FILTER  ENTER LAUNCH  ESC CLOSE":"TYPE TO SEARCH  ENTER LAUNCH",sub_color(),1);
}

static void draw_installer(void){
    window_bar("INSTALL STEVEOS","SAFE EXISTING-FILESYSTEM INSTALLER");
    text(42,116,"TARGET VOLUMES",accent_color(),1);
    text(42,138,"STEVEOS WILL NOT PARTITION OR FORMAT A DISK.",sub_color(),1);
    text(42,158,"IT WRITES EFI/BOOT/BOOTX64.EFI TO THE SELECTED VOLUME.",sub_color(),1);
    int shown=install_target_count>8?8:(int)install_target_count;
    for(int i=0;i<shown;i++){
        int y=184+i*40;
        fill_rect(42,y,(int)width-84,32,i==install_target_pick?accent_dark():panel_color());
        text(58,y+10,install_targets[i].removable?"REMOVABLE VOLUME":"INTERNAL VOLUME",i==install_target_pick?0xFFFFFFu:text_color(),1);
        u64_text((int)width-250,y+10,(install_targets[i].blocks*(uint64_t)install_targets[i].block_size)/1000000000ULL,i==install_target_pick?0xFFFFFFu:text_color(),1);
        text((int)width-190,y+10,"GB",sub_color(),1);
        if(!install_targets[i].filesystem)text((int)width-150,y+10,"NO FS",danger_color(),1);else text((int)width-150,y+10,"EFI FS",good_color(),1);
    }
    fill_rect(42,(int)height-136,150,38,install_armed==1?accent_dark():panel2_color());
    text(62,(int)height-125,install_armed==1?"ARMED":"ARM INSTALL",text_color(),1);
    fill_rect(208,(int)height-136,150,38,install_armed==1?accent_color():panel2_color());
    text(232,(int)height-125,"INSTALL STEVEOS",text_color(),1);
    fill_rect((int)width-180,(int)height-136,138,38,panel2_color());
    text((int)width-160,(int)height-125,"REFRESH",text_color(),1);
    if(install_armed==2)text(42,(int)height-96,"INSTALL COMPLETE",good_color(),1);
    else if(install_armed==3)text(42,(int)height-96,"INSTALL FAILED",danger_color(),1);
    else text(42,(int)height-96,"SELECT A VOLUME, ARM INSTALL, THEN CONFIRM.",sub_color(),1);
    taskbar();
}
static void draw_store(void){
    window_bar("APP STORE","EFI + WINDOWS APPLICATION PACKAGES");
    text(42,116,"AVAILABLE PACKAGES",accent_color(),1);
    fill_rect(42,134,(int)width-84,34,bg_color());stroke_rect(42,134,(int)width-84,34,app_download_focus?accent_color():panel2_color());
    text_clip(54,143,app_download_url[0]?app_download_url:"DOWNLOAD EFI APP FROM URL",text_color(),1,(int)width-110);
    text((int)width-150,143,"ENTER FETCH",sub_color(),1);
    text(42,176,"EFI apps run in firmware; EXE apps use Server Mode + Wine.",sub_color(),1);
    if(app_package_count==0){
        text(42,188,"NO INSTALLABLE PACKAGES ON THIS BOOT VOLUME.",text_color(),2);
        text(42,224,"ADD A .EFI OR .EXE UNDER \\Apps TO MAKE IT INSTALLABLE.",sub_color(),1);
    }
    for(int i=0;i<app_package_count&&i<7;i++){
        int y=214+i*48;int idx=app_package_indices[i];char name[80];file_name(&boot_files[idx],name,sizeof(name));
        fill_rect(42,y,(int)width-84,38,i==app_package_pick?accent_dark():panel_color());
        text(58,y+11,name,i==app_package_pick?0xFFFFFFu:text_color(),1);
        text((int)width-290,y+11,is_windows_package(&boot_files[idx])?"WINDOWS EXE":"UEFI APP",sub_color(),1);
    }
    fill_rect(42,(int)height-136,150,38,panel2_color());text(62,(int)height-125,"INSTALL",text_color(),1);
    fill_rect(208,(int)height-136,150,38,app_package_pick>=0?accent_color():panel2_color());text(228,(int)height-125,"INSTALL + RUN",text_color(),1);
    text(42,(int)height-96,app_install_done==1?"PACKAGE INSTALLED":app_install_done==2?"PACKAGE ACTION FAILED":"SELECT A PACKAGE",app_install_done==2?danger_color():app_install_done==1?good_color():sub_color(),1);
    taskbar();
}
static void draw_server(void){
    window_bar("SERVER MANAGER","DISCORD + TAILSCALE");
    text(42,116,"SERVER RUNTIME",accent_color(),1);
    fill_rect(42,140,(int)width-84,120,panel_color());
    text(58,156,"ALPINE SERVER MODE",text_color(),2);
    text(58,184,"NODE.JS + PYTHON + TAILSCALE + SSH + LINUX NETWORKING",sub_color(),1);
    text(58,206,"CONFIG: \\SteveOS\\Server\\server.conf",accent_color(),1);
    text(58,228,"BOT SOURCE: \\SteveOS\\Server\\bot",sub_color(),1);
    fill_rect(42,278,170,38,server_install_state==2?panel2_color():accent_dark());
    text(60,289,server_install_state==2?"SERVER INSTALLED":"INSTALL SERVER",text_color(),1);
    fill_rect(228,278,170,38,server_install_state==2?accent_color():panel2_color());
    text(248,289,"BOOT SERVER MODE",text_color(),1);
    fill_rect(414,278,170,38,panel2_color());text(434,289,"EDIT CONFIG",text_color(),1);
    if(server_install_state==2)text(42,330,"SERVER RUNTIME INSTALLED",good_color(),1);
    else if(server_install_state==3)text(42,330,"SERVER INSTALL FAILED",danger_color(),1);
    else text(42,330,"INSTALL IS NON-DESTRUCTIVE AND TARGET-SPECIFIC.",sub_color(),1);

    text(42,370,"SERVICES",accent_color(),1);
    fill_rect(42,390,(int)width-84,80,panel_color());
    text(58,406,"DISCORD BOT",text_color(),1);text(170,406,(service_flags&1)?"AUTO-START":"OFF",accent_color(),1);
    text(58,430,"TAILSCALE",text_color(),1);text(170,430,(service_flags&2)?"AUTO-START":"OFF",accent_color(),1);
    text(58,454,"SSH",text_color(),1);text(170,454,(service_flags&4)?"AUTO-START":"OFF",accent_color(),1);

    text(42,496,"NETWORK",accent_color(),1);
    fill_rect(42,516,(int)width-84,96,panel2_color());
    text(58,534,network_info_valid?(network_info.media_present?"LINK PRESENT":"NO LINK"):"NO NETWORK ADAPTER",text_color(),1);
    text(58,556,network_info_valid?(network_info.mac_size>=6?"MAC AVAILABLE":"MAC UNKNOWN"):"UEFI SNP UNAVAILABLE",sub_color(),1);
    text(58,578,boot_info->uefi_http_get?"DESKTOP HTTP BRIDGE AVAILABLE":"DESKTOP HTTP BRIDGE OFF",sub_color(),1);
    text(42,(int)height-98,"SERVER MODE USES ALPINE LINUX FOR NATIVE TCP/IP/TLS/USERSPACE SERVICES.",sub_color(),1);
    taskbar();
}

static void draw_advanced(void){
    window_bar("ADVANCED SETTINGS","PERSISTENT SERVICE + DEBUG CONTROLS");
    text(42,116,"SERVER STARTUP",accent_color(),1);
    fill_rect(42,136,(int)width-84,48,panel_color());text(58,151,"AUTO-START DISCORD BOT",text_color(),1);text((int)width-180,151,(service_flags&1)?"ON":"OFF",accent_color(),1);
    fill_rect(42,194,(int)width-84,48,panel_color());text(58,209,"AUTO-START TAILSCALE",text_color(),1);text((int)width-180,209,(service_flags&2)?"ON":"OFF",accent_color(),1);
    fill_rect(42,252,(int)width-84,48,panel_color());text(58,267,"REMOTE SHELL",text_color(),1);text((int)width-180,267,(service_flags&4)?"ON":"OFF",accent_color(),1);
    fill_rect(42,310,(int)width-84,48,panel_color());text(58,325,"VERBOSE LOGGING",text_color(),1);text((int)width-180,325,logging_level?"ON":"OFF",accent_color(),1);
    fill_rect(42,368,(int)width-84,48,panel_color());text(58,383,"BOOT DELAY",text_color(),1);u64_text((int)width-180,383,boot_delay,text_color(),1);text((int)width-150,383,"SECONDS",sub_color(),1);
    text(42,450,"NETWORK RUNTIME",accent_color(),1);
    fill_rect(42,470,(int)width-84,118,panel2_color());text(58,486,network_info_valid?(network_info.media_present?"LINK PRESENT":"NO CARRIER"):"NO NETWORK ADAPTER",text_color(),1);text(58,508,network_info_valid?(network_info.mac_size>=6?"MAC ADDRESS AVAILABLE":"MAC ADDRESS UNKNOWN"):"UEFI SNP UNAVAILABLE",sub_color(),1);text(58,530,net_test_state==1?"INTERNET TEST: PASSED":net_test_state==2?"INTERNET TEST: FAILED":"INTERNET TEST: NOT RUN",net_test_state==1?good_color():net_test_state==2?danger_color():sub_color(),1);text(58,552,"UEFI HTTP CLIENT CAN ACCESS INTERNET WHEN FIRMWARE NETWORKING IS CONFIGURED.",sub_color(),1);text(58,574,"NATIVE TCP/IP + TLS ARE STILL FUTURE RUNTIME COMPONENTS.",sub_color(),1);fill_rect((int)width-220,594,160,34,accent_color());text((int)width-204,603,"TEST INTERNET",0xFFFFFFu,1);
    fill_rect(42,634,(int)width-84,36,panel_color());text(58,644,"SYSTEM UPDATE",accent_color(),1);text(190,644,update_info.available?"NEW VERSION READY":update_info.remote_version[0]?update_info.remote_version:"CHECK NOT RUN",update_info.available?danger_color():text_color(),1);fill_rect((int)width-220,636,76,32,panel2_color());text((int)width-207,646,"CHECK",text_color(),1);fill_rect((int)width-136,636,76,32,update_info.available?accent_color():panel2_color());text((int)width-124,646,"INSTALL",text_color(),1);
    text(42,(int)height-98,"ARROWS  •  F5 SAVE  •  7 CHECK UPDATE  •  8 INSTALL UPDATE  •  REBOOT AFTER INSTALL",sub_color(),1);taskbar();
}
static void draw_desktop(void){
    panel();
    text(28,72,"WELCOME",text_color(),3);
    text(30,104,"A BIGGER NATIVE STEVEOS DESKTOP",sub_color(),1);
    const char*names[]={"WEB BROWSER","CALCULATOR","TEXT EDITOR","FILE MANAGER","IMAGE VIEWER","SETTINGS","TASK MANAGER","TERMINAL","CALENDAR","CONTROL CENTER","ABOUT STEVEOS","SYSTEM INFORMATION","DEVICE MANAGER","INSTALLER","APP STORE","SERVER MANAGER","ADVANCED SETTINGS"};
    const int ap[]={APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_IMAGE,APP_SETTINGS,APP_TASKS,APP_TERMINAL,APP_CALENDAR,APP_CONTROL,APP_ABOUT,APP_SYSINFO,APP_DEVICES,APP_INSTALLER,APP_STORE,APP_SERVER,APP_ADVANCED};
    int cw=250,ch=80,g=14,x0=28,y0=132,cols=width>=1200?4:3;
    for(int i=0;i<17;i++){
        int col=i%cols,row=i/cols,x=x0+col*(cw+g),y=y0+row*(ch+g);
        if(x+cw>(int)width-20)continue;
        fill_rect(x+3,y+4,cw,ch,0x05080Bu);fill_rect(x,y,cw,ch,panel_color());
        static const int desktop_icon_map[]={0,1,2,3,11,6,5,4,7,13,15,14,15,14,15,15,6};draw_icon(x+14,y+14,desktop_icon_map[i]);text(x+82,y+21,names[i],text_color(),1);
        text(x+82,y+43,i==0?"REAL HTTP FIRMWARE BRIDGE":i==1?"INTEGER EXPRESSION ENGINE":i==2?"NVRAM TEXT EDITOR":i==3?"BOOT VOLUME EXPLORER":i==4?"BMP + BOOT IMAGE":i==5?"THEME + INPUT":i==6?"LIVE SYSTEM STATUS":i==7?"NATIVE COMMAND SHELL":i==8?"SYSTEM DATE + TIME":i==9?"HARDWARE CONTROL CENTER":i==10?"LICENSES + BUILD INFO":i==11?"CPU + MEMORY + FIRMWARE":i==12?"PCI HARDWARE ENUMERATION":i==13?"INSTALL TO EXISTING EFI VOLUME":i==14?"EFI + WINDOWS APP PACKAGES":i==15?"DISCORD + TAILSCALE SERVICES":"SERVER + BOOT RUNTIME CONTROLS",sub_color(),1);
        if(ap[i]>=0)fill_rect(x+cw-24,y+17,7,7,(current_app==ap[i])?accent_color():panel2_color());
    }
    text(30,(int)height-82,"TRADITIONAL PANEL  •  KEYBOARD SHORTCUTS  •  NATIVE INPUT  •  MINT-INSPIRED VISUALS",sub_color(),1);
    taskbar();
    if(menu_open)draw_start_menu();
}

static int64_t parse_expr(const char**ps,int*ok);
static int64_t parse_factor(const char**ps,int*ok){
    const char*p=*ps;while(*p==' ')p++;
    if(*p=='-'){p++;*ps=p;int64_t v=parse_factor(ps,ok);return -v;}
    if(*p=='('){p++;*ps=p;int64_t v=parse_expr(ps,ok);p=*ps;while(*p==' ')p++;if(*p!=')'){*ok=0;return 0;}*ps=p+1;return v;}
    if(*p<'0'||*p>'9'){*ok=0;return 0;}
    int64_t v=0;while(*p>='0'&&*p<='9'){if(v>922337203685477580LL)*ok=0;v=v*10+(*p-'0');p++;}*ps=p;return v;
}
static int64_t parse_term(const char**ps,int*ok){
    int64_t v=parse_factor(ps,ok);while(*ok){const char*p=*ps;while(*p==' ')p++;char op=*p;if(op!='*'&&op!='/')break;p++;*ps=p;int64_t r=parse_factor(ps,ok);if(!*ok)break;if(op=='*')v*=r;else{if(!r){*ok=0;break;}v/=r;}}return v;
}
static int64_t parse_expr(const char**ps,int*ok){
    int64_t v=parse_term(ps,ok);while(*ok){const char*p=*ps;while(*p==' ')p++;char op=*p;if(op!='+'&&op!='-')break;p++;*ps=p;int64_t r=parse_term(ps,ok);if(!*ok)break;if(op=='+')v+=r;else v-=r;}return v;
}
static void calc_eval(void){
    calc_input[calc_len]=0;const char*p=calc_input;int ok=1;int64_t v=parse_expr(&p,&ok);while(*p==' ')p++;if(!*p&&ok){calc_result=v;calc_has_result=1;}else calc_has_result=0;
}

static void draw_calc(void){
    window_bar("CALCULATOR","ENTER EVALUATES  BACKSPACE CLEARS");
    fill_rect(38,114,(int)width-76,74,bg_color());stroke_rect(38,114,(int)width-76,74,panel2_color());
    text(58,128,calc_input,text_color(),2);
    if(calc_has_result){text(58,159,"=",accent_color(),1);s64_text(82,154,calc_result,text_color(),2);}
    const char*keys[]={"7","8","9","/","4","5","6","*","1","2","3","-","0","(",")","+","C","=","."};
    int bw=100,bh=46,g=10,cols=5,x0=38,y0=204;
    for(int i=0;i<19;i++){int bx=x0+(i%cols)*(bw+g),by=y0+(i/cols)*(bh+g);fill_rect(bx,by,bw,bh,(i==18||i==17)?accent_dark():panel2_color());text(bx+42,by+13,keys[i],0xFFFFFFu,2);}
    text(40,(int)height-98,"Supports +  -  *  /  parentheses and unary minus",sub_color(),1);taskbar();
}

static void note_key(uint8_t s){
    if(s==0x2A||s==0x36){shift_down=1;return;}if(s==0xAA||s==0xB6){shift_down=0;return;}
    if(s==0x0E){if(note_cursor){for(size_t i=note_cursor-1;i<note_len;i++)note[i]=note[i+1];note_cursor--;note_len--;note_dirty=1;}return;}
    if(s==0x1C){if(note_len<NOTE_MAX){for(size_t i=note_len;i>note_cursor;i--)note[i]=note[i-1];note[note_cursor++]='\n';note_len++;note_dirty=1;}return;}
    if(s==0x4B){if(note_cursor)note_cursor--;return;}if(s==0x4D){if(note_cursor<note_len)note_cursor++;return;}
    static const char lower[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0};
    if(s<128&&lower[s]&&note_len<NOTE_MAX){char c=lower[s];if(shift_down&&c>='a'&&c<='z')c=(char)(c-'a'+'A');if(shift_down&&c=='1')c='!';else if(shift_down&&c=='2')c='@';else if(shift_down&&c=='3')c='#';else if(shift_down&&c=='4')c='$';else if(shift_down&&c=='5')c='%';else if(shift_down&&c=='6')c='^';else if(shift_down&&c=='7')c='&';else if(shift_down&&c=='8')c='*';else if(shift_down&&c=='9')c='(';else if(shift_down&&c=='0')c=')';else if(shift_down&&c==';')c=':';else if(shift_down&&c=='\'')c='"';for(size_t i=note_len;i>note_cursor;i--)note[i]=note[i-1];note[note_cursor++]=(uint8_t)c;note_dirty=1;}
}
static void draw_editor(void){
    window_bar("TEXT EDITOR",note_dirty?"UNSAVED  F5 SAVES":"SAVED  F5 SAVES");
    fill_rect(38,110,(int)width-76,(int)height-214,bg_color());
    int x=52,y=126;size_t line=0;for(size_t i=0;i<note_len&&y<(int)height-116;i++){char c=(char)note[i];if(c=='\n'){line++;x=52;y=126+(int)line*18;continue;}glyph(x,y,c,text_color(),1);x+=6;if(x>(int)width-65){line++;x=52;y=126+(int)line*18;}}
    if(note_cursor==note_len&&y<(int)height-116)fill_rect(x,y,2,10,accent_color());
    text(40,(int)height-98,"ARROWS MOVE  BACKSPACE DELETE  ENTER NEWLINE  F5 SAVE",sub_color(),1);taskbar();
}

static void parse_browser_html(void);

static int boot_name_is_html(const STEVEOS_BOOT_FILE*f){
    if(!f)return 0;
    char n[96];file_name(f,n,sizeof(n));
    size_t z=0;while(n[z])z++;
    if(z>=5&&n[z-5]=='.'&&
       ((n[z-4]=='h'||n[z-4]=='H'))&&
       ((n[z-3]=='t'||n[z-3]=='T'))&&
       ((n[z-2]=='m'||n[z-2]=='M'))&&
       ((n[z-1]=='l'||n[z-1]=='L')))return 1;
    if(z>=4&&n[z-4]=='.'&&
       ((n[z-3]=='h'||n[z-3]=='H'))&&
       ((n[z-2]=='t'||n[z-2]=='T'))&&
       ((n[z-1]=='m'||n[z-1]=='M')))return 1;
    return 0;
}
static void browser_load_local_file(const STEVEOS_BOOT_FILE*f){
    if(!f||!f->data||!f->size)return;
    size_t n=(size_t)f->size;
    if(n>=BROWSER_RAW_MAX)n=BROWSER_RAW_MAX-1;
    const uint8_t*d=(const uint8_t*)(uintptr_t)f->data;
    for(size_t i=0;i<n;i++)browser_raw[i]=(char)d[i];
    browser_raw[n]=0;
    browser_raw_len=n;browser_image_url[0]=0;browser_image_alt[0]=0;browser_image_loaded=0;browser_image_len=0;
    parse_browser_html();
    char path[96];file_name(f,path,sizeof(path));
    const char*prefix="file:///";
    size_t p=0;while(prefix[p]&&p+1<BROWSER_URL_MAX){browser_url[p]=prefix[p];p++;}
    for(size_t i=0;path[i]&&p+1<BROWSER_URL_MAX;i++)browser_url[p++]=path[i];
    browser_url[p]=0;
    browser_status=200;browser_loaded=1;browser_focus=0;browser_scroll=0;current_app=APP_BROWSER;mark_dirty();
}

static const char*file_filter_name(void){
    const char*n[]={"ALL ITEMS","BOOT VOLUME","TEXT FILES","IMAGES","DIRECTORIES","OTHER FILES"};
    return n[file_filter<6?file_filter:0];
}
static int file_matches(const STEVEOS_BOOT_FILE*f){
    if(!f)return 0;
    if(file_filter==0||file_filter==1)return 1;
    if(file_filter==2)return f->kind==2;
    if(file_filter==3)return f->kind==1;
    if(file_filter==4)return (f->attributes&0x10)!=0;
    return f->kind==0;
}
static int filtered_count(void){
    int n=0;
    for(uint64_t i=0;i<boot_info->boot_file_count;i++)if(file_matches(&boot_files[i]))n++;
    return n;
}
static int visible_file_at(int visible,uint64_t*out){
    if(!out||visible<0)return 0;
    int n=0;
    for(uint64_t i=0;i<boot_info->boot_file_count;i++)if(file_matches(&boot_files[i])){
        if(n==visible){*out=i;return 1;}
        n++;
    }
    return 0;
}
static int selected_visible_index(void){
    if(selected_file<0)return -1;
    int n=0;
    for(uint64_t i=0;i<boot_info->boot_file_count;i++)if(file_matches(&boot_files[i])){
        if((int)i==selected_file)return n;
        n++;
    }
    return -1;
}
static void move_file_selection(int delta){
    int total=filtered_count();
    if(total<=0){selected_file=-1;file_scroll=0;return;}
    int cur=selected_visible_index();if(cur<0)cur=delta>0?0:total-1;else cur+=delta;
    if(cur<0)cur=0;if(cur>=total)cur=total-1;
    uint64_t idx=0;if(visible_file_at(cur,&idx))selected_file=(int)idx;
    if(cur<file_scroll)file_scroll=cur;
    if(cur>=file_scroll+10)file_scroll=cur-9;
}
static void draw_files(void){
    window_bar("FILE MANAGER",file_filter_name());
    fill_rect(38,110,220,(int)height-214,panel2_color());
    text(56,128,"PLACES / FILTERS",sub_color(),1);
    const char*places[]={"All Items","Boot Volume","Text Files","Images","Directories","Other Files"};
    for(int i=0;i<6;i++){fill_rect(50,150+i*42,190,34,(i==file_filter)?accent_dark():panel_color());draw_icon(54,141+i*42,i==0?3:i==1?3:i==2?9:i==3?11:i==4?3:15);text(112,161+i*42,places[i],i==file_filter?0xFFFFFFu:text_color(),1);}
    text(284,128,"NAME",sub_color(),1);text((int)width-240,128,"SIZE",sub_color(),1);
    int total=filtered_count(),shown=0;
    for(int row=0;row<10;row++){
        uint64_t i=0;if(!visible_file_at(file_scroll+row,&i))break;
        STEVEOS_BOOT_FILE*f=&boot_files[i];char name[64];file_name(f,name,sizeof(name));int y=150+shown*40;uint32_t cc=selected_file==(int)i?accent_dark():panel_color();
        fill_rect(278,y,(int)width-316,32,cc);text(294,y+10,name,selected_file==(int)i?0xFFFFFFu:text_color(),1);u64_text((int)width-236,y+10,f->size,sub_color(),1);text((int)width-150,y+10,(f->attributes&0x10)?"DIR":(f->kind==1?"IMG":(f->kind==2?"TXT":"FILE")),sub_color(),1);shown++;
    }
    text(284,(int)height-122,"VISIBLE",sub_color(),1);u64_text(342,(int)height-122,(uint64_t)total,text_color(),1);
    text(40,(int)height-98,"1-6 FILTERS  •  CLICK/ENTER OPEN TEXT OR IMAGE  •  UP/DOWN SCROLL",sub_color(),1);taskbar();
}
static uint16_t read16le(const uint8_t*d){return (uint16_t)d[0]|((uint16_t)d[1]<<8);}
static uint32_t read32le(const uint8_t*d){return (uint32_t)d[0]|((uint32_t)d[1]<<8)|((uint32_t)d[2]<<16)|((uint32_t)d[3]<<24);}
static void draw_bmp(const uint8_t*d,size_t len){
    if(len<54||d[0]!='B'||d[1]!='M')return;
    uint32_t off=read32le(d+10),w=read32le(d+18),hr=read32le(d+22);int32_t h=(int32_t)hr;uint16_t planes=read16le(d+26),bpp=read16le(d+28);if(!w||!h||planes!=1||(bpp!=24&&bpp!=32))return;uint32_t ah=(uint32_t)(h<0?-h:h),row=((w*bpp+31)/32)*4;if((uint64_t)off+(uint64_t)row*ah>len)return;
    uint32_t dw=520,dh=(uint64_t)ah*dw/w;if(dh>430){dh=430;dw=(uint64_t)w*dh/ah;}int ox=((int)width-(int)dw)/2,oy=112,bytes=bpp/8;for(uint32_t y=0;y<dh;y++){uint32_t sy=(uint64_t)y*ah/dh;if(h>0)sy=ah-1-sy;for(uint32_t x=0;x<dw;x++){uint32_t sx=(uint64_t)x*w/dw;const uint8_t*v=d+off+(uint64_t)sy*row+(uint64_t)sx*bytes;put_pixel(ox+(int)x,oy+(int)y,0xFF000000u|(uint32_t)v[2]|((uint32_t)v[1]<<8)|((uint32_t)v[0]<<16));}}
}
static void draw_builtin_image(void){
    const uint8_t*r=_binary_build_boot_raw_start,*e=_binary_build_boot_raw_end;if((size_t)(e-r)<8)return;uint32_t w=*(const uint32_t*)r,h=*(const uint32_t*)(r+4);const uint8_t*p=r+8;if(!w||!h||(size_t)w*h*4+8>(size_t)(e-r))return;uint32_t dw=520,dh=(uint64_t)h*dw/w;if(dh>430){dh=430;dw=(uint64_t)w*dh/h;}int ox=((int)width-(int)dw)/2,oy=112;for(uint32_t y=0;y<dh;y++){uint32_t sy=(uint64_t)y*h/dh;for(uint32_t x=0;x<dw;x++){uint32_t sx=(uint64_t)x*w/dw;const uint8_t*v=p+((size_t)sy*w+sx)*4;put_pixel(ox+(int)x,oy+(int)y,0xFF000000u|(uint32_t)v[2]|((uint32_t)v[1]<<8)|((uint32_t)v[0]<<16));}}
}
static void draw_image(void){
    window_bar("IMAGE VIEWER","BMP + BUILT-IN BOOT IMAGE");
    if(selected_file>=0&&boot_files&&(uint64_t)selected_file<boot_info->boot_file_count){STEVEOS_BOOT_FILE*f=&boot_files[selected_file];char n[64];file_name(f,n,sizeof(n));text(42,102,n,sub_color(),1);if(f->data&&f->size){const uint8_t*d=(const uint8_t*)(uintptr_t)f->data;if(f->size>=2&&d[0]=='B'&&d[1]=='M')draw_bmp(d,(size_t)f->size);else draw_builtin_image();}else draw_builtin_image();}else draw_builtin_image();
    text(40,(int)height-98,"ESC BACK TO FILES",sub_color(),1);taskbar();
}

static void status_text(char*out,size_t cap,uint32_t status){
    if(!out||cap<2){return;}
    if(!status){out[0]=0;return;}
    if(cap<9){out[0]=0;return;}
    out[0]='H';out[1]='T';out[2]='T';out[3]='P';out[4]=' ';
    out[5]=(char)('0'+(status/100)%10);out[6]=(char)('0'+(status/10)%10);out[7]=(char)('0'+status%10);out[8]=0;
}
static void append_text(char*out,size_t*len,size_t cap,char c){if(*len+1<cap){out[(*len)++]=c;out[*len]=0;}}
static void entity_char(const char*p,size_t n,char*out,size_t*len){
    if(n==4&&begins_ci(p,"amp;"))append_text(out,len,BROWSER_TEXT_MAX,'&');
    else if(n==3&&begins_ci(p,"lt;"))append_text(out,len,BROWSER_TEXT_MAX,'<');
    else if(n==3&&begins_ci(p,"gt;"))append_text(out,len,BROWSER_TEXT_MAX,'>');
    else if(n==5&&begins_ci(p,"quot;"))append_text(out,len,BROWSER_TEXT_MAX,'"');
    else if(n==5&&begins_ci(p,"nbsp;"))append_text(out,len,BROWSER_TEXT_MAX,' ');
    else append_text(out,len,BROWSER_TEXT_MAX,'&');
}
static void resolve_url(const char*href,char*out,size_t cap){
    if(!cap)return;
    out[0]=0;
    if(!href)return;
    while(*href==' '||*href=='\t'||*href=='\n')href++;
    size_t n=0;while(href[n]&&href[n]!='\"'&&href[n]!='\''&&href[n]!=' '&&href[n]!='\t'&&n+1<cap)n++;
    if(!n||href[0]=='#'||begins_ci(href,"javascript:")||begins_ci(href,"mailto:"))return;
    if(begins_ci(href,"http://")||begins_ci(href,"https://")){
        for(size_t i=0;i<n&&i+1<cap;i++)out[i]=href[i];
        out[n<cap?n:cap-1]=0;return;
    }
    const char*scheme=find_ci(browser_url,"://");
    size_t prefix=scheme?(size_t)(scheme-browser_url)+3:0;
    size_t authority=prefix;
    while(browser_url[authority]&&browser_url[authority]!='/'&&authority+1<BROWSER_URL_MAX)authority++;
    if(href[0]=='/'&&href[1]=='/'){
        const char*proto=(prefix>=3&&browser_url[4]=='s')?"https:":"http:";
        size_t p=0;while(proto[p]&&p+1<cap){out[p]=proto[p];p++;}
        for(size_t i=0;i<n&&p+1<cap;i++){out[p+i]=href[i];}
        p+=n;out[p<cap?p:cap-1]=0;return;
    }
    if(href[0]=='/'){
        size_t p=authority;
        for(size_t i=0;i<p&&i+1<cap;i++)out[i]=browser_url[i];
        for(size_t i=0;i<n&&p+i+1<cap;i++)out[p+i]=href[i];
        out[(p+n<cap)?p+n:cap-1]=0;return;
    }
    size_t base=authority;
    if(base&&browser_url[base-1]!='/'){
        size_t q=base;while(q>prefix&&browser_url[q-1]!='/')q--;base=q;
    }
    for(size_t i=0;i<base&&i+1<cap;i++)out[i]=browser_url[i];
    if(base&&out[base-1]!='/'&&base+1<cap)out[base++]='/';
    for(size_t i=0;i<n&&base+i+1<cap;i++)out[base+i]=href[i];
    out[(base+n<cap)?base+n:cap-1]=0;
}
static void parse_browser_html(void){
    browser_text[0]=0;
    browser_title[0]=0;
    browser_link_count=0;
    browser_image_url[0]=0;
    browser_image_alt[0]=0;
    size_t tl=0,i=0;
    int skip=0,in_a=0;
    size_t link_len=0;
    while(i<(size_t)BROWSER_RAW_MAX&&browser_raw[i]){
        if(browser_raw[i]=='<'){
            size_t j=i+1;
            while(browser_raw[j]&&browser_raw[j]!='>'&&j-i<300)j++;
            if(!browser_raw[j])break;
            const char*tag=browser_raw+i+1;
            while(*tag==' '||*tag=='/')tag++;
            char name[16];
            size_t tn=0;
            while(tag[tn]&&tag[tn]!=' '&&tag[tn]!='>'&&tag[tn]!='/'&&tn+1<sizeof(name)){
                char ch=tag[tn];
                if(ch>='A'&&ch<='Z')ch=(char)(ch-'A'+'a');
                name[tn++]=ch;
            }
            name[tn]=0;
            int closing=(browser_raw[i+1]=='/');
            if(ci_eq(name,"script")||ci_eq(name,"style"))skip=closing?0:1;
            if(ci_eq(name,"title")&&!closing){
                const char*q=browser_raw+j+1;
                const char*end=find_ci(q,"</title>");
                if(end){
                    size_t nn=0;
                    while(q<end&&nn+1<sizeof(browser_title)){
                        if(*q!='\n'&&*q!='\r')browser_title[nn++]=*q;
                        q++;
                    }
                    browser_title[nn]=0;
                }
            }
            if(ci_eq(name,"img")&&!closing&&!browser_image_url[0]){
                const char*sp=find_ci(tag,"src");
                if(sp&&sp<browser_raw+j){
                    sp+=3;
                    while(*sp==' '||*sp=='=')sp++;
                    if(*sp=='"'||*sp==39)sp++;
                    resolve_url(sp,browser_image_url,sizeof(browser_image_url));
                }
                const char*ap=find_ci(tag,"alt");
                if(ap&&ap<browser_raw+j){
                    ap+=3;
                    while(*ap==' '||*ap=='=')ap++;
                    if(*ap=='"'||*ap==39)ap++;
                    size_t an=0;
                    while(ap[an]&&ap[an]!='"'&&ap[an]!=39&&ap[an]!=' '&&ap[an]!='>'&&an+1<sizeof(browser_image_alt)){
                        browser_image_alt[an]=ap[an];
                        an++;
                    }
                    browser_image_alt[an]=0;
                }
            }
            if(ci_eq(name,"a")&&!closing&&browser_link_count<BROWSER_LINKS){
                const char*hp=find_ci(tag,"href");
                if(hp&&hp<browser_raw+j){
                    hp+=4;
                    while(*hp==' '||*hp=='=')hp++;
                    if(*hp=='"'||*hp==39)hp++;
                    char u[BROWSER_LINK_MAX+1];
                    resolve_url(hp,u,sizeof(u));
                    if(u[0]){
                        for(size_t k=0;k<BROWSER_URL_MAX&&u[k]&&k+1<BROWSER_URL_MAX;k++)
                            browser_link_urls[browser_link_count][k]=u[k],browser_link_urls[browser_link_count][k+1]=0;
                        in_a=1;
                        link_len=0;
                        browser_link_text[browser_link_count][0]=0;
                    }
                }
            }
            if(ci_eq(name,"a")&&closing){
                if(in_a){
                    in_a=0;
                    if(browser_link_count<BROWSER_LINKS){
                        if(!browser_link_text[browser_link_count][0]){
                            browser_link_text[browser_link_count][0]='L';
                            browser_link_text[browser_link_count][1]='I';
                            browser_link_text[browser_link_count][2]='N';
                            browser_link_text[browser_link_count][3]='K';
                            browser_link_text[browser_link_count][4]=0;
                        }
                        browser_link_count++;
                    }
                    link_len=0;
                }
            }
            if(!skip&&(ci_eq(name,"br")||ci_eq(name,"p")||ci_eq(name,"div")||ci_eq(name,"li")||ci_eq(name,"h1")||ci_eq(name,"h2")||ci_eq(name,"tr")||ci_eq(name,"hr")))
                append_text(browser_text,&tl,BROWSER_TEXT_MAX,'\n');
            i=j+1;
            continue;
        }
        if(skip){i++;continue;}
        if(browser_raw[i]=='&'){
            size_t j=i+1;
            while(browser_raw[j]&&browser_raw[j]!=';'&&j-i<12)j++;
            if(browser_raw[j]==';'){
                size_t before=tl;
                entity_char(browser_raw+i+1,j-i,browser_text,&tl);
                if(in_a&&browser_link_count<BROWSER_LINKS&&tl>before&&link_len+1<47){
                    browser_link_text[browser_link_count][link_len++]=browser_text[tl-1];
                    browser_link_text[browser_link_count][link_len]=0;
                }
                i=j+1;
                continue;
            }
        }
        char ch=browser_raw[i++];
        if(ch=='\r')continue;
        if(ch=='\n'){
            append_text(browser_text,&tl,BROWSER_TEXT_MAX,' ');
            if(in_a&&browser_link_count<BROWSER_LINKS&&link_len+1<47)browser_link_text[browser_link_count][link_len++]=' ';
            continue;
        }
        if(tl&&browser_text[tl-1]==' '&&ch==' ')continue;
        append_text(browser_text,&tl,BROWSER_TEXT_MAX,ch);
        if(in_a&&browser_link_count<BROWSER_LINKS&&link_len+1<47){
            browser_link_text[browser_link_count][link_len++]=ch;
            browser_link_text[browser_link_count][link_len]=0;
        }
    }
    if(!browser_title[0]){
        size_t n=0;
        for(size_t k=0;browser_text[k]&&n+1<sizeof(browser_title)&&k<120;k++){
            if(browser_text[k]!='\n'&&browser_text[k]!='\r')browser_title[n++]=browser_text[k];
        }
        browser_title[n]=0;
    }
}

static void browser_copy_url(char*out,const char*in){size_t n=0;while(in[n]&&n+1<BROWSER_URL_MAX){out[n]=in[n];n++;}out[n]=0;}
static void browser_history_visit(void){
    if(browser_history_lock){browser_history_lock=0;return;}
    if(browser_history_count&&ci_eq(browser_history[browser_history_pos],browser_url))return;
    if(browser_history_count&&browser_history_pos+1<browser_history_count)browser_history_count=(uint8_t)(browser_history_pos+1);
    if(browser_history_count>=8){for(int i=1;i<8;i++)browser_copy_url(browser_history[i-1],browser_history[i]);browser_history_count=7;}
    browser_copy_url(browser_history[browser_history_count],browser_url);
    browser_history_pos=browser_history_count;
    browser_history_count++;
}
static void browser_load_first_image(void){
    browser_image_loaded=0;browser_image_len=0;
    if(!browser_image_url[0]||!(begins_ci(browser_image_url,"http://")||begins_ci(browser_image_url,"https://"))||!boot_info->uefi_http_get)return;
    HTTPGET get=(HTTPGET)(uintptr_t)boot_info->uefi_http_get;
    uint16_t u16[BROWSER_URL_MAX+2];size_t n=0;
    while(browser_image_url[n]&&n+1<sizeof(u16)/sizeof(u16[0])){u16[n]=(uint16_t)(unsigned char)browser_image_url[n];n++;}u16[n]=0;
    uint64_t len=0;uint32_t image_status=0;uint64_t st=get(u16,browser_image_raw,sizeof(browser_image_raw)-1,&len,&image_status);
    if(st==0&&len>=54&&len<=sizeof(browser_image_raw)-1&&browser_image_raw[0]=='B'&&browser_image_raw[1]=='M'){browser_image_len=(size_t)len;browser_image_loaded=1;}
}
static void draw_browser_image(void){
    if(!browser_image_loaded||browser_image_len<54)return;
    const uint8_t*d=(const uint8_t*)browser_image_raw;uint32_t off=read32le(d+10),w=read32le(d+18),hr=read32le(d+22);int32_t h=(int32_t)hr;uint16_t planes=read16le(d+26),bpp=read16le(d+28);
    if(!w||!h||planes!=1||(bpp!=24&&bpp!=32))return;
    uint32_t ah=(uint32_t)(h<0?-h:h),row=((w*bpp+31)/32)*4;
    if((uint64_t)off+(uint64_t)row*ah>browser_image_len)return;
    uint32_t dw=220,dh=(uint64_t)ah*dw/w;if(dh>150){dh=150;dw=(uint64_t)w*dh/ah;}
    int ox=(int)width-270,oy=205,bytes=bpp/8;
    fill_rect(ox-8,oy-8,dw+16,dh+16,bg_color());
    for(uint32_t y=0;y<dh;y++){uint32_t sy=(uint64_t)y*ah/dh;if(h>0)sy=ah-1-sy;for(uint32_t x=0;x<dw;x++){uint32_t sx=(uint64_t)x*w/dw;const uint8_t*v=d+off+(uint64_t)sy*row+(uint64_t)sx*bytes;put_pixel(ox+(int)x,oy+(int)y,0xFF000000u|(uint32_t)v[2]|((uint32_t)v[1]<<8)|((uint32_t)v[0]<<16));}}
}
static int browser_fetch(void){
    HTTPGET get=(HTTPGET)(uintptr_t)boot_info->uefi_http_get;
    if(!get){browser_status=0;browser_loaded=0;return 0;}
    if(browser_url[0]&&!begins_ci(browser_url,"http://")&&!begins_ci(browser_url,"https://")&&!begins_ci(browser_url,"file:///")){
        char tmp[BROWSER_URL_MAX+1];size_t p=0;const char*proto="http://";
        while(proto[p]&&p+1<BROWSER_URL_MAX){tmp[p]=proto[p];p++;}
        for(size_t i=0;browser_url[i]&&p+1<BROWSER_URL_MAX;i++)tmp[p++]=browser_url[i];
        tmp[p]=0;browser_copy_url(browser_url,tmp);
    }
    if(begins_ci(browser_url,"file:///"))return 0;
    uint16_t u16[BROWSER_URL_MAX+2];size_t n=0;while(browser_url[n]&&n+1<sizeof(u16)/sizeof(u16[0])){u16[n]=(uint16_t)(unsigned char)browser_url[n];n++;}u16[n]=0;
    uint64_t len=0;browser_raw[0]=0;browser_status=0;
    uint64_t st=get(u16,browser_raw,BROWSER_RAW_MAX-1,&len,&browser_status);
    if(st!=0||!len){browser_loaded=0;return 0;}
    if(len>=BROWSER_RAW_MAX)len=BROWSER_RAW_MAX-1;
    browser_raw[len]=0;browser_raw_len=(size_t)len;browser_image_url[0]=0;browser_image_alt[0]=0;parse_browser_html();browser_load_first_image();browser_loaded=1;browser_scroll=0;
    browser_copy_url(browser_tabs[browser_current_tab],browser_url);
    browser_history_visit();
    return 1;
}
static void browser_new_tab(void){
    if(browser_tab_count<4){browser_current_tab=browser_tab_count++;browser_tabs[browser_current_tab][0]=0;}
    else{browser_current_tab=(uint8_t)((browser_current_tab+1)&3u);browser_tabs[browser_current_tab][0]=0;}
    browser_url[0]=0;browser_raw[0]=0;browser_text[0]=0;browser_title[0]=0;browser_link_count=0;browser_status=0;browser_loaded=0;browser_scroll=0;browser_focus=1;mark_dirty();
}
static void browser_tab_switch(int index){
    if(index<0||index>=browser_tab_count)return;
    if(index==browser_current_tab)return;
    browser_current_tab=(uint8_t)index;
    browser_copy_url(browser_url,browser_tabs[browser_current_tab]);
    browser_focus=browser_url[0]?0:1;
    browser_raw[0]=0;browser_text[0]=0;browser_title[0]=0;browser_link_count=0;browser_loaded=0;browser_scroll=0;browser_status=0;
    if(browser_url[0]){browser_history_lock=1;browser_fetch();}
    mark_dirty();
}
static void browser_close_tab(void){
    if(browser_tab_count<=1){browser_url[0]=0;browser_loaded=0;browser_focus=1;mark_dirty();return;}
    for(int i=browser_current_tab;i+1<browser_tab_count;i++)browser_copy_url(browser_tabs[i],browser_tabs[i+1]);
    browser_tab_count--;if(browser_current_tab>=browser_tab_count)browser_current_tab=(uint8_t)(browser_tab_count-1);
    browser_copy_url(browser_url,browser_tabs[browser_current_tab]);
    browser_focus=browser_url[0]?0:1;browser_loaded=0;browser_raw[0]=0;browser_text[0]=0;browser_title[0]=0;browser_link_count=0;browser_status=0;browser_scroll=0;
    if(browser_url[0]){browser_history_lock=1;browser_fetch();}
    mark_dirty();
}
static void browser_save_page(void){
    if(!browser_loaded||!browser_raw[0]||!boot_info->uefi_write_text)return;
    WRITEFILE write=(WRITEFILE)(uintptr_t)boot_info->uefi_write_text;
    write((const uint16_t*)page_file_name,browser_raw,(uint64_t)browser_raw_len);
    terminal_add("PAGE SAVED AS STEVEOSPAGE.HTM");
}

static void browser_history_move(int direction){
    if(!browser_history_count)return;
    int next=(int)browser_history_pos+direction;
    if(next<0||next>=(int)browser_history_count)return;
    browser_history_pos=(uint8_t)next;
    browser_copy_url(browser_url,browser_history[browser_history_pos]);
    browser_history_lock=1;
    browser_fetch();
}
static void draw_browser(void){
    window_bar("WEB BROWSER",browser_focus?"ADDRESS ACTIVE  ENTER LOAD  ESC HOME":"UP DOWN SCROLL  1-8 LINKS");
    int field_w=(int)width-370;
    fill_rect(34,106,field_w,40,bg_color());stroke_rect(34,106,field_w,40,browser_focus?accent_color():panel2_color());
    if(browser_url[0])text_clip(46,117,browser_url,text_color(),1,field_w-24);else text(46,117,"TYPE URL THEN ENTER",sub_color(),1);
    int bx=(int)width-326;
    for(int i=0;i<browser_tab_count;i++){int tx=38+i*120,ty=152;fill_rect(tx,ty,110,28,i==browser_current_tab?accent_dark():panel2_color());char tn[8]={'T',(char)('1'+i),0};text(tx+10,ty+8,tn,i==browser_current_tab?0xFFFFFFu:text_color(),1);if(browser_tabs[i][0])text(tx+32,ty+8,"OPEN",sub_color(),1);}

    const char*bn[]={"B","F","R"};
    for(int i=0;i<3;i++){fill_rect(bx+i*56,106,50,40,panel2_color());text(bx+19+i*56,117,bn[i],i==2?accent_color():text_color(),1);}
    char st[24];status_text(st,sizeof(st),browser_status);if(browser_status){text((int)width-92,118,st,good_color(),1);}
    if(!browser_loaded){text(52,180,boot_info->uefi_http_get?"READY TO FETCH HTTP/HTTPS CONTENT":"FIRMWARE HTTP BRIDGE UNAVAILABLE",text_color(),2);text(52,216,"TYPE A DOMAIN SUCH AS HTTP://NEVERSL.COM/",sub_color(),1);text(52,246,"THE PAGE IS FETCHED BY THE UEFI NETWORK STACK.",sub_color(),1);}
    else{
        fill_rect(34,184,(int)width-310,(int)height-310,bg_color());text_clip(52,196,browser_title[0]?browser_title:"UNTITLED",accent_color(),2,(int)width-350);
        int x=52,y=232,lines=0;const char*p=browser_text;int skip=browser_scroll;while(*p&&skip>0){if(*p=='\n')skip--;p++;}while(*p&&lines<((int)height-330)/16){int used=0;while(*p&&*p!='\n'&&used<((int)width-350)/6){glyph(x+used*6,y+lines*16,*p,text_color(),1);used++;p++;}while(*p&&*p!='\n')p++;if(*p=='\n')p++;lines++;}
        fill_rect((int)width-286,184,252,(int)height-310,panel2_color());text((int)width-270,202,"LINKS",sub_color(),1);
        if(browser_image_loaded){draw_browser_image();if(browser_image_alt[0])text((int)width-270,372,browser_image_alt,text_color(),1);}
        for(int i=0;i<browser_link_count;i++){int yy=228+i*42;fill_rect((int)width-270,yy,220,32,panel_color());char num[2]={(char)('1'+i),0};text((int)width-258,yy+10,num,accent_color(),1);text_clip((int)width-238,yy+10,browser_link_text[i],text_color(),1,178);}
    }
    text(40,(int)height-98,"HTTP BROWSER  •  CTRL+L ADDRESS  •  CTRL+D SAVE BOOKMARK  •  CTRL+B OPEN BOOKMARK",sub_color(),1);taskbar();
}

static int server_runtime_detected(void){
    if(!boot_files||!boot_info)return 0;
    for(uint64_t i=0;i<boot_info->boot_file_count;i++){
        char n[96];file_name(&boot_files[i],n,sizeof(n));
        if(terminal_file_match(n,"SteveOS/Server/ServerBoot.efi"))return 1;
    }
    return 0;
}
static void open_server_config(void){
    if(!boot_files||!boot_info)return;
    for(uint64_t i=0;i<boot_info->boot_file_count;i++){
        STEVEOS_BOOT_FILE*f=&boot_files[i];char n[96];file_name(f,n,sizeof(n));
        if(terminal_file_match(n,"SteveOS/Server/server.conf")){selected_file=(int)i;if(f->data&&f->size)load_text_file(f);return;}
    }
    current_app=APP_FILES;file_filter=0;mark_dirty();
}
static void terminal_add(const char*s){if(terminal_count<TERM_LINES){size_t i=0;while(s[i]&&i<63){terminal_lines[terminal_count][i]=s[i];i++;}terminal_lines[terminal_count][i]=0;terminal_count++;}else{for(int r=1;r<TERM_LINES;r++)for(int c=0;c<64;c++)terminal_lines[r-1][c]=terminal_lines[r][c];size_t i=0;while(s[i]&&i<63){terminal_lines[TERM_LINES-1][i]=s[i];i++;}terminal_lines[TERM_LINES-1][i]=0;}}
static int terminal_file_match(const char*a,const char*b){
    while(*a&&*b){
        char x=*a,y=*b;
        if(x>='a'&&x<='z')x=(char)(x-'a'+'A');
        if(y>='a'&&y<='z')y=(char)(y-'a'+'A');
        if(x!=y)return 0;
        a++;b++;
    }
    return *a==0;
}
static void terminal_open_file(const char*path){
    for(uint64_t i=0;i<boot_info->boot_file_count;i++){
        STEVEOS_BOOT_FILE*f=&boot_files[i];char name[96];file_name(f,name,sizeof(name));
        if(terminal_file_match(name,path)){
            selected_file=(int)i;
            if(f->kind==1)current_app=APP_IMAGE;
            else if(f->kind==4){
                if(boot_info->uefi_run_windows_app){
                    RUNWINDOWSAPP fn=(RUNWINDOWSAPP)(uintptr_t)boot_info->uefi_run_windows_app;
                    fn(f->name);
                }
            }else if(f->kind==4){if(boot_info->uefi_run_windows_app){RUNWINDOWSAPP fn=(RUNWINDOWSAPP)(uintptr_t)boot_info->uefi_run_windows_app;fn(f->name);}}else if(f->kind==2&&f->data){if(boot_name_is_html(f))browser_load_local_file(f);else load_text_file(f);}
            else current_app=APP_FILES;
            mark_dirty();
            return;
        }
    }
    terminal_add("FILE NOT FOUND");
}
static void terminal_list(void){
    if(!boot_files||!boot_info)return;
    uint64_t start=0,end=boot_info->boot_file_count;
    if(end>6)end=6;
    for(uint64_t i=start;i<end;i++){char name[96];file_name(&boot_files[i],name,sizeof(name));terminal_add(name);}
    if(boot_info->boot_file_count>6)terminal_add("... USE FILE MANAGER FOR MORE");
}

static void terminal_net_test(void){
    if(!boot_info->uefi_http_get){terminal_add("NO UEFI HTTP SERVICE");return;}
    HTTPGET get=(HTTPGET)(uintptr_t)boot_info->uefi_http_get;
    const char*plain="http://example.com/";
    uint16_t url[32];size_t n=0;while(plain[n]&&n+1<sizeof(url)/sizeof(url[0])){url[n]=(uint16_t)(unsigned char)plain[n];n++;}url[n]=0;
    char out[64];uint64_t len=0;uint32_t status=0;uint64_t st=get(url,out,sizeof(out)-1,&len,&status);
    if(st==0)terminal_add(status>=200&&status<500?"INTERNET TEST PASSED":"HTTP SERVER REPLIED WITH ERROR");
    else terminal_add("INTERNET TEST FAILED");
}
static void terminal_disks(void){
    refresh_install_targets();
    if(!install_target_count){terminal_add("NO OTHER EFI VOLUMES FOUND");return;}
    for(uint64_t i=0;i<install_target_count&&i<8;i++){terminal_add(install_targets[i].removable?"REMOVABLE VOLUME":"INTERNAL VOLUME");}
}
static void terminal_apps(void){
    scan_app_packages();
    if(!app_package_count){terminal_add("NO APP PACKAGES IN \\Apps");return;}
    for(int i=0;i<app_package_count;i++){char name[80];file_name(&boot_files[app_package_indices[i]],name,sizeof(name));terminal_add(name);}
}

static int parse_small_number(const char*s){
    int v=0;int seen=0;
    while(s&&*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;seen=1;if(v>99)v=99;}
    return seen?v:-1;
}
static void terminal_app_action(const char*cmd,int run){
    int n=parse_small_number(cmd);
    scan_app_packages();
    if(n<0||n>=app_package_count){terminal_add("BAD APP INDEX");return;}
    app_package_pick=n;
    if(run)app_launch_selected();else app_install_selected();
}
static void terminal_services(void){
    terminal_add((service_flags&1)?"DISCORD: ARMED":"DISCORD: OFF");
    terminal_add((service_flags&2)?"TAILSCALE: ARMED":"TAILSCALE: OFF");
    terminal_add((service_flags&4)?"REMOTE SHELL: ARMED":"REMOTE SHELL: OFF");
}
static void terminal_service_set(const char*name,int bit,int on){
    if(str_eq(name,"DISCORD")){if(on)service_flags|=1;else service_flags&=(uint8_t)~1u;}
    else if(str_eq(name,"TAILSCALE")){if(on)service_flags|=2;else service_flags&=(uint8_t)~2u;}
    else if(str_eq(name,"SSH")){if(on)service_flags|=4;else service_flags&=(uint8_t)~4u;}
    else {terminal_add("UNKNOWN SERVICE");return;}
    save_settings();
    terminal_add(on?"SERVICE ARMED":"SERVICE DISABLED");
    (void)bit;
}
static void terminal_server_boot(void){
    if(!boot_info->uefi_launch_server){terminal_add("SERVER BOOT BRIDGE UNAVAILABLE");return;}
    if(!server_runtime_detected()){terminal_add("SERVER RUNTIME NOT INSTALLED");return;}
    LAUNCHSERVER fn=(LAUNCHSERVER)(uintptr_t)boot_info->uefi_launch_server;
    fn();
}
static void terminal_exes(void){
    int shown=0;
    for(uint64_t i=0;boot_info&&i<boot_info->boot_file_count&&shown<8;i++)
        if(boot_files[i].kind==4){
            char name[80];file_name(&boot_files[i],name,sizeof(name));terminal_add(name);shown++;
        }
    if(!shown)terminal_add("NO EXE FILES");
}
static void terminal_run_exe(const char*name){
    if(!name||!name[0]||!boot_info->uefi_run_windows_app){terminal_add("EXE RUNTIME UNAVAILABLE");return;}
    uint16_t path[STEVEOS_BOOT_FILE_NAME_MAX];size_t p=0;
    const char*prefix="\\SteveOS\\Apps\\";
    for(size_t i=0;prefix[i]&&p+1<sizeof(path)/sizeof(path[0]);i++)path[p++]=(uint16_t)(unsigned char)prefix[i];
    for(size_t i=0;name[i]&&p+1<sizeof(path)/sizeof(path[0]);i++)path[p++]=(uint16_t)(unsigned char)name[i];
    path[p]=0;
    RUNWINDOWSAPP fn=(RUNWINDOWSAPP)(uintptr_t)boot_info->uefi_run_windows_app;
    uint64_t st=fn(path);
    terminal_add(st==0?"STARTING WINDOWS APP":"WINDOWS APP FAILED");
}
static void terminal_exec(void){
    terminal_input[terminal_len]=0;
    if(str_eq(terminal_input,"HELP"))terminal_add("HELP LS OPEN MEM NET NETTEST DISKS APPS EXES APP INSTALL APPGET RUNEXE STATUS SERVICES SERVICE SERVER SERVER BOOT SERVER CONFIG STORE INSTALL ADVANCED SYSINFO VERSION BROWSE REFRESH CLEAR REBOOT HALT DATE");
    else if(str_eq(terminal_input,"LS"))terminal_list();
    else if(begins_ci(terminal_input,"OPEN ")){size_t i=5;while(terminal_input[i]==' ')i++;terminal_open_file(terminal_input+i);}
    else if(str_eq(terminal_input,"MEM"))terminal_add("OPEN TASK MANAGER FOR LIVE MEMORY DETAILS");
    else if(str_eq(terminal_input,"NET")){refresh_network_info();if(network_info_valid)terminal_add(network_info.media_present?"NETWORK LINK PRESENT":"NETWORK LINK DOWN");else terminal_add("NETWORK INFO UNAVAILABLE");}
    else if(str_eq(terminal_input,"NETTEST"))terminal_net_test();
    else if(str_eq(terminal_input,"DISKS"))terminal_disks();
    else if(str_eq(terminal_input,"APPS"))terminal_apps();
    else if(str_eq(terminal_input,"EXES"))terminal_exes();
    else if(begins_ci(terminal_input,"RUNEXE ")){size_t i=7;while(terminal_input[i]==' ')i++;terminal_run_exe(terminal_input+i);}
    else if(begins_ci(terminal_input,"APP INSTALL ")){terminal_app_action(terminal_input+12,0);}
    else if(begins_ci(terminal_input,"APP RUN ")){terminal_app_action(terminal_input+8,1);}
    else if(begins_ci(terminal_input,"APPGET ")){size_t i=7,n=0;app_download_url[0]=0;while(terminal_input[i]&&n+1<BROWSER_URL_MAX)app_download_url[n++]=terminal_input[i++];app_download_url[n]=0;current_app=APP_STORE;app_download_focus=1;mark_dirty();}
    else if(str_eq(terminal_input,"STATUS")){refresh_network_info();terminal_add(network_info_valid?(network_info.media_present?"NET LINK UP":"NET LINK DOWN"):"NET INFO OFF");terminal_services();}
    else if(str_eq(terminal_input,"SERVICES"))terminal_services();
    else if(str_eq(terminal_input,"SERVICE DISCORD ON"))terminal_service_set("DISCORD",1,1);
    else if(str_eq(terminal_input,"SERVICE DISCORD OFF"))terminal_service_set("DISCORD",1,0);
    else if(str_eq(terminal_input,"SERVICE TAILSCALE ON"))terminal_service_set("TAILSCALE",2,1);
    else if(str_eq(terminal_input,"SERVICE TAILSCALE OFF"))terminal_service_set("TAILSCALE",2,0);
    else if(str_eq(terminal_input,"SERVICE SSH ON"))terminal_service_set("SSH",4,1);
    else if(str_eq(terminal_input,"SERVICE SSH OFF"))terminal_service_set("SSH",4,0);
    else if(str_eq(terminal_input,"SERVER BOOT"))terminal_server_boot();
    else if(str_eq(terminal_input,"SERVER CONFIG"))open_server_config();
    else if(str_eq(terminal_input,"UPDATE")){refresh_update_info();terminal_add(update_info.available?"UPDATE READY":"NO NEW UPDATE");}
    else if(str_eq(terminal_input,"UPDATE CHECK")){refresh_update_info();terminal_add(update_info.available?"UPDATE READY":"NO NEW UPDATE");}
    else if(str_eq(terminal_input,"UPDATE INSTALL")){apply_system_update();}
    else if(str_eq(terminal_input,"COMPAT")){terminal_add("WINDOWS EXE: WINE + Xvfb IN SERVER MODE");terminal_add("LINUX: ALPINE USERSpace + LTS KERNEL");terminal_add("MEDIA: FFMPEG + GSTREAMER + ALSA + PIPEWIRE");terminal_add("CAMERA: V4L2 DEVICE PASSTHROUGH");terminal_add("MIC: ALSA/PIPEWIRE DEVICE PASSTHROUGH");}
    else if(str_eq(terminal_input,"MEDIA")){terminal_add("MEDIA STACK: FFMPEG/GSTREAMER/ALSA/PIPEWIRE/V4L2");terminal_add("SERVER MODE EXPOSES /dev/snd AND /dev/video*");}
    else if(str_eq(terminal_input,"CAMERA")){terminal_add("CAMERA USES LINUX V4L2 IN SERVER MODE");terminal_add("CHECK /dev/video* WHEN SERVER MODE IS RUNNING");}
    else if(str_eq(terminal_input,"MIC")){terminal_add("MIC USES ALSA/PIPEWIRE IN SERVER MODE");terminal_add("CHECK /dev/snd WHEN SERVER MODE IS RUNNING");}
    else if(str_eq(terminal_input,"STORE")){current_app=APP_STORE;scan_app_packages();mark_dirty();}
    else if(str_eq(terminal_input,"INSTALL")){current_app=APP_INSTALLER;refresh_install_targets();mark_dirty();}
    else if(str_eq(terminal_input,"SERVER")){current_app=APP_SERVER;refresh_network_info();mark_dirty();}
    else if(str_eq(terminal_input,"ADVANCED")){current_app=APP_ADVANCED;refresh_network_info();mark_dirty();}
    else if(str_eq(terminal_input,"SYSINFO")){current_app=APP_SYSINFO;mark_dirty();}
    else if(str_eq(terminal_input,"VERSION"))terminal_add("STEVEOS NATIVE DESKTOP 1.0+");
    else if(str_eq(terminal_input,"DATE")){current_app=APP_CALENDAR;mark_dirty();}
    else if(str_eq(terminal_input,"BROWSE")){current_app=APP_BROWSER;browser_focus=1;mark_dirty();}
    else if(begins_ci(terminal_input,"BROWSE ")){size_t i=7;while(terminal_input[i]==' ')i++;size_t n=0;browser_url[0]=0;if(!begins_ci(terminal_input+i,"http://")&&!begins_ci(terminal_input+i,"https://")){const char*p="http://";while(*p)browser_url[n++]=*p++;}while(terminal_input[i]&&n+1<BROWSER_URL_MAX)browser_url[n++]=terminal_input[i++];browser_url[n]=0;current_app=APP_BROWSER;browser_focus=0;browser_fetch();mark_dirty();}
    else if(str_eq(terminal_input,"REFRESH")){if(current_app==APP_BROWSER&&browser_url[0])browser_fetch();else mark_dirty();}
    else if(str_eq(terminal_input,"CLEAR"))terminal_count=0;
    else if(str_eq(terminal_input,"REBOOT")||str_eq(terminal_input,"RESTART"))native_reboot();
    else if(str_eq(terminal_input,"HALT")||str_eq(terminal_input,"SHUTDOWN"))native_halt();
    else if(terminal_len)terminal_add("UNKNOWN COMMAND");
    terminal_len=0;terminal_input[0]=0;
}
static void draw_terminal(void){window_bar("TERMINAL","NATIVE SHELL  TYPE HELP");for(int i=0;i<TERM_LINES;i++)text(42,116+i*21,terminal_lines[i],text_color(),1);fill_rect(38,(int)height-150,(int)width-76,34,panel2_color());text(48,(int)height-141,">",accent_color(),1);text(62,(int)height-141,terminal_input,text_color(),1);text(40,(int)height-98,"ENTER RUNS COMMAND  BACKSPACE EDITS",sub_color(),1);taskbar();}

static void draw_tasks(void){
    window_bar("TASK MANAGER","SYSTEM MONITOR");text(42,116,"PROCESS",sub_color(),1);text(250,116,"STATUS",sub_color(),1);text(390,116,"MEMORY",sub_color(),1);text(540,116,"SOURCE",sub_color(),1);
    const char*n[]={"kernel-main","desktop-shell","framebuffer","keyboard","usb-hid","i2c-hid","firmware-http","nvram","boot-files"};
    const char*s[]={"RUNNING","RUNNING","RUNNING", "RUNNING",native_usb_mouse_present()?"ACTIVE":"OFF",native_i2c_hid_present()?"ACTIVE":"OFF",boot_info->uefi_http_get?"READY":"OFF","READY","SNAPSHOT"};
    for(int i=0;i<9;i++){int y=132+i*34;fill_rect(40,y,(int)width-80,26,(i%2)?panel_color():bg_color());text(52,y+8,n[i],text_color(),1);text(250,y+8,s[i],(i==4&&native_usb_mouse_present())||i==6?good_color():sub_color(),1);fill_rect(390,y+6,90,12,panel2_color());fill_rect(390,y+6,(i==1?72:i==0?64:i==2?38:18),12,accent_color());text(540,y+8,i==6?"HTTP BRIDGE":i==5?"TOUCHPAD":"NATIVE",sub_color(),1);}
    fill_rect(40,456,(int)width-80,70,panel2_color());text(56,472,"CONVENTIONAL MEMORY",sub_color(),1);u64_text(56,493,total_memory/1024/1024,text_color(),2);text(118,496,"MB",sub_color(),1);text(220,472,"LARGEST FREE REGION",sub_color(),1);u64_text(220,493,largest_region/1024/1024,text_color(),2);text(282,496,"MB",sub_color(),1);text(40,(int)height-98,"LIVE COMPONENT VIEW  •  INPUT DRIVERS  •  NETWORK BRIDGE",sub_color(),1);taskbar();
}

static void draw_settings(void){
    window_bar("SYSTEM SETTINGS","F5 SAVES SETTINGS");text(42,116,"PERSONALIZATION",accent_color(),1);
    fill_rect(42,136,(int)width-84,54,light_theme?panel2_color():panel_color());text(58,154,"THEME",text_color(),1);text((int)width-190,154,light_theme?"LIGHT":"DARK",accent_color(),1);
    fill_rect(42,202,(int)width-84,54,panel_color());text(58,220,"POINTER SCALE",text_color(),1);u64_text((int)width-190,220,pointer_scale,accent_color(),1);text(58,238,"1 TO 4",sub_color(),1);
    text(42,290,"ACCENT",accent_color(),1);fill_rect(42,308,(int)width-84,54,panel_color());text(58,326,"ACCENT PRESET",text_color(),1);u64_text((int)width-190,326,(uint64_t)(accent_id+1),accent_color(),1);text((int)width-155,326,"1-4",sub_color(),1);
    text(42,388,"INPUT",accent_color(),1);fill_rect(42,406,(int)width-84,92,panel_color());text(58,424,native_usb_mouse_present()?"USB HID MOUSE ACTIVE":"USB HID MOUSE OFF",text_color(),1);text(58,446,native_i2c_hid_present()?"I2C HID TOUCHPAD ACTIVE":"I2C HID TOUCHPAD OFF",sub_color(),1);text(58,468,"NATIVE PS/2 KEYBOARD ACTIVE",sub_color(),1);
    text(42,520,"NETWORK",accent_color(),1);fill_rect(42,538,(int)width-84,78,panel_color());text(58,556,boot_info->uefi_http_get?"UEFI HTTP SERVICE AVAILABLE":"UEFI HTTP SERVICE UNAVAILABLE",text_color(),1);text(58,578,"BROWSER USES FIRMWARE DNS + HTTP STACK",sub_color(),1);
    text(42,(int)height-98,"CLICK THEME/POINTER/ACCENT ROWS  •  F5 SAVE TO NVRAM",sub_color(),1);taskbar();
}

static int leap_year(int y){return (y%4==0&&y%100!=0)||(y%400==0);}
static int month_days(int y,int m){
    static const int d[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(m==2)return d[m-1]+(leap_year(y)?1:0);
    return d[m-1];
}
static int weekday_monday0(int y,int m,int d){
    static const int t[]={0,3,2,5,0,3,5,1,4,6,2,4};
    if(m<3)y--;
    return (y+y/4-y/100+y/400+t[m-1]+d+6)%7;
}
static void draw_calendar(void){
    window_bar("CALENDAR","FIRMWARE REAL-TIME CLOCK");
    EFI_TIME_STEV t={0};GETTIME gt=(GETTIME)(uintptr_t)boot_info->uefi_get_time;
    if(gt)gt(&t,NULL);
    if(!t.year){text(54,132,"FIRMWARE CLOCK UNAVAILABLE",danger_color(),2);taskbar();return;}
    char date[32];date[0]=(char)('0'+(t.day/10));date[1]=(char)('0'+t.day%10);date[2]='/';date[3]=(char)('0'+(t.month/10));date[4]=(char)('0'+t.month%10);date[5]='/';date[6]=(char)('0'+(t.year/1000)%10);date[7]=(char)('0'+(t.year/100)%10);date[8]=(char)('0'+(t.year/10)%10);date[9]=(char)('0'+t.year%10);date[10]=0;
    char time[16];time[0]=(char)('0'+(t.hour/10));time[1]=(char)('0'+t.hour%10);time[2]=':';time[3]=(char)('0'+(t.minute/10));time[4]=(char)('0'+t.minute%10);time[5]=':';time[6]=(char)('0'+(t.second/10));time[7]=(char)('0'+t.second%10);time[8]=0;
    text(54,122,date,text_color(),2);text(210,122,time,accent_color(),2);text(54,158,"CURRENT FIRMWARE DATE AND TIME",sub_color(),1);
    const char*days[]={"MON","TUE","WED","THU","FRI","SAT","SUN"};
    int cellw=(int)width>750?74:((int)width-92)/7;
    int cellh=(int)height>720?42:34;
    int x0=46,y0=202,first=weekday_monday0((int)t.year,(int)t.month,1),days_in=month_days((int)t.year,(int)t.month);
    for(int i=0;i<7;i++)text(x0+i*cellw+12,y0,days[i],sub_color(),1);
    for(int slot=0;slot<42;slot++){
        int num=slot-first+1;if(num<1||num>days_in)continue;
        int col=slot%7,row=slot/7,x=x0+col*cellw,y=y0+22+row*cellh;
        fill_rect(x,y,58,cellh-6,num==t.day?accent_dark():panel2_color());
        u64_text(x+21,y+8,(uint64_t)num,num==t.day?0xFFFFFFu:text_color(),1);
    }
    taskbar();
}

static void draw_control(void){
    window_bar("CONTROL CENTER","HARDWARE + SERVICES");
    const char*names[]={"Display","Input Devices","Network","Storage","Memory","Firmware","Boot Volume","Open-Source Components","Server Runtime"};
    for(int i=0;i<9;i++){
        int col=i%2,row=i/2,x=42+col*300,y=114+row*76;
        fill_rect(x,y,280,60,panel_color());
        text(x+18,y+14,names[i],text_color(),1);
        if(i==0)text(x+18,y+35,"UEFI GOP FRAMEBUFFER",sub_color(),1);
        else if(i==1)text(x+18,y+35,native_usb_mouse_present()?"USB MOUSE ACTIVE":"PS2/TOUCHPAD INPUT",sub_color(),1);
        else if(i==2)text(x+18,y+35,network_info_valid?(network_info.media_present?"LINK PRESENT":"NO LINK"):"NO NETWORK INFO",sub_color(),1);
        else if(i==3)text(x+18,y+35,"BOOT FILE SNAPSHOT",sub_color(),1);
        else if(i==4)text(x+18,y+35,"RAM MAP AVAILABLE",sub_color(),1);
        else if(i==5)text(x+18,y+35,"RUNTIME SERVICES",sub_color(),1);
        else if(i==6)text(x+18,y+35,"FAT BOOT VOLUME",sub_color(),1);
        else if(i==7)text(x+18,y+35,"MINT-Y + CINNAMON REFERENCES",sub_color(),1);
        else text(x+18,y+35,"DISCORD / TAILSCALE RUNTIME",sub_color(),1);
        if(i==2&&network_info_valid){char mac[24];mac_text(mac,sizeof(mac));text(x+18,y+50,mac,accent_color(),1);}
    }
    text(42,(int)height-98,"NETWORK INFO SHOWS FIRMWARE LINK + MAC  •  SERVER RUNTIME STATUS",sub_color(),1);
    taskbar();
}

static void cpuid_native(uint32_t leaf,uint32_t sub,uint32_t*a,uint32_t*b,uint32_t*c,uint32_t*d){
    __asm__ __volatile__("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(leaf),"c"(sub));
}
static uint32_t desktop_pci_read32(uint8_t bus,uint8_t dev,uint8_t fn,uint8_t reg){
    uint32_t address=0x80000000u|((uint32_t)bus<<16)|((uint32_t)dev<<11)|((uint32_t)fn<<8)|(reg&0xFCu);
    __asm__ __volatile__("outl %0,%1"::"a"(address),"Nd"((uint16_t)0xCF8));
    uint32_t v;
    __asm__ __volatile__("inl %1,%0":"=a"(v):"Nd"((uint16_t)0xCFC));
    return v;
}
static void scan_pci_devices(void){
    pci_device_count=0;
    for(uint32_t bus=0;bus<256&&pci_device_count<24;bus++)
        for(uint32_t dev=0;dev<32&&pci_device_count<24;dev++)
            for(uint32_t fn=0;fn<8&&pci_device_count<24;fn++){
                uint32_t id=desktop_pci_read32((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,0);
                if((uint16_t)id==0xFFFFu)continue;
                uint32_t cls=desktop_pci_read32((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,8);
                PCI_VIEW*d=&pci_devices[pci_device_count++];
                d->bus=(uint8_t)bus;d->dev=(uint8_t)dev;d->fn=(uint8_t)fn;
                d->vendor=(uint16_t)id;d->device=(uint16_t)(id>>16);
                d->class_code=(uint8_t)(cls>>24);d->subclass=(uint8_t)(cls>>16);
            }
}
static void draw_devices(void){
    window_bar("DEVICE MANAGER","PCI HARDWARE ENUMERATION");
    text(42,116,"BUS",sub_color(),1);text(98,116,"DEVICE",sub_color(),1);text(190,116,"FUNCTION",sub_color(),1);text(290,116,"VENDOR",sub_color(),1);text(390,116,"DEVICE ID",sub_color(),1);text(500,116,"CLASS",sub_color(),1);
    for(int i=0;i<pci_device_count;i++){
        int y=132+i*30;
        fill_rect(40,y,(int)width-80,24,(i&1)?panel_color():bg_color());
        u64_text(52,y+7,pci_devices[i].bus,text_color(),1);
        u64_text(102,y+7,pci_devices[i].dev,text_color(),1);
        u64_text(194,y+7,pci_devices[i].fn,text_color(),1);
        u64_text(294,y+7,pci_devices[i].vendor,text_color(),1);
        u64_text(394,y+7,pci_devices[i].device,text_color(),1);
        u64_text(504,y+7,pci_devices[i].class_code,text_color(),1);
        text(570,y+7,pci_devices[i].class_code==3?"DISPLAY":pci_devices[i].class_code==1?"STORAGE":pci_devices[i].class_code==2?"NETWORK":pci_devices[i].class_code==12?"SERIAL":"DEVICE",sub_color(),1);
    }
    text(42,(int)height-122,"READ-ONLY PCI CONFIGURATION SPACE  •  FIRST 24 DEVICES",sub_color(),1);taskbar();
}

static void draw_sysinfo(void){
    window_bar("SYSTEM INFORMATION","NATIVE HARDWARE / FIRMWARE");
    uint32_t a=0,b=0,cpu_c=0,d=0;char vendor[13];
    cpuid_native(0,0,&a,&b,&cpu_c,&d);
    vendor[0]=(char)b;vendor[1]=(char)(b>>8);vendor[2]=(char)(b>>16);vendor[3]=(char)(b>>24);
    vendor[4]=(char)d;vendor[5]=(char)(d>>8);vendor[6]=(char)(d>>16);vendor[7]=(char)(d>>24);
    vendor[8]=(char)cpu_c;vendor[9]=(char)(cpu_c>>8);vendor[10]=(char)(cpu_c>>16);vendor[11]=(char)(cpu_c>>24);vendor[12]=0;
    text(42,116,"CPU VENDOR",accent_color(),1);text(42,140,vendor,text_color(),2);
    if(a>=1){cpuid_native(1,0,&a,&b,&cpu_c,&d);}
    text(42,186,"CPU SIGNATURE",accent_color(),1);u64_text(42,210,(uint64_t)(a),text_color(),2);
    text(42,248,"MEMORY",accent_color(),1);u64_text(42,272,total_memory/1024/1024,text_color(),2);text(104,275,"MB CONVENTIONAL",sub_color(),1);
    text(300,248,"LARGEST FREE REGION",accent_color(),1);u64_text(300,272,largest_region/1024/1024,text_color(),2);text(380,275,"MB",sub_color(),1);
    text(42,310,"GRAPHICS",accent_color(),1);u64_text(42,334,width,text_color(),2);text(100,337,"X",sub_color(),1);u64_text(122,334,height,text_color(),2);text(180,337,"GOP PIXELS",sub_color(),1);
    text(42,372,"FRAMEBUFFER STRIDE",sub_color(),1);u64_text(210,372,stride,text_color(),1);text(42,400,"KERNEL SIZE",sub_color(),1);u64_text(210,400,(uint64_t)boot_info->kernel_size/1024,text_color(),1);text(270,400,"KB",sub_color(),1);
    text(42,438,"ACPI RSDP",sub_color(),1);text(210,438,boot_info->acpi_rsdp?"PRESENT":"MISSING",boot_info->acpi_rsdp?good_color():danger_color(),1);
    text(42,466,"NVRAM",sub_color(),1);text(210,466,(boot_info->uefi_get_variable&&boot_info->uefi_set_variable)?"AVAILABLE":"OFF",good_color(),1);
    text(42,494,"UEFI HTTP",sub_color(),1);text(210,494,boot_info->uefi_http_get?"AVAILABLE":"OFF",boot_info->uefi_http_get?good_color():danger_color(),1);
    text(42,522,"USB HID MOUSE",sub_color(),1);text(210,522,native_usb_mouse_present()?"ACTIVE":"OFF",native_usb_mouse_present()?good_color():sub_color(),1);
    text(42,550,"I2C HID TOUCHPAD",sub_color(),1);text(210,550,native_i2c_hid_present()?"ACTIVE":"OFF",native_i2c_hid_present()?good_color():sub_color(),1);
    text(42,(int)height-98,"SYSTEM INFORMATION IS READ DIRECTLY FROM THE NATIVE BOOT ENVIRONMENT",sub_color(),1);taskbar();
}

static void draw_about(void){
    window_bar("ABOUT STEVEOS","NATIVE X86-64 DESKTOP");text(44,122,"STEVEOS",accent_color(),3);text(46,162,"NATIVE KERNEL DESKTOP 0.9",text_color(),1);text(46,190,"UEFI GOP • GDT • IDT • PAGING • PS2 • USB HID • I2C HID",sub_color(),1);text(46,216,"NATIVE APPS • NVRAM SETTINGS • TEXT EDITOR • HTTP BROWSER",sub_color(),1);
    fill_rect(44,254,(int)width-88,150,panel2_color());text(62,274,"LINUX MINT INSPIRATION",text_color(),1);text(62,300,"TRADITIONAL DESKTOP / PANEL / MENU PATTERNS",sub_color(),1);text(62,324,"MINT-Y ICON THEME REFERENCE: linuxmint/mint-y-icons",sub_color(),1);text(62,348,"CINNAMON PROJECT REFERENCE: linuxmint/cinnamon",sub_color(),1);text(62,372,"SEE THIRD_PARTY.MD FOR LICENSING AND ATTRIBUTION.",sub_color(),1);
    text(44,(int)height-98,"STEVEOS IS A FREESTANDING PROJECT, NOT A LINUX MINT DERIVATIVE",sub_color(),1);taskbar();
}

static void launch_app(int app){
    menu_open=0;power_menu=0;menu_search_len=0;menu_search[0]=0;
    if(app>=0){
        if(app!=current_app)previous_app=current_app;
        current_app=app;
        if(app==APP_EDITOR){editor_target_path[0]=0;}
    }
    selected_file=-1;
    browser_focus=app==APP_BROWSER?1:0;
    if(app==APP_BROWSER){browser_url[0]=0;browser_loaded=0;browser_scroll=0;browser_status=0;browser_raw[0]=0;browser_text[0]=0;browser_title[0]=0;browser_link_count=0;browser_history_count=0;browser_history_pos=0;browser_history_lock=0;browser_tab_count=1;browser_current_tab=0;browser_tabs[0][0]=0;}
    if(app==APP_INSTALLER)refresh_install_targets();
    if(app==APP_STORE){scan_app_packages();app_download_focus=1;app_download_url[0]=0;}
    if(app==APP_SERVER){refresh_network_info();refresh_install_targets();if(server_runtime_detected())server_install_state=2;}if(app==APP_ADVANCED)refresh_network_info();if(app==APP_CONTROL)refresh_network_info();
    mark_dirty();
}
static void app_click(uint32_t x,uint32_t y){
    if(power_menu){
        int w=300,h=154,px=(int)width-w-18,py=(int)height-54-h-12;
        if(hit(x,y,px+20,py+72,76,44)){power_menu=0;native_halt();return;}
        if(hit(x,y,px+112,py+72,76,44)){power_menu=0;native_reboot();return;}
        if(hit(x,y,px+204,py+72,76,44)){power_menu=0;mark_dirty();return;}
        power_menu=0;mark_dirty();return;
    }
    if(y>=(uint32_t)height-54){
        if(x>(uint32_t)width-90u){power_menu^=1;menu_open=0;mark_dirty();return;}
        if(update_info.available&&x>=(uint32_t)width-165u&&x<(uint32_t)width-90u){current_app=APP_ADVANCED;menu_open=0;mark_dirty();return;}
        if(x<76u){current_app=APP_DESKTOP;menu_open=1;mark_dirty();return;}
        if(x>=84u&&x<516u&&((x-84u)%72u)<64u){int slot=(int)((x-84u)/72u);launch_app((int[]){APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_TERMINAL,APP_SETTINGS}[slot]);return;}
    }
    if(current_app!=APP_DESKTOP&&hit(x,y,(int)width-62,66,30,24)){current_app=APP_DESKTOP;menu_open=0;mark_dirty();return;}
    if(current_app==APP_CALC){int bw=100,bh=46,g=10,cols=5,x0=38,y0=204;const char*keys[]={"7","8","9","/","4","5","6","*","1","2","3","-","0","(",")","+","C","=","."};for(int i=0;i<19;i++){int bx=x0+(i%cols)*(bw+g),by=y0+(i/cols)*(bh+g);if(hit(x,y,bx,by,bw,bh)){char c=keys[i][0];if(c=='C'){calc_len=0;calc_input[0]=0;calc_has_result=0;}else if(c=='=')calc_eval();else if(calc_len<CALC_MAX){calc_input[calc_len++]=c;calc_input[calc_len]=0;calc_has_result=0;}mark_dirty();return;}}}
    else if(current_app==APP_FILES){
        for(int p=0;p<6;p++)if(hit(x,y,50,150+p*42,190,34)){file_filter=p;file_scroll=0;selected_file=-1;mark_dirty();return;}
        for(int row=0;row<10;row++){uint64_t i=0;if(!visible_file_at(file_scroll+row,&i))break;if(hit(x,y,278,150+row*40,(int)width-316,32)){selected_file=(int)i;STEVEOS_BOOT_FILE*f=&boot_files[i];if(f->kind==1)current_app=APP_IMAGE;else if(f->kind==4){if(boot_info->uefi_run_windows_app) {RUNWINDOWSAPP fn=(RUNWINDOWSAPP)(uintptr_t)boot_info->uefi_run_windows_app;fn(f->name);}}else if(f->kind==2&&f->data){if(boot_name_is_html(f))browser_load_local_file(f);else load_text_file(f);}mark_dirty();return;}}
    }
    else if(current_app==APP_SETTINGS){if(hit(x,y,42,136,(int)width-84,54))light_theme^=1;else if(hit(x,y,42,202,(int)width-84,54)){pointer_scale=pointer_scale>=4?1:pointer_scale+1;native_pointer_set_scale(pointer_scale);}else if(hit(x,y,42,308,(int)width-84,54)){accent_id=(uint8_t)((accent_id+1)&3u);}mark_dirty();}
    else if(current_app==APP_CONTROL){
        for(int i=0;i<9;i++){int col=i%2,row=i/2,rx=42+col*300,ry=114+row*76;if(hit(x,y,rx,ry,280,60)){const int targets[]={APP_SETTINGS,APP_SETTINGS,APP_BROWSER,APP_FILES,APP_TASKS,APP_SYSINFO,APP_FILES,APP_DEVICES,APP_SERVER};launch_app(targets[i]);return;}}
    }
    else if(current_app==APP_INSTALLER){
        int shown=install_target_count>8?8:(int)install_target_count;
        for(int i=0;i<shown;i++)if(hit(x,y,42,184+i*40,(int)width-84,32)){install_target_pick=i;install_armed=0;mark_dirty();return;}
        if(hit(x,y,42,(int)height-136,150,38)&&install_target_pick>=0){install_armed=install_armed?install_armed:1;mark_dirty();return;}
        if(hit(x,y,208,(int)height-136,150,38)&&install_target_pick>=0&&install_armed==1){install_self_now();mark_dirty();return;}
        if(hit(x,y,(int)width-180,(int)height-136,138,38)){refresh_install_targets();mark_dirty();return;}
    }
    else if(current_app==APP_STORE){
        if(hit(x,y,42,134,(int)width-84,34)){app_download_focus=1;mark_dirty();return;}
        for(int i=0;i<app_package_count&&i<7;i++)if(hit(x,y,42,214+i*48,(int)width-84,38)){app_package_pick=i;app_install_done=0;mark_dirty();return;}
        if(hit(x,y,42,(int)height-136,150,38)&&app_package_pick>=0){app_install_selected();mark_dirty();return;}
        if(hit(x,y,208,(int)height-136,150,38)&&app_package_pick>=0){app_install_selected();if(app_install_done==1)app_launch_selected();mark_dirty();return;}
    }
    else if(current_app==APP_SERVER){
        if(hit(x,y,42,278,170,38)){
            if(boot_info->uefi_install_server){
                int pick=install_target_pick>=0?install_target_pick:0;
                if(install_target_count==0)refresh_install_targets();
                if(install_target_count>0){
                    INSTALLSERVER fn=(INSTALLSERVER)(uintptr_t)boot_info->uefi_install_server;
                    uint64_t st=fn((uint64_t)pick);server_install_state=(st==0)?2:3;
                }else server_install_state=3;
            }
            mark_dirty();return;
        }
        if(hit(x,y,228,278,170,38)){
            if(boot_info->uefi_launch_server&&server_install_state==2){
                LAUNCHSERVER fn=(LAUNCHSERVER)(uintptr_t)boot_info->uefi_launch_server;
                fn();
            }
            mark_dirty();return;
        }
        if(hit(x,y,414,278,170,38)){open_server_config();return;}
        if(hit(x,y,42,390,(int)width-84,80)){service_flags^=7;save_settings();mark_dirty();return;}
    }
    else if(current_app==APP_ADVANCED){
        if(hit(x,y,42,136,(int)width-84,48)){service_flags^=1;save_settings();return;}
        if(hit(x,y,42,194,(int)width-84,48)){service_flags^=2;save_settings();return;}
        if(hit(x,y,42,252,(int)width-84,48)){service_flags^=4;save_settings();return;}
        if(hit(x,y,42,310,(int)width-84,48)){logging_level^=1;save_settings();return;}
        if(hit(x,y,42,368,(int)width-84,48)){boot_delay=boot_delay>=10?0:boot_delay+1;save_settings();return;}
        if(hit(x,y,(int)width-220,594,160,34)){internet_test();mark_dirty();return;}
        if(hit(x,y,(int)width-220,636,76,32)){refresh_update_info();mark_dirty();return;}
        if(hit(x,y,(int)width-136,636,76,32)){apply_system_update();mark_dirty();return;}
    }
    else if(current_app==APP_BROWSER){
        for(int i=0;i<browser_tab_count;i++)if(hit(x,y,38+i*120,152,110,28)){browser_tab_switch(i);return;}
        if(hit(x,y,34,106,(int)width-370,40)){browser_focus=1;mark_dirty();}
        else if(hit(x,y,(int)width-326,106,50,40)){browser_focus=0;browser_history_move(-1);mark_dirty();}
        else if(hit(x,y,(int)width-270,106,50,40)){browser_focus=0;browser_history_move(1);mark_dirty();}
        else if(hit(x,y,(int)width-214,106,50,40)){browser_focus=0;if(browser_url[0])browser_fetch();mark_dirty();}
        else{for(int i=0;i<browser_link_count;i++)if(hit(x,y,(int)width-270,228+i*42,220,32)){size_t n=0;while(browser_link_urls[i][n]&&n+1<BROWSER_URL_MAX)browser_url[n]=browser_link_urls[i][n],n++;browser_url[n]=0;browser_focus=0;browser_fetch();mark_dirty();return;}}
    }
    else if(current_app==APP_DESKTOP){if(hit(x,y,0,(int)height-54,76,54)){menu_open^=1;if(menu_open){menu_search_len=0;menu_search[0]=0;}mark_dirty();return;}int h=54;for(int i=0;i<6;i++)if(hit(x,y,84+i*72,(int)height-h,64,38)){launch_app((int[]){APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_TERMINAL,APP_SETTINGS}[i]);return;}int cw=250,ch=80,g=14,x0=28,y0=132,cols=width>=1200?4:3;for(int i=0;i<12;i++){int col=i%cols,row=i/cols,bx=x0+col*(cw+g),by=y0+row*(ch+g);if(hit(x,y,bx,by,cw,ch)){launch_app((int[]){APP_BROWSER,APP_CALC,APP_EDITOR,APP_FILES,APP_IMAGE,APP_SETTINGS,APP_TASKS,APP_TERMINAL,APP_CALENDAR,APP_CONTROL,APP_ABOUT,APP_SYSINFO,APP_DEVICES,APP_INSTALLER,APP_STORE,APP_SERVER,APP_ADVANCED}[i]);return;}}}
    else if(current_app==APP_ABOUT||current_app==APP_CONTROL||current_app==APP_TASKS||current_app==APP_EDITOR||current_app==APP_TERMINAL||current_app==APP_IMAGE||current_app==APP_CALENDAR||current_app==APP_SYSINFO||current_app==APP_DEVICES||current_app==APP_INSTALLER||current_app==APP_STORE||current_app==APP_SERVER||current_app==APP_ADVANCED){if(hit(x,y,(int)width-62,66,30,24)){current_app=APP_DESKTOP;mark_dirty();return;}if(y>(uint32_t)height-54&&x<76){current_app=APP_DESKTOP;menu_open=1;mark_dirty();return;}}
}
static void menu_click(uint32_t x,uint32_t y){
    int mw=430,mh=(int)height-76,mx=12,my=(int)height-62-mh;
    if(!hit(x,y,mx,my,mw,mh)){menu_open=0;menu_search_len=0;menu_search[0]=0;mark_dirty();return;}
    if(hit(x,y,mx+18,my+60,mw-36,30)){return;}
    int shown=0;
    for(int i=0;i<17;i++)if(contains_ci(menu_names[i],menu_search)){
        int row=shown%9,col=shown/9,bx=mx+18+col*196,by=my+98+row*55;
        if(hit(x,y,bx,by,180,45)){launch_app(menu_apps[i]);return;}
        shown++;
    }
}

static char key_char(uint8_t s);
static void menu_key(uint8_t s){
    if(s==0x01){menu_open=0;menu_search_len=0;menu_search[0]=0;mark_dirty();return;}
    if(s==0x0E){if(menu_search_len)menu_search[--menu_search_len]=0;mark_dirty();return;}
    if(s==0x1C){
        if(menu_search_len){launch_app(menu_filtered_app(0));return;}
        menu_open=0;mark_dirty();return;
    }
    char ch=key_char(s);
    if(ch&&menu_search_len+1<sizeof(menu_search) && ((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch==' '||ch=='-'||ch=='_')){
        if(ch>='A'&&ch<='Z')ch=(char)(ch-'A'+'a');
        menu_search[menu_search_len++]=ch;menu_search[menu_search_len]=0;mark_dirty();return;
    }
}
static char key_char(uint8_t s){
    static const char m[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0};return s<128?m[s]:0;
}
static char shifted(char c){if(c>='a'&&c<='z')return(char)(c-'a'+'A');if(c>='0'&&c<='9'){const char*s=")!@#$%^&*(";return s[c-'0'];}if(c=='-')return'_';if(c=='=')return'+';if(c=='[')return'{';if(c==']')return'}';if(c==';')return':';if(c=='\'')return'"';if(c==',')return'<';if(c=='.')return'>';if(c=='/')return'?';if(c=='\\')return'|';return c;}
static void browser_key(uint8_t s){
    if(ctrl_down){
        if(s==0x26){browser_focus=1;return;}
        if(s==0x14){browser_new_tab();return;}
        if(s==0x11){browser_close_tab();return;}
        if(s==0x13){if(browser_url[0])browser_fetch();return;}
        if(s==0x1F){browser_save_page();return;}
        if(s==0x20){bookmark_current();return;}
        if(s==0x30){open_first_bookmark();return;}
        if(s>=0x02&&s<=0x05){browser_tab_switch((int)s-2);return;}
    }

    if(s==0x2A||s==0x36){shift_down=1;return;}
    if(s==0xAA||s==0xB6){shift_down=0;return;}
    if(s==0x1C){
        if(browser_focus){
            if(browser_url[0])browser_fetch();
            else {const char*p="http://neverssl.com/";size_t n=0;while(p[n]&&n+1<BROWSER_URL_MAX){browser_url[n]=p[n];n++;}browser_url[n]=0;browser_fetch();}
            browser_focus=0;
        }
        return;
    }
    if(s==0x0E){
        if(browser_focus){size_t n=0;while(browser_url[n])n++;if(n)browser_url[n-1]=0;}
        return;
    }
    if(s==0x48){if(!browser_focus&&browser_scroll>0)browser_scroll--;return;}
    if(s==0x50){if(!browser_focus)browser_scroll++;return;}
    if(s==0x47){if(!browser_focus)browser_scroll=0;return;}
    if(s==0x4B){if(!browser_focus)browser_history_move(-1);return;}
    if(s==0x4D){if(!browser_focus)browser_history_move(1);return;}
    if(s==0x01){browser_focus=0;return;}
    if(!browser_focus&&s>1&&s<10&&s-2<browser_link_count){int i=s-2;size_t n=0;while(browser_link_urls[i][n]&&n+1<BROWSER_URL_MAX){browser_url[n]=browser_link_urls[i][n];n++;}browser_url[n]=0;browser_fetch();return;}
    char c=key_char(s);
    if(browser_focus&&c&&((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c==':'||c=='/'||c=='.'||c=='-'||c=='_'||c=='?'||c=='&'||c=='='||c=='#'||c=='%'||c=='+'||c==';'||c=='~')){
        size_t n=0;while(browser_url[n])n++;
        if(n<BROWSER_URL_MAX){browser_url[n]=shift_down?shifted(c):c;browser_url[n+1]=0;}
    }
}
static void terminal_key(uint8_t s){if(s==0x2A||s==0x36){shift_down=1;return;}if(s==0xAA||s==0xB6){shift_down=0;return;}if(s==0x1C){terminal_exec();return;}if(s==0x0E){if(terminal_len)terminal_input[--terminal_len]=0;return;}char c=key_char(s);if(c&&terminal_len<TERM_MAX){terminal_input[terminal_len++]=shift_down?shifted(c):((c>='a'&&c<='z')?(char)(c-'a'+'A'):c);terminal_input[terminal_len]=0;}}
static void calc_key(uint8_t s){
    if(s==0x1C){calc_eval();return;}
    if(s==0x0E){if(calc_len){calc_input[--calc_len]=0;calc_has_result=0;}return;}
    char c=key_char(s);
    int allowed=(c>='0'&&c<='9')||c=='+'||c=='-'||c=='*'||c=='/'||c=='('||c==')'||c=='.';
    if(allowed&&calc_len<CALC_MAX){calc_input[calc_len++]=c;calc_input[calc_len]=0;calc_has_result=0;}
}

static void handle_scan(uint8_t s){
    if(!s)return;
    if(s==0x2A||s==0x36){shift_down=1;return;}
    if(s==0xAA||s==0xB6){shift_down=0;return;}
    if(s==0x1D){ctrl_down=1;return;}
    if(s==0x9D){ctrl_down=0;return;}
    if(s==0x38){alt_down=1;return;}
    if(s==0xB8){alt_down=0;return;}
    if(s&0x80)return;
    if(alt_down&&s==0x0F){int a=current_app;current_app=previous_app;previous_app=a;menu_open=0;power_menu=0;mark_dirty();return;}
    if(s==0x58){power_menu^=1;menu_open=0;mark_dirty();return;}if(s==0x3B){launch_app(APP_BROWSER);return;}if(s==0x3C){launch_app(APP_CALC);return;}if(s==0x3D){launch_app(APP_EDITOR);return;}if(s==0x3E){launch_app(APP_FILES);return;}if(s==0x3F){if(current_app==APP_EDITOR)save_note();else if(current_app==APP_SETTINGS||current_app==APP_ADVANCED)save_settings();else if(current_app==APP_BROWSER&&browser_url[0])browser_fetch();mark_dirty();return;}if(s==0x40){launch_app(APP_TASKS);return;}if(s==0x41){launch_app(APP_TERMINAL);return;}if(s==0x42){launch_app(APP_CALENDAR);return;}if(s==0x43){launch_app(APP_CONTROL);return;}if(s==0x44){launch_app(APP_SYSINFO);return;}if(s==0x57){launch_app(APP_ABOUT);return;}
    if(s==1){current_app=APP_DESKTOP;menu_open=0;mark_dirty();return;}
    if(s==0x38&&current_app==APP_BROWSER){browser_focus=1;mark_dirty();return;}
    if(menu_open){menu_key(s);return;}
    if(current_app==APP_EDITOR){note_key(s);return;}if(current_app==APP_BROWSER){browser_key(s);return;}if(current_app==APP_CALC){calc_key(s);return;}if(current_app==APP_TERMINAL){terminal_key(s);return;}
    if(current_app==APP_CONTROL){if(s>=2&&s<=9){const int targets[]={APP_SETTINGS,APP_SETTINGS,APP_BROWSER,APP_FILES,APP_TASKS,APP_SYSINFO,APP_FILES,APP_DEVICES};launch_app(targets[s-2]);return;}mark_dirty();return;}
    if(current_app==APP_INSTALLER){
        if(s==0x1C){if(install_target_pick>=0){if(!install_armed)install_armed=1;else install_self_now();}mark_dirty();return;}
        if(s==0x48&&install_target_pick>0)install_target_pick--;else if(s==0x50&&install_target_pick+1<(int)install_target_count)install_target_pick++;
        if(s==0x52)refresh_install_targets();mark_dirty();return;
    }
    if(current_app==APP_STORE){
        if(app_download_focus){
            if(s==0x1C){app_download_focus=0;app_download_selected();mark_dirty();return;}
            if(s==0x0E){size_t n=0;while(app_download_url[n])n++;if(n)app_download_url[n-1]=0;mark_dirty();return;}
            char ch=key_char(s);if(ch&&((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch==':'||ch=='/'||ch=='.'||ch=='-'||ch=='_'||ch=='?'||ch=='&'||ch=='='||ch=='%'||ch=='#'||ch=='+')){
                size_t n=0;while(app_download_url[n])n++;if(n<BROWSER_URL_MAX){app_download_url[n++]=shift_down?shifted(ch):ch;app_download_url[n]=0;}mark_dirty();return;
            }
        }else{
            if(s==0x48&&app_package_pick>0)app_package_pick--;else if(s==0x50&&app_package_pick+1<app_package_count)app_package_pick++;
            else if(s==0x1C&&app_package_pick>=0){app_install_selected();}
            else if(s==0x4C&&app_package_pick>=0){app_launch_selected();}
            else if(s==0x2E){app_download_focus=1;}
        }
        mark_dirty();return;
    }
    if(current_app==APP_SERVER){
        if(s==0x1C&&server_install_state==2&&boot_info->uefi_launch_server){LAUNCHSERVER fn=(LAUNCHSERVER)(uintptr_t)boot_info->uefi_launch_server;fn();return;}
        if(s==0x18){open_server_config();return;}
        if(s==2||s==3||s==4){service_flags^=1u<<(s-2);save_settings();}
        else if(s==5){if(boot_info->uefi_install_server&&install_target_count){INSTALLSERVER fn=(INSTALLSERVER)(uintptr_t)boot_info->uefi_install_server;uint64_t st=fn((uint64_t)(install_target_pick>=0?install_target_pick:0));server_install_state=(st==0)?2:3;}}
        mark_dirty();return;
    }
    if(current_app==APP_ADVANCED){if(s==2)service_flags^=1;else if(s==3)service_flags^=2;else if(s==4)service_flags^=4;else if(s==5)logging_level^=1;else if(s==6)boot_delay=boot_delay>=10?0:boot_delay+1;save_settings();mark_dirty();return;}
    if(current_app==APP_FILES){
        if(s>=2&&s<=7){file_filter=(int)(s-2);file_scroll=0;selected_file=-1;}
        else if(s==0x48||s==0x4B)move_file_selection(-1);
        else if(s==0x50||s==0x4D)move_file_selection(1);
        else if(s==0x1C&&selected_file>=0){STEVEOS_BOOT_FILE*f=&boot_files[selected_file];if(file_matches(f)){if(f->kind==1)current_app=APP_IMAGE;else if(f->kind==4&&boot_info->uefi_run_windows_app){RUNWINDOWSAPP fn=(RUNWINDOWSAPP)(uintptr_t)boot_info->uefi_run_windows_app;fn(f->name);}else if(f->kind==2&&f->data){if(boot_name_is_html(f))browser_load_local_file(f);else load_text_file(f);}}}
        mark_dirty();return;
    }
    if(current_app==APP_SETTINGS){if(s==0x4B&&pointer_scale>1)pointer_scale--;else if(s==0x4D&&pointer_scale<4)pointer_scale++;else if(s==0x48&&accent_id>0)accent_id--;else if(s==0x50)accent_id=(uint8_t)((accent_id+1)&3u);native_pointer_set_scale(pointer_scale);mark_dirty();return;}
    mark_dirty();
}

static void render(void){
    switch(current_app){case APP_DESKTOP:draw_desktop();break;case APP_BROWSER:draw_browser();break;case APP_CALC:draw_calc();break;case APP_EDITOR:draw_editor();break;case APP_FILES:draw_files();break;case APP_IMAGE:draw_image();break;case APP_SETTINGS:draw_settings();break;case APP_TASKS:draw_tasks();break;case APP_TERMINAL:draw_terminal();break;case APP_CALENDAR:draw_calendar();break;case APP_CONTROL:draw_control();break;case APP_SYSINFO:draw_sysinfo();break;case APP_DEVICES:scan_pci_devices();draw_devices();break;case APP_INSTALLER:draw_installer();break;case APP_STORE:draw_store();break;case APP_SERVER:draw_server();break;case APP_ADVANCED:draw_advanced();break;default:draw_about();break;}
    draw_power_menu();
    present();dirty=0;
}

void steveos_desktop_init(STEVEOS_BOOT_INFO *boot){
    boot_info=boot;boot_files=(STEVEOS_BOOT_FILE*)(uintptr_t)boot->boot_files;framebuffer=(uint32_t*)(uintptr_t)boot->framebuffer_base;width=(uint32_t)boot->width;height=(uint32_t)boot->height;stride=(uint32_t)boot->pixels_per_scanline;memory_stats();init_backbuffer();load_settings();load_note();load_bookmarks();browser_tab_count=1;browser_current_tab=0;browser_tabs[0][0]=0;calc_input[0]=0;terminal_lines[0][0]=0;terminal_add("STEVEOS NATIVE SHELL");terminal_add("TYPE HELP FOR COMMANDS");browser_focus=0;dirty=1;}

void steveos_desktop_run(STEVEOS_BOOT_INFO *boot){
    (void)boot;
    uint32_t last_x=native_pointer_x(),last_y=native_pointer_y();
    uint8_t last_b=native_pointer_buttons();
    uint32_t refresh_ticks=0;
    for(;;){
        uint8_t s=native_keyboard_read_scancode();if(s){handle_scan(s);dirty=1;}
        uint32_t x=native_pointer_x(),y=native_pointer_y();uint8_t b=native_pointer_buttons();
        if(x!=last_x||y!=last_y||b!=last_b){dirty=1;last_x=x;last_y=y;last_b=b;}
        if((b&1)&&!(previous_buttons&1)){if(menu_open)menu_click(x,y);else app_click(x,y);dirty=1;}
        previous_buttons=b;
        if(++refresh_ticks>=8000){refresh_ticks=0;refresh_network_info();if(!update_checked)refresh_update_info();dirty=1;}
        if(dirty)render();
        for(volatile int i=0;i<1800;i++)__asm__ __volatile__("pause");
    }
}
