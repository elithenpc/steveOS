#include <stdint.h>
#include <stddef.h>
#include "usb.h"
#define PCI_CFG_ADDR 0xCF8
#define PCI_CFG_DATA 0xCFC
#define CAPLENGTH 0x00
#define HCSPARAMS1 0x04
#define HCSPARAMS2 0x08
#define HCCPARAMS1 0x10
#define DBOFF 0x14
#define RTSOFF 0x18
#define USBCMD 0x00
#define USBSTS 0x04
#define PAGESIZE 0x08
#define CRCR 0x18
#define DCBAAP 0x30
#define CONFIG 0x38
#define PORTSC_BASE 0x400
#define PORTSC_STRIDE 0x10
#define IMAN 0x20
#define IMOD 0x24
#define ERSTSZ 0x28
#define ERSTBA 0x30
#define ERDP 0x38
#define CMD_RUN (1u << 0)
#define CMD_RESET (1u << 1)
#define STS_HCH (1u << 0)
#define PORT_CCS (1u << 0)
#define PORT_PED (1u << 1)
#define PORT_PR (1u << 4)
#define PORT_SPEED_SHIFT 10
#define TRB_CYCLE (1u << 0)
#define TRB_TOGGLE_CYCLE (1u << 1)
#define TRB_CHAIN (1u << 4)
#define TRB_IOC (1u << 5)
#define TRB_IDT (1u << 6)
#define TRB_DIR_IN (1u << 16)
#define TRB_LINK 6u
#define TRB_SETUP 2u
#define TRB_DATA 3u
#define TRB_STATUS 4u
#define TRB_NORMAL 1u
#define TRB_ENABLE_SLOT 9u
#define TRB_ADDRESS_DEVICE 11u
#define TRB_CONFIGURE_ENDPOINT 12u
#define TRB_TRANSFER_EVENT 32u
#define TRB_COMMAND_COMPLETION 33u
#define CC_SUCCESS 1u
#define CC_SHORT 13u
#define MAX_TRBS 256
#define MAX_SLOTS 32
#define MAX_SCRATCHPADS 64
#define MAX_CONFIG 4096

typedef struct __attribute__((aligned(64))) { uint32_t d[4]; } TRB;
typedef struct __attribute__((packed, aligned(64))) { uint64_t base; uint32_t size; uint32_t reserved; } ERST_ENTRY;
static volatile uint8_t *base,*op,*runtime;
static volatile uint32_t *db;
static uint32_t port_count,ctx_size;
static uint8_t slot_id,ep_id,speed,ready;
static uint16_t ep_mps;
static TRB command_ring[MAX_TRBS] __attribute__((aligned(64)));
static TRB event_ring[MAX_TRBS] __attribute__((aligned(64)));
static TRB control_ring[MAX_TRBS] __attribute__((aligned(64)));
static TRB mouse_ring[MAX_TRBS] __attribute__((aligned(64)));
static ERST_ENTRY erst __attribute__((aligned(64)));
static uint64_t dcbaa[MAX_SLOTS] __attribute__((aligned(64)));
static uint8_t input_ctx[4096] __attribute__((aligned(64)));
static uint8_t output_ctx[4096] __attribute__((aligned(64)));
static uint64_t scratchpad_ptrs[MAX_SCRATCHPADS] __attribute__((aligned(64)));
static uint8_t scratchpads[MAX_SCRATCHPADS][4096] __attribute__((aligned(4096)));
static uint8_t config[MAX_CONFIG] __attribute__((aligned(65536)));
static uint8_t dev_desc[18] __attribute__((aligned(64)));
static uint8_t report[64] __attribute__((aligned(64)));
static size_t cmd_index,evt_index,ctl_index,mouse_index;
static uint8_t cmd_cycle=1,evt_cycle=1,ctl_cycle=1,mouse_cycle=1;
extern void native_pointer_move(int32_t dx,int32_t dy,uint8_t buttons);
static inline uint32_t in32(uint16_t p){uint32_t v;__asm__ __volatile__("inl %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void out32(uint16_t p,uint32_t v){__asm__ __volatile__("outl %0,%1"::"a"(v),"Nd"(p));}
static uint32_t pci_read32(uint8_t b,uint8_t d,uint8_t f,uint8_t o){uint32_t a=0x80000000u|((uint32_t)b<<16)|((uint32_t)d<<11)|((uint32_t)f<<8)|(o&0xFCu);out32(PCI_CFG_ADDR,a);return in32(PCI_CFG_DATA);}
static void pci_write32(uint8_t b,uint8_t d,uint8_t f,uint8_t o,uint32_t v){uint32_t a=0x80000000u|((uint32_t)b<<16)|((uint32_t)d<<11)|((uint32_t)f<<8)|(o&0xFCu);out32(PCI_CFG_ADDR,a);out32(PCI_CFG_DATA,v);}
static int find_xhci(uint64_t*bar){for(uint32_t bus=0;bus<256;bus++)for(uint32_t dev=0;dev<32;dev++)for(uint32_t fn=0;fn<8;fn++){uint32_t id=pci_read32((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,0);if((uint16_t)id==0xFFFFu)continue;uint32_t cr=pci_read32((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,8);if((uint8_t)(cr>>24)!=0x0C||(uint8_t)(cr>>16)!=0x03||(uint8_t)(cr>>8)!=0x30)continue;uint32_t cmd=pci_read32((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,4);pci_write32((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,4,cmd|6u);uint32_t lo=pci_read32((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,0x10);if(lo&1u)continue;uint64_t address=(uint64_t)(lo&0xFFFFFFF0u);if(((lo>>1)&3u)==2u)address|=(uint64_t)pci_read32((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,0x14)<<32;if(!address)continue;*bar=address;return 1;}return 0;}
static inline uint32_t rd(volatile uint8_t*p,uint32_t o){return *(volatile uint32_t*)(p+o);}static inline void wr(volatile uint8_t*p,uint32_t o,uint32_t v){*(volatile uint32_t*)(p+o)=v;}static inline void wr64(volatile uint8_t*p,uint32_t o,uint64_t v){*(volatile uint64_t*)(p+o)=v;}
static void ring_reset(TRB*r,size_t*i,uint8_t*c){for(size_t n=0;n<MAX_TRBS;n++)r[n].d[0]=r[n].d[1]=r[n].d[2]=r[n].d[3]=0;r[MAX_TRBS-1].d[0]=(uint32_t)(uintptr_t)r;r[MAX_TRBS-1].d[1]=(uint32_t)((uint64_t)(uintptr_t)r>>32);r[MAX_TRBS-1].d[3]=(TRB_LINK<<10)|TRB_TOGGLE_CYCLE|TRB_CYCLE;*i=0;*c=1;}
static TRB*ring_next(TRB*r,size_t*i,uint8_t*c){if(*i==MAX_TRBS-1){*i=0;*c^=1u;}TRB*t=&r[*i];t->d[0]=t->d[1]=t->d[2]=t->d[3]=0;++*i;return t;}
static void event_reset(void){for(size_t i=0;i<MAX_TRBS;i++)event_ring[i].d[0]=event_ring[i].d[1]=event_ring[i].d[2]=event_ring[i].d[3]=0;evt_index=0;evt_cycle=1;}
static int next_event(TRB*out){TRB*t=&event_ring[evt_index];if((t->d[3]&TRB_CYCLE)!=evt_cycle)return 0;*out=*t;t->d[0]=t->d[1]=t->d[2]=t->d[3]=0;if(++evt_index==MAX_TRBS){evt_index=0;evt_cycle^=1u;}wr64(runtime,ERDP,(uint64_t)(uintptr_t)&event_ring[evt_index]|(1ULL<<3));return 1;}
static int wait_command(uint32_t timeout){TRB e;while(timeout--){while(next_event(&e)){if(((e.d[3]>>10)&0x3F)!=TRB_COMMAND_COMPLETION)continue;return((e.d[2]>>24)&0xFFu)==CC_SUCCESS;}__asm__ __volatile__("pause");}return 0;}
static int wait_transfer(uint32_t timeout){TRB e;while(timeout--){while(next_event(&e)){if(((e.d[3]>>10)&0x3F)!=TRB_TRANSFER_EVENT)continue;if((uint8_t)(e.d[3]>>24)!=slot_id)continue;if(ep_id&&((uint8_t)((e.d[3]>>16)&0x1F)!=ep_id))continue;uint32_t cc=(e.d[2]>>24)&0xFFu;return cc==CC_SUCCESS||cc==CC_SHORT;}__asm__ __volatile__("pause");}return 0;}
static void legacy_handoff(void){uint32_t hcc=rd(base,HCCPARAMS1);uint16_t off=(uint16_t)(hcc>>16);if(!off)return;volatile uint8_t*p=base+((uint32_t)off<<2);for(int i=0;i<64&&p<base+0x1000;i++){uint32_t v=*(volatile uint32_t*)p;if((v&0xFFu)==1u){if(v&(1u<<16)){*(volatile uint32_t*)p=v|(1u<<24);for(uint32_t n=0;n<1000000&&(*(volatile uint32_t*)p&(1u<<16));n++)__asm__ __volatile__("pause");}return;}uint8_t next=(uint8_t)((v>>8)&0xFFu);if(!next)return;p+=(uint32_t)next<<2;}}
static int controller_start(uint64_t bar){base=(volatile uint8_t*)(uintptr_t)bar;uint8_t caplen=*(volatile uint8_t*)(base+CAPLENGTH);uint32_t hcs1=rd(base,HCSPARAMS1),hcs2=rd(base,HCSPARAMS2),hcc=rd(base,HCCPARAMS1);port_count=(hcs1>>24)&0xFFu;if(port_count>15)port_count=15;ctx_size=(hcc&(1u<<2))?64u:32u;uint32_t scratch_count=(((hcs2>>27)&0x1Fu)<<5)|((hcs2>>21)&0x1Fu);if(scratch_count>MAX_SCRATCHPADS)return 0;legacy_handoff();op=base+caplen;runtime=base+(rd(base,RTSOFF)&~0x1Fu);db=(volatile uint32_t*)(base+(rd(base,DBOFF)&~3u));wr(op,USBCMD,rd(op,USBCMD)&~CMD_RUN);for(uint32_t i=0;i<1000000;i++){if(rd(op,USBSTS)&STS_HCH)break;__asm__ __volatile__("pause");}wr(op,USBCMD,rd(op,USBCMD)|CMD_RESET);for(uint32_t i=0;i<2000000;i++){if(!(rd(op,USBCMD)&CMD_RESET)&&(rd(op,USBSTS)&STS_HCH))break;if(i==1999999)return 0;__asm__ __volatile__("pause");}wr(op,PAGESIZE,1);if(!(rd(op,PAGESIZE)&1u))return 0;for(size_t i=0;i<MAX_SLOTS;i++)dcbaa[i]=0;if(scratch_count){dcbaa[0]=(uint64_t)(uintptr_t)scratchpad_ptrs;for(uint32_t i=0;i<scratch_count;i++)scratchpad_ptrs[i]=(uint64_t)(uintptr_t)scratchpads[i];}ring_reset(command_ring,&cmd_index,&cmd_cycle);ring_reset(control_ring,&ctl_index,&ctl_cycle);ring_reset(mouse_ring,&mouse_index,&mouse_cycle);event_reset();erst.base=(uint64_t)(uintptr_t)event_ring;erst.size=MAX_TRBS;erst.reserved=0;wr64(op,DCBAAP,(uint64_t)(uintptr_t)dcbaa);wr64(op,CRCR,(uint64_t)(uintptr_t)command_ring|1u);wr(runtime,IMAN,0);wr(runtime,IMOD,0);wr(runtime,ERSTSZ,1);wr64(runtime,ERSTBA,(uint64_t)(uintptr_t)&erst);wr64(runtime,ERDP,(uint64_t)(uintptr_t)event_ring);wr(op,CONFIG,hcs1&0xFFu);wr(op,USBCMD,rd(op,USBCMD)|CMD_RUN);for(uint32_t i=0;i<1000000;i++){if(!(rd(op,USBSTS)&STS_HCH))return 1;__asm__ __volatile__("pause");}return 0;}
static void submit_cmd(uint32_t a,uint32_t b,uint32_t c,uint32_t ctl){TRB*t=ring_next(command_ring,&cmd_index,&cmd_cycle);t->d[0]=a;t->d[1]=b;t->d[2]=c;t->d[3]=ctl|(cmd_cycle?TRB_CYCLE:0);db[0]=0;}
static int enable_slot(void){submit_cmd(0,0,0,TRB_ENABLE_SLOT<<10);TRB e;for(uint32_t t=0;t<2000000;t++){while(next_event(&e)){if(((e.d[3]>>10)&0x3F)!=TRB_COMMAND_COMPLETION)continue;if(((e.d[2]>>24)&0xFFu)!=CC_SUCCESS)return 0;slot_id=(uint8_t)(e.d[3]>>24);return slot_id!=0;}__asm__ __volatile__("pause");}return 0;}
static uint32_t*ctx(void*c,uint32_t i){return(uint32_t*)((uint8_t*)c+(size_t)i*ctx_size);}
static int address_device(uint8_t port,uint8_t device_speed){for(size_t i=0;i<sizeof(input_ctx);i++)input_ctx[i]=0;for(size_t i=0;i<sizeof(output_ctx);i++)output_ctx[i]=0;dcbaa[slot_id]=(uint64_t)(uintptr_t)output_ctx;uint32_t*icc=ctx(input_ctx,0);icc[0]=3u;uint32_t*slot=ctx(input_ctx,1);slot[0]=((uint32_t)device_speed<<20)|(1u<<27);slot[1]=(uint32_t)port<<16;uint32_t*ep0=ctx(input_ctx,2);uint32_t mps=device_speed==1?8u:64u;ep0[1]=(3u<<1)|(4u<<3)|(mps<<16);ep0[2]=(uint32_t)(uintptr_t)control_ring|1u;ep0[3]=(uint32_t)((uint64_t)(uintptr_t)control_ring>>32);ep0[4]=8u;submit_cmd((uint32_t)(uintptr_t)input_ctx,(uint32_t)((uint64_t)(uintptr_t)input_ctx>>32),0,TRB_ADDRESS_DEVICE<<10);return wait_command(2000000);}
static void setup_trb(TRB*t,uint8_t bm,uint8_t req,uint16_t value,uint16_t index,uint16_t length){t->d[0]=(uint32_t)bm|((uint32_t)req<<8)|((uint32_t)value<<16);t->d[1]=(uint32_t)index|((uint32_t)length<<16);uint32_t trt=length?((bm&0x80u)?3u:2u):0u;t->d[2]=8u|(trt<<16);t->d[3]=(TRB_SETUP<<10)|TRB_IDT|(ctl_cycle?TRB_CYCLE:0);}
static int control_transfer(uint8_t bm,uint8_t req,uint16_t value,uint16_t index,void*buffer,uint16_t length){TRB*setup=ring_next(control_ring,&ctl_index,&ctl_cycle);setup_trb(setup,bm,req,value,index,length);int data_in=(bm&0x80u)!=0;if(length){TRB*data=ring_next(control_ring,&ctl_index,&ctl_cycle);data->d[0]=(uint32_t)(uintptr_t)buffer;data->d[1]=(uint32_t)((uint64_t)(uintptr_t)buffer>>32);data->d[2]=length;data->d[3]=(TRB_DATA<<10)|(data_in?TRB_DIR_IN:0)|TRB_CHAIN|(ctl_cycle?TRB_CYCLE:0);}TRB*status=ring_next(control_ring,&ctl_index,&ctl_cycle);status->d[3]=(TRB_STATUS<<10)|TRB_IOC|((!length||!data_in)?TRB_DIR_IN:0)|(ctl_cycle?TRB_CYCLE:0);db[slot_id]=1;return wait_transfer(2000000);}
static int parse_mouse(const uint8_t*buf,size_t len,uint8_t*iface,uint8_t*endpoint,uint16_t*mps,uint8_t*interval,uint8_t*cfg){if(len<9||buf[1]!=2)return 0;*cfg=buf[5];uint8_t current=0xFF,subclass=0,protocol=0;for(size_t p=0;p+2<=len;){uint8_t dl=buf[p],dt=buf[p+1];if(dl<2||p+dl>len)break;if(dt==4&&dl>=9){current=buf[p+2];subclass=buf[p+6];protocol=buf[p+7];}else if(dt==5&&dl>=7&&current!=0xFF&&subclass==1&&protocol==2){uint8_t addr=buf[p+2];if((addr&0x80u)&&((buf[p+3]&3u)==3u)){*iface=current;*endpoint=addr;*mps=(uint16_t)buf[p+4]|((uint16_t)buf[p+5]<<8);*mps&=0x7FFu;*interval=buf[p+6];return 1;}}p+=dl;}return 0;}
static int configure_mouse(uint8_t endpoint,uint16_t mps,uint8_t interval){ep_id=(uint8_t)(((endpoint&0x0Fu)<<1)|((endpoint&0x80u)?0:1));ep_mps=mps?mps:8;if(ep_mps>64)ep_mps=64;uint32_t context_index=(uint32_t)ep_id+1;if((size_t)(context_index+1)*ctx_size>sizeof(input_ctx))return 0;uint32_t*icc=ctx(input_ctx,0);icc[0]=1u|(1u<<(ep_id+1));uint32_t*slot=ctx(input_ctx,1);slot[0]=((uint32_t)speed<<20)|((uint32_t)ep_id<<27);uint32_t*ep0=ctx(input_ctx,2);ep0[1]=(3u<<1)|(4u<<3)|(64u<<16);ep0[2]=(uint32_t)(uintptr_t)control_ring|1u;ep0[3]=(uint32_t)((uint64_t)(uintptr_t)control_ring>>32);ep0[4]=8u;uint32_t interval_field=interval?interval:1;if(speed<3){interval_field=3;uint32_t period=interval?interval:1;while(period>1&&interval_field<16){period=(period+1)>>1;++interval_field;}}if(interval_field>16)interval_field=16;uint32_t*ep=ctx(input_ctx,context_index);ep[0]=interval_field<<16;ep[1]=((uint32_t)ep_mps<<16)|(3u<<1)|(7u<<3);ep[2]=(uint32_t)(uintptr_t)mouse_ring|1u;ep[3]=(uint32_t)((uint64_t)(uintptr_t)mouse_ring>>32);ep[4]=ep_mps;submit_cmd((uint32_t)(uintptr_t)input_ctx,(uint32_t)((uint64_t)(uintptr_t)input_ctx>>32),0,TRB_CONFIGURE_ENDPOINT<<10);return wait_command(2000000);}
static void submit_mouse_report(void){TRB*t=ring_next(mouse_ring,&mouse_index,&mouse_cycle);t->d[0]=(uint32_t)(uintptr_t)report;t->d[1]=(uint32_t)((uint64_t)(uintptr_t)report>>32);t->d[2]=ep_mps;t->d[3]=(TRB_NORMAL<<10)|TRB_IOC|(mouse_cycle?TRB_CYCLE:0);db[slot_id]=ep_id;}
int native_usb_init(void){uint64_t bar=0;ready=0;if(!find_xhci(&bar))return 0;if(!controller_start(bar))return 0;for(uint32_t port=1;port<=port_count;port++){uint32_t off=PORTSC_BASE+(port-1)*PORTSC_STRIDE,p=rd(op,off);if(!(p&PORT_CCS))continue;wr(op,off,p|PORT_PR);int reset_ok=0;for(uint32_t i=0;i<2000000;i++){p=rd(op,off);if(!(p&PORT_PR)&&(p&PORT_PED)){reset_ok=1;break;}if(!(p&PORT_CCS))break;__asm__ __volatile__("pause");}if(!reset_ok)continue;speed=(uint8_t)((p>>PORT_SPEED_SHIFT)&0xFu);if(!speed||speed>4)continue;if(!enable_slot())continue;if(!address_device((uint8_t)port,speed))continue;if(!control_transfer(0x80,6,0x0100,0,dev_desc,sizeof(dev_desc)))continue;if(!control_transfer(0x80,6,0x0200,0,config,9))continue;uint16_t total=(uint16_t)config[2]|((uint16_t)config[3]<<8);if(total<9)total=9;if(total>MAX_CONFIG)total=MAX_CONFIG;if(!control_transfer(0x80,6,0x0200,0,config,total))continue;uint8_t iface=0,ep=0,interval=0,cfg=0;uint16_t mps=0;if(!parse_mouse(config,total,&iface,&ep,&mps,&interval,&cfg))continue;if(!control_transfer(0x00,9,cfg,0,NULL,0))continue;if(!control_transfer(0x21,0x0A,0,iface,NULL,0))continue;if(!control_transfer(0x21,11,0,iface,NULL,0))continue;if(!configure_mouse(ep,mps,interval))continue;for(size_t i=0;i<sizeof(report);i++)report[i]=0;ready=1;submit_mouse_report();return 1;}return 0;}
void native_usb_poll(void){if(!ready)return;TRB e;while(next_event(&e)){if(((e.d[3]>>10)&0x3F)!=TRB_TRANSFER_EVENT)continue;if((uint8_t)(e.d[3]>>24)!=slot_id)continue;if((uint8_t)((e.d[3]>>16)&0x1F)!=ep_id)continue;uint32_t cc=(e.d[2]>>24)&0xFFu;if(cc==CC_SUCCESS||cc==CC_SHORT)native_pointer_move((int8_t)report[1],-(int8_t)report[2],report[0]);submit_mouse_report();}}
int native_usb_mouse_present(void){return ready!=0;}
