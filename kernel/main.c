#include <stdint.h>
#include <stddef.h>
#include "../src/bootinfo.h"

#define EFI_CONVENTIONAL_MEMORY 7
#define NOTE_MAX 512
#define APP_DESKTOP 0
#define APP_CALC 1
#define APP_NOTEPAD 2
#define APP_EXPLORER 3
#define APP_IMAGE 4

typedef struct { uint32_t a,b,c,d; } GUID;
typedef uint64_t (__attribute__((ms_abi)) *EFI_GET_VARIABLE)(uint16_t*, GUID*, uint32_t*, uint64_t*, void*);
typedef uint64_t (__attribute__((ms_abi)) *EFI_SET_VARIABLE)(uint16_t*, GUID*, uint32_t, uint64_t, void*);

extern void native_reboot(void);
extern void native_halt(void);
extern uint8_t native_keyboard_read_scancode(void);
extern uint32_t native_pointer_x(void);
extern uint32_t native_pointer_y(void);
extern uint8_t native_pointer_buttons(void);
extern void native_pointer_hide(void);
extern void native_pointer_show(void);
extern int native_usb_mouse_present(void);
extern const unsigned char _binary_build_boot_raw_start[];
extern const unsigned char _binary_build_boot_raw_end[];

static uint32_t *framebuffer;
static uint32_t fb_width, fb_height, fb_stride, fb_format;
static uint64_t memory_total, heap_current, heap_end;
static int app = APP_DESKTOP;
static uint8_t note[NOTE_MAX];
static size_t note_len, note_cursor;
static uint8_t dirty;
static uint8_t prev_buttons;
static char calc_input[64];
static size_t calc_len;
static uint64_t calc_value;
static char calc_op;
static uint8_t calc_has_value;
static STEVEOS_BOOT_FILE *boot_files;
static uint64_t boot_file_count;
static int selected_file = -1;
static int file_scroll;

static const GUID note_guid = {0x53544556,0x4f53,0x4e56,0x00010001};
static const uint16_t note_name[] = {'S','t','e','v','e','O','S','N','o','t','e',0};

static const uint8_t font[36][5] = {
 {0x7e,0x11,0x11,0x7e,0},{0x7f,0x49,0x49,0x36,0},{0x3e,0x41,0x41,0x22,0},{0x7f,0x41,0x41,0x3e,0},
 {0x7f,0x49,0x49,0x41,0},{0x7f,0x09,0x09,0x01,0},{0x3e,0x41,0x51,0x72,0},{0x7f,0x08,0x08,0x7f,0},
 {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0},{0x7f,0x08,0x14,0x63,0},{0x7f,0x40,0x40,0x40,0},
 {0x7f,0x06,0x18,0x06,0x7f},{0x7f,0x06,0x18,0x7f,0},{0x3e,0x41,0x41,0x3e,0},{0x7f,0x09,0x09,0x06,0},
 {0x3e,0x41,0x61,0x7e,0},{0x7f,0x09,0x19,0x66,0},{0x26,0x49,0x49,0x32,0},{1,0x7f,1,1,0},
 {0x3f,0x40,0x40,0x3f,0},{0x1f,0x60,0x60,0x1f,0},{0x7f,0x30,0x0c,0x30,0x7f},{0x63,0x14,8,0x14,0x63},
 {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},{0x3e,0x45,0x49,0x51,0x3e},{0x21,0x7f,1,0,0},
 {0x23,0x45,0x49,0x49,0x31},{0x22,0x41,0x49,0x49,0x36},{0x0c,0x14,0x24,0x7f,4},{0x7a,0x49,0x49,0x49,0x46},
 {0x3e,0x49,0x49,0x49,0x26},{0x40,0x47,0x48,0x50,0x60},{0x36,0x49,0x49,0x49,0x36},{0x32,0x49,0x49,0x49,0x3e}
};

static inline void pixel(int x,int y,uint32_t c){if(!framebuffer||x<0||y<0||(uint32_t)x>=fb_width||(uint32_t)y>=fb_height)return;framebuffer[(size_t)y*fb_stride+(size_t)x]=c;}
static void fill(int x,int y,int w,int h,uint32_t c){for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++)pixel(x+xx,y+yy,c);}
static const uint8_t *glyph_for(char c){if(c>='A'&&c<='Z')return font[c-'A'];if(c>='a'&&c<='z')return font[c-'a'];if(c>='0'&&c<='9')return font[26+c-'0'];return NULL;}
static void glyph(int x,int y,char c,uint32_t col,int s){const uint8_t*g=glyph_for(c);if(!g)return;for(int a=0;a<5;a++)for(int b=0;b<7;b++)if(g[a]&(1u<<b))fill(x+a*s,y+b*s,s,s,col);}
static void text(int x,int y,const char*s,uint32_t c,int sc){while(*s){if(*s==' ')x+=6*sc;else{glyph(x,y,*s,c,sc);x+=6*sc;}s++;}}
static void num(int x,int y,uint64_t v,uint32_t c,int sc){char b[24];size_t n=0;if(!v)b[n++]='0';while(v&&n<23){b[n++]=(char)('0'+v%10);v/=10;}while(n)glyph(x,y,b[--n],c,sc),x+=6*sc;}
static uint32_t rgb(uint8_t r,uint8_t g,uint8_t b){if(fb_format==0)return((uint32_t)r)|((uint32_t)g<<8)|((uint32_t)b<<16);return((uint32_t)b)|((uint32_t)g<<8)|((uint32_t)r<<16);}
static int inside(uint32_t x,uint32_t y,int a,int b,int w,int h){return x>=(uint32_t)a&&x<(uint32_t)(a+w)&&y>=(uint32_t)b&&y<(uint32_t)(b+h);}

static void memory_init(const STEVEOS_BOOT_INFO*boot){memory_total=heap_current=heap_end=0;if(!boot||!boot->memory_map||!boot->memory_descriptor_size)return;uint8_t*p=(uint8_t*)(uintptr_t)boot->memory_map,*e=p+boot->memory_map_size;uint64_t ls=0,le=0;while(p+boot->memory_descriptor_size<=e){uint32_t type=*(uint32_t*)p;uint64_t*q=(uint64_t*)p;uint64_t st=q[1],pages=q[4],bytes=pages*4096ULL;if(type==EFI_CONVENTIONAL_MEMORY){memory_total+=bytes;if(bytes>=1024*1024&&st+bytes>le){ls=st;le=st+bytes;}}p+=boot->memory_descriptor_size;}heap_current=(ls+0xfffULL)&~0xfffULL;heap_end=le;}

static void bar(void){fill(0,0,(int)fb_width,48,rgb(27,42,70));text(24,18,"STEVEOS",rgb(245,248,255),2);text(145,22,"NATIVE",rgb(154,177,210),1);text((int)fb_width-210,20,native_usb_mouse_present()?"USB MOUSE":"INPUT READY",rgb(205,235,218),1);}
static void clear_desktop(void){native_pointer_hide();fill(0,0,(int)fb_width,(int)fb_height,rgb(9,14,28));bar();text(48,80,"STEVEOS NATIVE KERNEL",rgb(245,248,255),3);text(50,112,"CLICK AN APP OR USE F1 F2 F3",rgb(150,173,205),1);
 int y=150,w=150,h=105;fill(40,y,w,h,rgb(23,37,61));fill(220,y,w,h,rgb(23,37,61));fill(400,y,w,h,rgb(23,37,61));text(65,y+25,"CALCULATOR",rgb(240,245,252),1);text(65,y+50,"F1",rgb(150,173,205),1);text(245,y+25,"NOTEPAD",rgb(240,245,252),1);text(245,y+50,"F2",rgb(150,173,205),1);text(425,y+25,"FILES",rgb(240,245,252),1);text(425,y+50,"F3",rgb(150,173,205),1);
 fill(40,292,510,120,rgb(17,28,47));text(62,314,"SYSTEM",rgb(150,173,205),1);text(62,342,"MEMORY",rgb(150,173,205),1);num(62,365,memory_total/1024/1024,rgb(245,248,255),2);text(122,368,"MB",rgb(150,173,205),1);text(280,342,"HEAP",rgb(150,173,205),1);num(280,365,(heap_end>heap_current?(heap_end-heap_current):0)/1024/1024,rgb(245,248,255),2);text(340,368,"MB",rgb(150,173,205),1);text(62,391,"PERSISTENT NOTES: FIRMWARE NVRAM",rgb(210,232,220),1);
 fill(40,(int)fb_height-55,(int)fb_width-80,30,rgb(17,28,47));text(55,(int)fb_height-45,"F1 CALC",rgb(157,181,210),1);text(135,(int)fb_height-45,"F2 NOTEPAD",rgb(157,181,210),1);text(250,(int)fb_height-45,"F3 FILES",rgb(157,181,210),1);text(355,(int)fb_height-45,"ESC DESKTOP",rgb(157,181,210),1);native_pointer_show();}

static void draw_calc(void){native_pointer_hide();fill(0,0,(int)fb_width,(int)fb_height,rgb(12,18,31));bar();text(28,72,"CALCULATOR",rgb(245,248,255),2);fill(28,108,470,75,rgb(25,38,60));if(calc_len)text(48,132,calc_input,rgb(245,248,255),2);else num(48,132,calc_value,rgb(245,248,255),2);const char*keys[12]={"7","8","9","4","5","6","1","2","3","0","C","="};for(int i=0;i<12;i++){int x=28+(i%3)*110,y=205+(i/3)*65;fill(x,y,96,50,rgb(31,48,75));text(x+38,y+17,keys[i],rgb(238,244,252),1);}const char*ops[4]={"+","-","*","/"};for(int i=0;i<4;i++){int y=205+i*65;fill(370,y,96,50,rgb(31,48,75));text(410,y+17,ops[i],rgb(238,244,252),1);}text(28,490,"TYPE DIGITS AND OPERATORS   ENTER =   BACKSPACE",rgb(147,168,199),1);native_pointer_show();}

static void draw_notepad(void){native_pointer_hide();fill(0,0,(int)fb_width,(int)fb_height,rgb(12,18,31));bar();text(28,72,"NOTEPAD",rgb(245,248,255),2);fill(28,108,(int)fb_width-56,(int)fb_height-185,rgb(242,243,239));int x=42,y=126;for(size_t i=0;i<note_len;i++){char c=(char)note[i];if(c=='\n'||x>(int)fb_width-55){x=42;y+=15;if(c=='\n')continue;}glyph(x,y,c,rgb(20,25,32),1);x+=6;if(y>(int)fb_height-85)break;}if(note_cursor<=note_len&&((note_cursor/90)&1))fill(x,y,2,10,rgb(20,25,32));fill(28,(int)fb_height-62,(int)fb_width-56,35,rgb(25,39,62));text(45,(int)fb_height-50,"F5 SAVE",rgb(215,238,224),1);text(125,(int)fb_height-50,dirty?"UNSAVED":"SAVED",rgb(165,185,210),1);text(220,(int)fb_height-50,"BACKSPACE EDIT",rgb(165,185,210),1);text(355,(int)fb_height-50,"ESC DESKTOP",rgb(165,185,210),1);native_pointer_show();}

static void draw_builtin_image(void){const uint8_t*raw=_binary_build_boot_raw_start;const uint8_t*end=_binary_build_boot_raw_end;if((size_t)(end-raw)<8)return;uint32_t iw=*(const uint32_t*)raw,ih=*(const uint32_t*)(raw+4);const uint8_t*p=raw+8;if(!iw||!ih||(size_t)iw*ih*4+8>(size_t)(end-raw))return;uint32_t dw=520,dh=(uint64_t)ih*dw/iw;if(dh>500){dh=500;dw=(uint64_t)iw*dh/ih;}int ox=((int)fb_width-(int)dw)/2,oy=90;for(uint32_t yy=0;yy<dh;yy++){uint32_t sy=(uint64_t)yy*ih/dh;for(uint32_t xx=0;xx<dw;xx++){uint32_t sx=(uint64_t)xx*iw/dw;const uint8_t*v=p+((size_t)sy*iw+sx)*4;pixel(ox+(int)xx,oy+(int)yy,rgb(v[2],v[1],v[0]));}}}

static uint32_t rd32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int32_t rd32s(const uint8_t*p){return(int32_t)rd32(p);}
static uint16_t rd16(const uint8_t*p){return(uint16_t)p[0]|((uint16_t)p[1]<<8);}
static void draw_bmp(const uint8_t*data,size_t len){if(len<54||data[0]!='B'||data[1]!='M')return;uint32_t off=rd32(data+10),dib=rd32(data+14);int32_t sw=rd32s(data+18),sh=rd32s(data+22);uint16_t planes=rd16(data+26),bpp=rd16(data+28);uint32_t comp=rd32(data+30);if(dib<40||sw<=0||sh==0||planes!=1||(bpp!=24&&bpp!=32)||comp!=0||off>=len)return;uint32_t uh=(uint32_t)(sh<0?-sh:sh);uint32_t row=((uint32_t)sw*bpp+31)/32*4;uint64_t need=(uint64_t)off+(uint64_t)row*uh;if(need>len)return;uint32_t dw=520,dh=(uint64_t)uh*dw/(uint32_t)sw;if(dh>500){dh=500;dw=(uint64_t)sw*500/uh;}int ox=((int)fb_width-(int)dw)/2,oy=90;for(uint32_t y=0;y<dh;y++){uint32_t sy=(uint64_t)y*uh/dh;if(sh>0)sy=uh-1-sy;for(uint32_t x=0;x<dw;x++){uint32_t sx=(uint64_t)x*(uint32_t)sw/dw;const uint8_t*v=data+off+(uint64_t)sy*row+(uint64_t)sx*(bpp/8);pixel(ox+(int)x,oy+(int)y,rgb(v[2],v[1],v[0]));}}}

static size_t ws(const uint8_t*p,size_t n,size_t i){while(i<n&&(p[i]==' '||p[i]=='\n'||p[i]=='\r'||p[i]=='\t'))i++;return i;}
static uint32_t pnum(const uint8_t*p,size_t n,size_t*i){uint32_t v=0;*i=ws(p,n,*i);while(*i<n&&p[*i]>='0'&&p[*i]<='9'){v=v*10+(p[*i]-'0');(*i)++;}return v;}
static void draw_ppm(const uint8_t*data,size_t len){if(len<3||data[0]!='P'||data[1]!='6')return;size_t i=2;uint32_t w=pnum(data,len,&i),h=pnum(data,len,&i),maxv=pnum(data,len,&i);i=ws(data,len,i);if(!w||!h||maxv!=255||i>=len)return;uint64_t need=(uint64_t)i+w*(uint64_t)h*3;if(need>len)return;uint32_t dw=520,dh=(uint64_t)h*dw/w;if(dh>500){dh=500;dw=(uint64_t)w*500/h;}int ox=((int)fb_width-(int)dw)/2,oy=90;for(uint32_t y=0;y<dh;y++){uint32_t sy=(uint64_t)y*h/dh;for(uint32_t x=0;x<dw;x++){uint32_t sx=(uint64_t)x*w/dw;const uint8_t*v=data+i+((size_t)sy*w+sx)*3;pixel(ox+(int)x,oy+(int)y,rgb(v[0],v[1],v[2]));}}}

static void file_name_ascii(const STEVEOS_BOOT_FILE*f,char*out,size_t cap){if(!out||!cap)return;size_t i=0;if(f){while(i+1<cap&&i<STEVEOS_BOOT_FILE_NAME_MAX&&f->name[i]){uint16_t c=f->name[i++];out[i-1]=(c<128)?(char)c:'?';}out[i]=0;}else out[0]=0;}
static void draw_image(void){native_pointer_hide();fill(0,0,(int)fb_width,(int)fb_height,rgb(8,13,23));bar();text(28,72,"IMAGE VIEWER",rgb(245,248,255),2);if(selected_file>=0&&boot_files&&(uint64_t)selected_file<boot_file_count){STEVEOS_BOOT_FILE*f=&boot_files[selected_file];char name[64];file_name_ascii(f,name,sizeof(name));text(28,94,name,rgb(155,178,207),1);if(f->data&&f->size){const uint8_t*d=(const uint8_t*)(uintptr_t)f->data;if(f->size>=2&&d[0]=='B'&&d[1]=='M')draw_bmp(d,(size_t)f->size);else if(f->size>=2&&d[0]=='P'&&d[1]=='6')draw_ppm(d,(size_t)f->size);else draw_builtin_image();}else draw_builtin_image();}else draw_builtin_image();text(28,(int)fb_height-42,"ESC BACK   SUPPORTED: BMP PPM   BUILT-IN PNG RAW",rgb(160,181,210),1);native_pointer_show();}

static void draw_explorer(void){native_pointer_hide();fill(0,0,(int)fb_width,(int)fb_height,rgb(11,17,29));bar();text(28,72,"FILE EXPLORER",rgb(245,248,255),2);fill(20,108,(int)fb_width-40,42,rgb(25,40,63));text(38,122,"BOOT VOLUME / USB WHEN BOOTED FROM USB",rgb(220,235,248),1);int shown=0;for(uint64_t i=(uint64_t)file_scroll;i<boot_file_count&&shown<7;i++,shown++){STEVEOS_BOOT_FILE*f=&boot_files[i];char name[64];file_name_ascii(f,name,sizeof(name));int y=162+shown*46;fill(20,y,(int)fb_width-40,38,rgb(18,30,49));if(f->attributes&0x10)text(40,y+12,"DIR",rgb(175,198,220),1);else if(f->kind==1)text(40,y+12,"IMG",rgb(201,225,207),1);else if(f->kind==2)text(40,y+12,"TXT",rgb(190,208,229),1);else text(40,y+12,"FILE",rgb(170,190,215),1);text(90,y+12,name,rgb(232,239,248),1);if(f->size){text(400,y+12,"SIZE",rgb(145,168,198),1);num(442,y+12,f->size,rgb(232,239,248),1);}}
if(boot_file_count==0)text(40,178,"NO PRELOADED BOOT FILES",rgb(190,208,229),1);fill(20,(int)fb_height-68,(int)fb_width-40,40,rgb(25,39,62));text(38,(int)fb_height-54,"CLICK IMAGE TO OPEN   F5 NOTEPAD SAVES TO NVRAM",rgb(165,185,210),1);native_pointer_show();}
static void render(void){if(app==APP_DESKTOP)clear_desktop();else if(app==APP_CALC)draw_calc();else if(app==APP_NOTEPAD)draw_notepad();else if(app==APP_EXPLORER)draw_explorer();else draw_image();}

static uint64_t parse_num(const char*s){uint64_t v=0;while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}return v;}
static void calc_key(char c){if(c>='0'&&c<='9'){if(calc_len<sizeof(calc_input)-1)calc_input[calc_len++]=c;calc_input[calc_len]=0;return;}if(c=='C'){calc_len=0;calc_input[0]=0;calc_value=0;calc_has_value=0;return;}if(c=='+'||c=='-'||c=='*'||c=='/'){calc_value=parse_num(calc_input);calc_op=c;calc_has_value=1;calc_len=0;calc_input[0]=0;return;}if(c=='='&&calc_has_value){uint64_t b=parse_num(calc_input);if(calc_op=='+')calc_value+=b;else if(calc_op=='-')calc_value=calc_value>b?calc_value-b:0;else if(calc_op=='*')calc_value*=b;else if(calc_op=='/'&&b)calc_value/=b;calc_len=0;calc_input[0]=0;}}

static void load_note(const STEVEOS_BOOT_INFO*boot){note_len=note_cursor=0;if(!boot||!boot->uefi_get_variable)return;EFI_GET_VARIABLE get=(EFI_GET_VARIABLE)(uintptr_t)boot->uefi_get_variable;uint32_t attrs=0;uint64_t sz=NOTE_MAX;if(get((uint16_t*)note_name,(GUID*)&note_guid,&attrs,&sz,note)==0){if(sz>NOTE_MAX)sz=NOTE_MAX;note_len=(size_t)sz;}note[note_len]=0;dirty=0;}
static void save_note(const STEVEOS_BOOT_INFO*boot){if(!boot||!boot->uefi_set_variable)return;EFI_SET_VARIABLE set=(EFI_SET_VARIABLE)(uintptr_t)boot->uefi_set_variable;set((uint16_t*)note_name,(GUID*)&note_guid,7,note_len,note);dirty=0;}
static void note_key(uint8_t scan){if(scan==0x0e){if(note_cursor){for(size_t i=note_cursor-1;i<note_len;i++)note[i]=note[i+1];note_cursor--;note_len--;dirty=1;}return;}if(scan==0x1c){if(note_len<NOTE_MAX-1){for(size_t i=note_len;i>note_cursor;i--)note[i]=note[i-1];note[note_cursor++]='\n';note_len++;dirty=1;}return;}static const char map[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0};if(scan<128&&map[scan]&&note_len<NOTE_MAX-1){for(size_t i=note_len;i>note_cursor;i--)note[i]=note[i-1];note[note_cursor++]=(uint8_t)map[scan];note_len++;dirty=1;}}

static void click(uint32_t x,uint32_t y){if(app==APP_DESKTOP){if(inside(x,y,40,150,150,105))app=APP_CALC;else if(inside(x,y,220,150,150,105))app=APP_NOTEPAD;else if(inside(x,y,400,150,150,105))app=APP_EXPLORER;}else if(app==APP_EXPLORER){for(int row=0;row<7;row++){uint64_t idx=(uint64_t)file_scroll+(uint64_t)row;if(idx>=boot_file_count)break;if(!inside(x,y,20,162+row*46,(int)fb_width-40,38))continue;STEVEOS_BOOT_FILE*f=&boot_files[idx];if(f->kind==1){selected_file=(int)idx;app=APP_IMAGE;}return;}}else if(app==APP_CALC){for(int i=0;i<12;i++){int bx=28+(i%3)*110,by=205+(i/3)*65;if(inside(x,y,bx,by,96,50)){const char*k="7894561230C=";calc_key(k[i]);return;}}if(inside(x,y,370,205,96,50))calc_key('+');else if(inside(x,y,370,270,96,50))calc_key('-');else if(inside(x,y,370,335,96,50))calc_key('*');else if(inside(x,y,370,400,96,50))calc_key('/');}}

void kernel_main(STEVEOS_BOOT_INFO*boot){framebuffer=(uint32_t*)(uintptr_t)boot->framebuffer_base;fb_width=(uint32_t)boot->width;fb_height=(uint32_t)boot->height;fb_stride=(uint32_t)boot->pixels_per_scanline;fb_format=(uint32_t)boot->pixel_format;boot_files=(STEVEOS_BOOT_FILE*)(uintptr_t)boot->boot_files;boot_file_count=boot->boot_file_count;memory_init(boot);load_note(boot);render();for(;;){uint8_t scan=native_keyboard_read_scancode();uint8_t btn=native_pointer_buttons();if(btn&&!prev_buttons)click(native_pointer_x(),native_pointer_y());prev_buttons=btn;if(scan){if(scan&0x80){__asm__ __volatile__("pause");continue;}if(scan==0x01){app=APP_DESKTOP;selected_file=-1;render();continue;}if(scan==0x3b){app=APP_CALC;render();continue;}if(scan==0x3c){app=APP_NOTEPAD;render();continue;}if(scan==0x3d){app=APP_EXPLORER;render();continue;}if(scan==0x3f&&app==APP_NOTEPAD){save_note(boot);render();continue;}if(app==APP_CALC){if(scan==0x0e){if(calc_len)calc_input[--calc_len]=0;else calc_key('C');render();continue;}if(scan==0x2e){calc_key('C');render();continue;}char c=scan<128?((const char[128]){0,27,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0})[scan]:0;if((c>='0'&&c<='9')||c=='-'||c=='*'||c=='/')calc_key(c);else if(scan==0x1c)calc_key('=');render();}else if(app==APP_NOTEPAD){note_key(scan);render();}else if(app==APP_IMAGE&&scan==0x1c){app=APP_EXPLORER;selected_file=-1;render();continue;}else if(app==APP_EXPLORER&&scan==0x1c){app=APP_DESKTOP;render();continue;}}__asm__ __volatile__("pause");}}
