#include <stdint.h>
#include "../src/bootinfo.h"
#include "usb.h"

extern void native_default_isr(void);

typedef struct __attribute__((packed)){uint16_t limit;uint64_t base;} GDTR;
typedef struct __attribute__((packed)){uint16_t offset_low;uint16_t selector;uint8_t ist;uint8_t type_attr;uint16_t offset_mid;uint32_t offset_high;uint32_t zero;} IDT_GATE;
typedef struct __attribute__((packed)){uint16_t limit;uint64_t base;} IDTR;

static uint64_t gdt[3] __attribute__((aligned(8)))={0x0ULL,0x00AF9A000000FFFFULL,0x00AF92000000FFFFULL};
static IDT_GATE idt[256] __attribute__((aligned(16)));
static uint64_t page_tables[1024] __attribute__((aligned(4096)))={0};
static uint32_t *mouse_fb;
static uint32_t mouse_width,mouse_height,mouse_stride;
static uint32_t mouse_x,mouse_y;
static uint8_t mouse_present,mouse_buttons,cursor_visible;
static uint8_t pointer_scale=1;

typedef struct __attribute__((packed)){uint8_t signature[8];uint8_t checksum;uint8_t oemid[6];uint8_t revision;uint32_t rsdt;uint32_t length;uint64_t xsdt;} STEVEOS_RSDP;
static uint16_t acpi_pm1a_evt,acpi_pm1b_evt,acpi_pm1a_cnt,acpi_pm1b_cnt;static uint8_t acpi_pm1_len,acpi_slp_typa,acpi_slp_typb,acpi_ready,acpi_power_seen;
static inline uint16_t inw(uint16_t p){uint16_t v;__asm__ __volatile__("inw %1,%0":"=a"(v):"Nd"(p));return v;} static inline void outw(uint16_t p,uint16_t v){__asm__ __volatile__("outw %0,%1"::"a"(v),"Nd"(p));}
static uint32_t acpi_entry_count(const STEVEOS_RSDP*r){if(!r)return 0;if(r->revision>=2&&r->xsdt){uint32_t l=*(const uint32_t*)((const uint8_t*)(uintptr_t)r->xsdt+4);return l>=36?(l-36)/8:0;}if(r->rsdt){uint32_t l=*(const uint32_t*)((const uint8_t*)(uintptr_t)r->rsdt+4);return l>=36?(l-36)/4:0;}return 0;}
static uintptr_t acpi_find_table(const STEVEOS_RSDP*r,uint32_t sig){if(!r)return 0;uint32_t n=acpi_entry_count(r);if(r->revision>=2&&r->xsdt){const uint64_t*e=(const uint64_t*)((const uint8_t*)(uintptr_t)r->xsdt+36);for(uint32_t i=0;i<n;i++){const uint8_t*t=(const uint8_t*)(uintptr_t)e[i];if(t&&*(const uint32_t*)t==sig)return(uintptr_t)t;}}else if(r->rsdt){const uint32_t*e=(const uint32_t*)((const uint8_t*)(uintptr_t)r->rsdt+36);for(uint32_t i=0;i<n;i++){const uint8_t*t=(const uint8_t*)(uintptr_t)e[i];if(t&&*(const uint32_t*)t==sig)return(uintptr_t)t;}}return 0;}
static int acpi_parse_sleep_type(uintptr_t dsdt,uint8_t*out_a,uint8_t*out_b){if(!dsdt)return 0;uint32_t len=*(const uint32_t*)(dsdt+4);const uint8_t*p=(const uint8_t*)dsdt+36,*e=(const uint8_t*)dsdt+len;for(;p+10<e;p++){if(p[0]==0x08&&p[1]=='_'&&p[2]=='S'&&p[3]=='5'&&p[4]=='_'&&p[5]==0x12){const uint8_t*q=p+6;if(q>=e)break;uint8_t pkg=q[0],extra=(pkg>>6)&3;uint32_t pl=1+(pkg&0x3f);if(extra==1){if(q+2>=e)break;pl|=(uint32_t)q[1]<<6;}else if(extra==2){if(q+3>=e)break;pl|=(uint32_t)q[1]<<6|(uint32_t)q[2]<<14;}else if(extra==3){if(q+4>=e)break;pl|=(uint32_t)q[1]<<6|(uint32_t)q[2]<<14|(uint32_t)q[3]<<22;}const uint8_t*n=q+1+extra;if(n>=e||pl<2)break;n++;if(n>=e)break;uint8_t a=0,b=0;if(*n==0x0A){if(n+1>=e)break;a=n[1];n+=2;}else if(*n==0x0B){if(n+2>=e)break;a=n[1];n+=3;}else{a=*n;n++;}if(n>=e)break;if(*n==0x0A){if(n+1>=e)break;b=n[1];}else if(*n==0x0B){if(n+2>=e)break;b=n[1];}else b=*n;*out_a=a;*out_b=b;return 1;}}return 0;}
void native_power_init(const STEVEOS_BOOT_INFO*boot){acpi_ready=0;acpi_power_seen=0;if(!boot||!boot->acpi_rsdp)return;const STEVEOS_RSDP*r=(const STEVEOS_RSDP*)(uintptr_t)boot->acpi_rsdp;uintptr_t f=acpi_find_table(r,0x50434146u);if(!f)return;uint32_t len=*(const uint32_t*)(f+4);if(len<116)return;acpi_pm1a_evt=(uint16_t)*(const uint32_t*)(f+56);acpi_pm1b_evt=(uint16_t)*(const uint32_t*)(f+60);acpi_pm1a_cnt=(uint16_t)*(const uint32_t*)(f+64);acpi_pm1b_cnt=(uint16_t)*(const uint32_t*)(f+68);acpi_pm1_len=*(const uint8_t*)(f+88);uintptr_t dsdt=(uintptr_t)*(const uint32_t*)(f+40);if(len>=148)dsdt=(uintptr_t)*(const uint64_t*)(f+140);if(!dsdt||!acpi_pm1a_cnt)return;if(!acpi_parse_sleep_type(dsdt,&acpi_slp_typa,&acpi_slp_typb))return;if(acpi_pm1a_evt&&acpi_pm1_len>=4){uint16_t en=inw((uint16_t)(acpi_pm1a_evt+acpi_pm1_len/2));outw((uint16_t)(acpi_pm1a_evt+acpi_pm1_len/2),(uint16_t)(en|0x0100u));}if(acpi_pm1b_evt&&acpi_pm1_len>=4){uint16_t en=inw((uint16_t)(acpi_pm1b_evt+acpi_pm1_len/2));outw((uint16_t)(acpi_pm1b_evt+acpi_pm1_len/2),(uint16_t)(en|0x0100u));}acpi_ready=1;}
int native_power_button_event(void){if(!acpi_ready||!acpi_pm1a_evt)return 0;uint16_t st=inw(acpi_pm1a_evt);if(st&0x0100u){outw(acpi_pm1a_evt,0x0100u);if(acpi_power_seen)return 0;acpi_power_seen=1;return 1;}acpi_power_seen=0;return 0;}
void native_sleep(void){if(!acpi_ready)return;if(acpi_pm1a_evt)outw(acpi_pm1a_evt,0xFFFFu);if(acpi_pm1b_evt)outw(acpi_pm1b_evt,0xFFFFu);uint16_t a=(uint16_t)(((uint16_t)acpi_slp_typa)<<10)|0x2000u,b=(uint16_t)(((uint16_t)acpi_slp_typb)<<10)|0x2000u;__asm__ __volatile__("cli");if(acpi_pm1a_cnt)outw(acpi_pm1a_cnt,a);if(acpi_pm1b_cnt)outw(acpi_pm1b_cnt,b);for(;;)__asm__ __volatile__("hlt");}
static uint32_t cursor_saved[8*16];

static void load_gdt(void){GDTR gdtr={(uint16_t)(sizeof(gdt)-1),(uint64_t)gdt};__asm__ __volatile__("lgdt %0"::"m"(gdtr));__asm__ __volatile__("pushq $0x08\nleaq 1f(%%rip),%%rax\npushq %%rax\nlretq\n1:\nmovw $0x10,%%ax\nmovw %%ax,%%ds\nmovw %%ax,%%es\nmovw %%ax,%%ss\n":::"rax","memory");}
void native_gdt_init(void){load_gdt();}
static void idt_gate_set(IDT_GATE*g,uintptr_t a){g->offset_low=(uint16_t)a;g->selector=0x08;g->ist=0;g->type_attr=0x8E;g->offset_mid=(uint16_t)(a>>16);g->offset_high=(uint32_t)(a>>32);g->zero=0;}
void native_idt_init(void){uintptr_t h=(uintptr_t)&native_default_isr;for(int i=0;i<256;i++)idt_gate_set(&idt[i],h);IDTR idtr={(uint16_t)(sizeof(idt)-1),(uint64_t)idt};__asm__ __volatile__("lidt %0"::"m"(idtr));}
void native_paging_init(const STEVEOS_BOOT_INFO*boot){(void)boot;for(int i=0;i<512;i++){page_tables[i]=0;page_tables[512+i]=((uint64_t)i<<30)|0x83ULL;}uintptr_t pml4=(uintptr_t)&page_tables[0],pdpt=(uintptr_t)&page_tables[512];page_tables[0]=(uint64_t)pdpt|3ULL;__asm__ __volatile__("mov %0,%%cr3"::"r"(pml4):"memory");}
static inline uint8_t inb(uint16_t p){uint8_t v;__asm__ __volatile__("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void outb(uint16_t p,uint8_t v){__asm__ __volatile__("outb %0,%1"::"a"(v),"Nd"(p));}
static void io_wait(void){outb(0x80,0);}
static int wait_input_clear(void){for(uint32_t i=0;i<100000;i++){if(!(inb(0x64)&2))return 1;io_wait();}return 0;}
static int wait_output_full(void){for(uint32_t i=0;i<100000;i++){if(inb(0x64)&1)return 1;io_wait();}return 0;}
static int ps2_read_byte(uint8_t*v){if(!wait_output_full())return 0;*v=inb(0x60);return 1;}
static int ps2_send_mouse(uint8_t c){if(!wait_input_clear())return 0;outb(0x64,0xD4);if(!wait_input_clear())return 0;outb(0x60,c);uint8_t a=0;return ps2_read_byte(&a)&&a==0xFA;}
static int ps2_mouse_init(void){uint8_t status;if(!wait_input_clear())return 0;outb(0x64,0xA8);if(!wait_input_clear())return 0;outb(0x64,0x20);if(!ps2_read_byte(&status))return 0;status|=2;status&=(uint8_t)~0x20;if(!wait_input_clear())return 0;outb(0x64,0x60);if(!wait_input_clear())return 0;outb(0x60,status);return ps2_send_mouse(0xF6)&&ps2_send_mouse(0xF4);}
static const uint8_t cursor_shape[16]={0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF,0xE0,0xA0,0x80,0,0,0,0,0};
void native_pointer_hide(void){if(!mouse_fb||!cursor_visible)return;for(int y=0;y<16;y++)for(int x=0;x<8;x++){uint32_t px=mouse_x+(uint32_t)x,py=mouse_y+(uint32_t)y;if(px<mouse_width&&py<mouse_height)mouse_fb[(uint64_t)py*mouse_stride+px]=cursor_saved[y*8+x];}cursor_visible=0;}
void native_pointer_show(void){if(!mouse_fb||cursor_visible)return;for(int y=0;y<16;y++)for(int x=0;x<8;x++){uint32_t px=mouse_x+(uint32_t)x,py=mouse_y+(uint32_t)y;if(px<mouse_width&&py<mouse_height){cursor_saved[y*8+x]=mouse_fb[(uint64_t)py*mouse_stride+px];if(cursor_shape[y]&(uint8_t)(0x80u>>x))mouse_fb[(uint64_t)py*mouse_stride+px]=0xFFFFFFFFu;}}cursor_visible=1;}
void native_pointer_set_scale(uint8_t s){if(s<1)s=1;if(s>4)s=4;pointer_scale=s;}
void native_pointer_move(int32_t dx,int32_t dy,uint8_t buttons){if(dx>80||dx<-80||dy>80||dy<-80)return;if(dx>24)dx=24;else if(dx<-24)dx=-24;if(dy>24)dy=24;else if(dy<-24)dy=-24;dx*=pointer_scale;dy*=pointer_scale;mouse_buttons=(uint8_t)(buttons&7u);if(!mouse_fb||!mouse_width||!mouse_height)return;int32_t nx=(int32_t)mouse_x+dx,ny=(int32_t)mouse_y+dy;if(nx<0)nx=0;if(ny<0)ny=0;if(nx>=(int32_t)mouse_width)nx=(int32_t)mouse_width-1;if(ny>=(int32_t)mouse_height)ny=(int32_t)mouse_height-1;native_pointer_hide();mouse_x=(uint32_t)nx;mouse_y=(uint32_t)ny;native_pointer_show();}
uint32_t native_pointer_x(void){return mouse_x;} uint32_t native_pointer_y(void){return mouse_y;} uint8_t native_pointer_buttons(void){return mouse_buttons;}
void native_input_bind(const STEVEOS_BOOT_INFO*boot){if(!boot)return;mouse_fb=(uint32_t*)(uintptr_t)boot->framebuffer_base;mouse_width=(uint32_t)boot->width;mouse_height=(uint32_t)boot->height;mouse_stride=(uint32_t)boot->pixels_per_scanline;mouse_x=mouse_width/2;mouse_y=mouse_height/2;mouse_buttons=0;cursor_visible=0;mouse_present=(uint8_t)native_usb_init();if(!mouse_present)mouse_present=(uint8_t)ps2_mouse_init();native_pointer_show();}
static void native_mouse_poll(void){if(native_usb_mouse_present()){native_usb_poll();return;}if(!mouse_present)return;static uint8_t packet[3];static uint8_t index=0;while(inb(0x64)&1){uint8_t status=inb(0x64),value=inb(0x60);if(!(status&0x20))continue;if(index==0){if(!(value&8))continue;packet[0]=value;index=1;continue;}packet[index++]=value;if(index<3)continue;index=0;if(packet[0]&0xC0)continue;native_pointer_move((int8_t)packet[1],-(int8_t)packet[2],packet[0]&7u);}}
uint8_t native_keyboard_read_scancode(void){native_mouse_poll();if(!(inb(0x64)&1))return 0;uint8_t status=inb(0x64),value=inb(0x60);if(status&0x20)return 0;return value;}
void native_reboot(void){for(uint32_t i=0;i<100000;i++){if(!(inb(0x64)&2)){outb(0x64,0xFE);break;}}for(;;)__asm__ __volatile__("cli;hlt");}
void native_halt(void){for(;;)__asm__ __volatile__("cli;hlt");}
