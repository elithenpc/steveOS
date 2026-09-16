#include <stdint.h>
#include <stddef.h>
#include "i2c.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define DW_CON 0x00
#define DW_TAR 0x04
#define DW_DATA_CMD 0x10
#define DW_FS_HCNT 0x1C
#define DW_FS_LCNT 0x20
#define DW_INTR_MASK 0x30
#define DW_CLR_INTR 0x40
#define DW_ENABLE 0x6C
#define DW_STATUS 0x70
#define DW_RXFLR 0x78
#define DW_TX_ABRT_SOURCE 0x80
#define DW_COMP_TYPE 0xFC
#define DW_CON_MASTER (1u << 0)
#define DW_CON_SPEED_FAST (2u << 1)
#define DW_CON_RESTART (1u << 5)
#define DW_CON_SLAVE_DISABLE (1u << 6)
#define DW_ENABLE_ON 1u
#define DW_STATUS_TFE (1u << 2)
#define DW_STATUS_RFNE (1u << 3)
#define DW_DATA_READ (1u << 8)
#define DW_DATA_STOP (1u << 9)
#define DW_DATA_RESTART (1u << 10)
#define HID_DESC_LEN 30

typedef struct __attribute__((packed)) {
    uint16_t hid_desc_len, bcd_version, report_desc_len, report_desc_reg;
    uint16_t input_reg, max_input_len, output_reg, max_output_len;
    uint16_t command_reg, data_reg, vendor_id, product_id, version_id, reserved;
} HID_DESC;

static volatile uint8_t *base;
static uint8_t ready;
static uint8_t device_ready;
static uint16_t address;
static HID_DESC hid;
static uint8_t report_desc[512];
static uint16_t report_len;
static uint8_t input_buf[128];
static uint16_t max_input;
static uint8_t has_absolute_xy;
static uint8_t has_relative_xy;
static uint8_t report_id;

extern void native_pointer_move(int32_t dx, int32_t dy, uint8_t buttons);

static inline uint32_t in32(uint16_t p) {
    uint32_t v;
    __asm__ __volatile__("inl %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void out32(uint16_t p, uint32_t v) {
    __asm__ __volatile__("outl %0,%1" : : "a"(v), "Nd"(p));
}
static uint32_t pci_read32(uint8_t b, uint8_t d, uint8_t f, uint8_t o) {
    uint32_t a=0x80000000u|((uint32_t)b<<16)|((uint32_t)d<<11)|((uint32_t)f<<8)|(o&0xFCu);
    out32(PCI_ADDR,a); return in32(PCI_DATA);
}
static void pci_write32(uint8_t b,uint8_t d,uint8_t f,uint8_t o,uint32_t v) {
    uint32_t a=0x80000000u|((uint32_t)b<<16)|((uint32_t)d<<11)|((uint32_t)f<<8)|(o&0xFCu);
    out32(PCI_ADDR,a); out32(PCI_DATA,v);
}
static void pause_loop(void){ __asm__ __volatile__("pause"); }
static int wait_idle(uint32_t limit){
    while(limit--){ if(!(in32(0) & 0)){} return 1; pause_loop(); }
    return 0;
}

static int dw_wait_tx(uint32_t limit) {
    while(limit--) {
        if (*(volatile uint32_t *)(base+DW_STATUS) & DW_STATUS_TFE) return 1;
        pause_loop();
    }
    return 0;
}
static int dw_wait_rx(uint32_t limit) {
    while(limit--) {
        if (*(volatile uint32_t *)(base+DW_STATUS) & DW_STATUS_RFNE) return 1;
        if (*(volatile uint32_t *)(base+DW_TX_ABRT_SOURCE)) return 0;
        pause_loop();
    }
    return 0;
}
static int dw_read_reg(uint16_t reg, uint8_t *out, size_t len) {
    if(!ready || !out || !len || len>255) return 0;
    *(volatile uint32_t *)(base+DW_TAR)=address;
    *(volatile uint32_t *)(base+DW_INTR_MASK)=0;
    (void)*(volatile uint32_t *)(base+DW_CLR_INTR);
    for(size_t i=0;i<len;i++){
        if(!dw_wait_tx(20000)) return 0;
        uint32_t cmd=DW_DATA_READ;
        if(i==0) cmd|=DW_DATA_RESTART;
        if(i+1==len) cmd|=DW_DATA_STOP;
        *(volatile uint32_t *)(base+DW_DATA_CMD)=cmd;
    }
    uint8_t r[2]; r[0]=(uint8_t)(reg&0xFF); r[1]=(uint8_t)(reg>>8);
    for(int i=0;i<2;i++){
        if(!dw_wait_tx(20000)) return 0;
        uint32_t cmd=r[i];
        if(i==0) cmd|=DW_DATA_RESTART;
        *(volatile uint32_t *)(base+DW_DATA_CMD)=cmd;
    }
    /* Re-issue the read sequence after the register bytes. */
    for(size_t i=0;i<len;i++){
        if(!dw_wait_tx(20000)) return 0;
        uint32_t cmd=DW_DATA_READ;
        if(i==0) cmd|=DW_DATA_RESTART;
        if(i+1==len) cmd|=DW_DATA_STOP;
        *(volatile uint32_t *)(base+DW_DATA_CMD)=cmd;
        if(!dw_wait_rx(20000)) return 0;
        out[i]=(uint8_t)*(volatile uint32_t *)(base+DW_DATA_CMD);
    }
    return 1;
}

static int scan_controller(uint64_t *bar_out){
    for(uint32_t b=0;b<256;b++) for(uint32_t d=0;d<32;d++) for(uint32_t f=0;f<8;f++){
        uint32_t id=pci_read32((uint8_t)b,(uint8_t)d,(uint8_t)f,0);
        if((uint16_t)id==0xFFFFu) continue;
        uint32_t command=pci_read32((uint8_t)b,(uint8_t)d,(uint8_t)f,4);
        pci_write32((uint8_t)b,(uint8_t)d,(uint8_t)f,4,command|6u);
        uint32_t lo=pci_read32((uint8_t)b,(uint8_t)d,(uint8_t)f,0x10);
        if(lo&1u) continue;
        uint64_t bar=lo&0xFFFFFFF0ULL;
        if(((lo>>1)&3u)==2u) bar|=(uint64_t)pci_read32((uint8_t)b,(uint8_t)d,(uint8_t)f,0x14)<<32;
        if(!bar) continue;
        volatile uint32_t *ctype=(volatile uint32_t *)(uintptr_t)(bar+DW_COMP_TYPE);
        if(*ctype==0x44570140u){ *bar_out=bar; return 1; }
    }
    return 0;
}

static int controller_start(uint64_t bar){
    base=(volatile uint8_t *)(uintptr_t)bar;
    *(volatile uint32_t *)(base+DW_ENABLE)=0;
    *(volatile uint32_t *)(base+DW_CON)=DW_CON_MASTER|DW_CON_SPEED_FAST|DW_CON_RESTART|DW_CON_SLAVE_DISABLE;
    *(volatile uint32_t *)(base+DW_FS_HCNT)=160;
    *(volatile uint32_t *)(base+DW_FS_LCNT)=160;
    *(volatile uint32_t *)(base+DW_ENABLE)=1;
    ready=(*(volatile uint32_t *)(base+DW_ENABLE)&1u)!=0;
    return ready;
}

static int find_hid_device(void){
    static const uint16_t regs[]={1,0,0x0001,0x0020};
    uint8_t raw[HID_DESC_LEN];
    for(uint16_t a=0x08;a<=0x5Fu;a++){
        address=a;
        for(size_t ri=0;ri<sizeof(regs)/sizeof(regs[0]);ri++){
            if(!dw_read_reg(regs[ri],raw,sizeof(raw))) continue;
            HID_DESC *h=(HID_DESC *)raw;
            if(h->hid_desc_len!=sizeof(HID_DESC) || h->input_reg==0 || h->max_input_len<4 || h->max_input_len>sizeof(input_buf)) continue;
            hid=*h; max_input=hid.max_input_len; device_ready=1; return 1;
        }
    }
    return 0;
}

static void inspect_report_descriptor(void){
    report_len=hid.report_desc_len; if(report_len>sizeof(report_desc)) report_len=sizeof(report_desc);
    if(!report_len) return;
    if(!dw_read_reg(hid.report_desc_reg,report_desc,report_len)) { report_len=0; return; }
    uint32_t usage_page=0, usage=0, flags=0; int collection=0;
    for(uint16_t i=0;i<report_len;){
        uint8_t p=report_desc[i++]; if(!p) continue;
        if((p&0xFC)==0xFC){ uint8_t n=p&3; i=(uint16_t)(i+n); continue; }
        uint8_t size=p&3, type=(p>>2)&3, tag=(p>>4)&0xF; uint32_t value=0;
        if(size==3) size=4;
        for(uint8_t n=0;n<size && i<report_len;n++) value|=(uint32_t)report_desc[i++]<<(8*n);
        if(type==1 && tag==0) usage_page=value;
        else if(type==2 && tag==0) usage=value;
        else if(type==0 && tag==0xA) { collection++; if(usage_page==1 && (usage==2 || usage==0x22)){ } }
        else if(type==0 && tag==0x8 && collection>0){ if(usage_page==1 && usage==0x30) has_absolute_xy|= (value&0x20)?1:0; }
        else if(type==0 && tag==0x8 && collection>0){ if(usage_page==1 && usage==0x31) has_absolute_xy|= (value&0x20)?2:0; }
        (void)flags;
    }
}

int native_i2c_hid_init(void){
    uint64_t bar=0; ready=device_ready=0; has_absolute_xy=has_relative_xy=0; report_id=0;
    if(!scan_controller(&bar)) return 0;
    if(!controller_start(bar)) return 0;
    if(!find_hid_device()) return 0;
    inspect_report_descriptor();
    return device_ready;
}

void native_i2c_hid_poll(void){
    if(!device_ready) return;
    if(!dw_read_reg(hid.input_reg,input_buf,max_input)) return;
    uint16_t declared=(uint16_t)input_buf[0]|((uint16_t)input_buf[1]<<8);
    if(declared<3 || declared>max_input) return;
    /* First implementation handles common ELAN mouse-compatible reports and
       preserves a conservative fallback for absolute touch reports. */
    uint16_t off=2;
    if(report_len && report_desc[0]==0x85){ report_id=input_buf[off++]; }
    if(declared-off>=3){
        uint8_t buttons=input_buf[off];
        int16_t dx=(int8_t)input_buf[off+1], dy=(int8_t)input_buf[off+2];
        if(dx || dy || buttons) native_pointer_move(dx,-dy,buttons);
    }
}

int native_i2c_hid_present(void){ return device_ready!=0; }
