#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "kernel.h"
#include "bootinfo.h"
#include "network.h"
#include "fs.h"

extern const unsigned char _binary_build_native_kernel_raw_start[];
extern const unsigned char _binary_build_native_kernel_raw_end[];
static EFI_HANDLE steveos_boot_device;
static EFI_HANDLE steveos_image_handle;
static EFI_STATUS steveos_install_self(UINT64 target_index);
static EFI_STATUS steveos_install_server(UINT64 target_index);
static EFI_STATUS steveos_launch_app(const CHAR16 *path);
EFI_STATUS steveos_write_boot_text(const CHAR16 *path,const void *data,UINTN size);
EFI_STATUS steveos_install_windows_app(const CHAR16 *source_path);
EFI_STATUS steveos_run_windows_app(const CHAR16 *source_path);

typedef void (*STEVEOS_NATIVE_ENTRY)(STEVEOS_BOOT_INFO *boot, void *stack_top);
#define STEVEOS_KERNEL_LOAD_ADDRESS 0x00200000ULL
#define STEVEOS_IMAGE_LOAD_LIMIT (2ULL * 1024ULL * 1024ULL)
#define STEVEOS_TEXT_LOAD_LIMIT (256ULL * 1024ULL)

typedef struct {
    uint16_t name[STEVEOS_BOOT_FILE_NAME_MAX];
    uint64_t size;
    uint64_t data;
    uint32_t attributes;
    uint32_t kind;
} STEVEOS_BOOT_FILE_UEFI;

static EFI_STATUS get_memory_map(EFI_MEMORY_DESCRIPTOR **map,
                                 UINTN *map_size,
                                 UINTN *map_key,
                                 UINTN *descriptor_size,
                                 UINT32 *descriptor_version) {
    UINTN size = 0, key = 0, desc_size = 0;
    UINT32 version = 0;
    EFI_STATUS st = uefi_call_wrapper(BS->GetMemoryMap, 5,
                                       &size, NULL, &key, &desc_size, &version);
    if (st != EFI_BUFFER_TOO_SMALL || desc_size == 0)
        return st;
    size += desc_size * 4;
    *map = AllocatePool(size);
    if (!*map)
        return EFI_OUT_OF_RESOURCES;
    st = uefi_call_wrapper(BS->GetMemoryMap, 5,
                           &size, *map, &key, &desc_size, &version);
    if (EFI_ERROR(st)) {
        FreePool(*map);
        *map = NULL;
        return st;
    }
    *map_size = size;
    *map_key = key;
    *descriptor_size = desc_size;
    *descriptor_version = version;
    return EFI_SUCCESS;
}

static EFI_PHYSICAL_ADDRESS allocate_kernel_pages(UINTN pages) {
    EFI_PHYSICAL_ADDRESS address = STEVEOS_KERNEL_LOAD_ADDRESS;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePages, 4,
                                      AllocateAddress, EfiLoaderData,
                                      pages, &address);
    return EFI_ERROR(st) ? 0 : address;
}

static EFI_PHYSICAL_ADDRESS allocate_pages(UINTN pages) {
    EFI_PHYSICAL_ADDRESS address = 0;
    EFI_STATUS st = uefi_call_wrapper(BS->AllocatePages, 4,
                                      AllocateAnyPages, EfiLoaderData,
                                      pages, &address);
    return EFI_ERROR(st) ? 0 : address;
}

static UINT64 find_acpi_rsdp(void) {
    static EFI_GUID acpi20 =
        {0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
    static EFI_GUID acpi10 =
        {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    UINT64 fallback = 0;
    for (UINTN i = 0; i < ST->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE *entry = &ST->ConfigurationTable[i];
        if (!CompareGuid(&entry->VendorGuid, &acpi20))
            return (UINT64)(UINTN)entry->VendorTable;
        if (!CompareGuid(&entry->VendorGuid, &acpi10))
            fallback = (UINT64)(UINTN)entry->VendorTable;
    }
    return fallback;
}

static int has_ext(const CHAR16 *name, const CHAR16 *ext) {
    UINTN n = StrLen((CHAR16 *)name), e = StrLen((CHAR16 *)ext);
    if (n < e) return 0;
    name += n - e;
    for (UINTN i = 0; i < e; ++i) {
        CHAR16 a = name[i], b = ext[i];
        if (a >= L'A' && a <= L'Z') a = (CHAR16)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = (CHAR16)(b - L'A' + L'a');
        if (a != b) return 0;
    }
    return 1;
}

static UINT32 file_kind(const CHAR16 *name) {
    if (has_ext(name, L".bmp") || has_ext(name, L".png") ||
        has_ext(name, L".jpg") || has_ext(name, L".jpeg") ||
        has_ext(name, L".ppm")) return 1;
    if (has_ext(name, L".exe")) return 4;
    if (has_ext(name, L".txt") || has_ext(name, L".md") ||
        has_ext(name, L".log") || has_ext(name, L".html") || has_ext(name, L".htm") ||
        has_ext(name, L".css") || has_ext(name, L".json") || has_ext(name, L".xml") ||
        has_ext(name, L".csv") || has_ext(name, L".c") || has_ext(name, L".h") ||
        has_ext(name, L".hxx") || has_ext(name, L".cpp") || has_ext(name, L".py") ||
        has_ext(name, L".sh") || has_ext(name, L".ini") || has_ext(name, L".cfg")) return 2;
    return 0;
}

static void copy_path(const CHAR16 *prefix,const CHAR16 *name,CHAR16 *out){
    UINTN n=0;
    while(prefix&&prefix[n]&&n+1<STEVEOS_BOOT_FILE_NAME_MAX){out[n]=prefix[n];n++;}
    if(n&&out[n-1]!=L'/'&&n+1<STEVEOS_BOOT_FILE_NAME_MAX)out[n++]=L'/';
    UINTN i=0;
    while(name&&name[i]&&n+1<STEVEOS_BOOT_FILE_NAME_MAX){out[n++]=name[i++];}
    out[n]=0;
}
static void snapshot_dir(EFI_FILE_PROTOCOL *dir,
                         STEVEOS_BOOT_FILE_UEFI *files,
                         UINTN *count,
                         VOID *buf,
                         UINTN buf_size,
                         const CHAR16 *prefix,
                         UINTN depth){
    if(!dir||!files||!count||!buf||depth>7||*count>=STEVEOS_MAX_BOOT_FILES)return;
    uefi_call_wrapper(dir->SetPosition,2,dir,0);
    while(*count<STEVEOS_MAX_BOOT_FILES){
        UINTN read_size=buf_size;
        EFI_STATUS st=uefi_call_wrapper(dir->Read,3,dir,&read_size,buf);
        if(EFI_ERROR(st)||read_size==0)break;
        EFI_FILE_INFO *info=(EFI_FILE_INFO*)buf;
        if(read_size<sizeof(EFI_FILE_INFO)||info->Size>read_size)continue;
        CHAR16 path[STEVEOS_BOOT_FILE_NAME_MAX];
        copy_path(prefix,info->FileName,path);
        STEVEOS_BOOT_FILE_UEFI *out=&files[*count];
        ZeroMem(out,sizeof(*out));
        UINTN j=0;
        while(j+1<STEVEOS_BOOT_FILE_NAME_MAX&&path[j]){
            out->name[j]=path[j];
            j++;
        }
        out->name[j]=0;
        out->size=info->FileSize;
        out->attributes=(UINT32)info->Attribute;
        out->kind=(info->Attribute&EFI_FILE_DIRECTORY)?3u:file_kind(info->FileName);

        if(info->Attribute&EFI_FILE_DIRECTORY){
            (*count)++;
            if(depth<7){
                EFI_FILE_PROTOCOL *child=NULL;
                st=uefi_call_wrapper(dir->Open,5,dir,&child,info->FileName,EFI_FILE_MODE_READ,0);
                if(!EFI_ERROR(st)&&child){
                    snapshot_dir(child,files,count,buf,buf_size,path,(UINTN)(depth+1));
                    uefi_call_wrapper(child->Close,1,child);
                }
            }
            continue;
        }

        if(((out->kind==1&&out->size>0&&out->size<=STEVEOS_IMAGE_LOAD_LIMIT) ||
            (out->kind==2&&out->size>0&&out->size<=STEVEOS_TEXT_LOAD_LIMIT))){
            EFI_FILE_PROTOCOL *file=NULL;
            st=uefi_call_wrapper(dir->Open,5,dir,&file,info->FileName,EFI_FILE_MODE_READ,0);
            if(!EFI_ERROR(st)&&file){
                UINTN pages=(UINTN)((out->size+4095)/4096);
                EFI_PHYSICAL_ADDRESS storage=allocate_pages(pages);
                if(storage){
                    UINTN data_size=(UINTN)out->size;
                    st=uefi_call_wrapper(file->Read,3,file,&data_size,(VOID*)(UINTN)storage);
                    if(!EFI_ERROR(st)&&data_size==out->size)out->data=storage;
                    else uefi_call_wrapper(BS->FreePages,2,storage,pages);
                }
                uefi_call_wrapper(file->Close,1,file);
            }
        }
        (*count)++;
    }
}

static EFI_STATUS snapshot_boot_files(EFI_HANDLE image_handle, STEVEOS_BOOT_INFO *boot){
    EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;
    EFI_FILE_PROTOCOL *root=NULL;
    EFI_STATUS st;
    if(!boot)return EFI_INVALID_PARAMETER;
    st=uefi_call_wrapper(BS->HandleProtocol,3,image_handle,&gEfiLoadedImageProtocolGuid,(VOID**)&loaded);
    if(EFI_ERROR(st)||!loaded||!loaded->DeviceHandle)return st;
    st=uefi_call_wrapper(BS->HandleProtocol,3,loaded->DeviceHandle,&gEfiSimpleFileSystemProtocolGuid,(VOID**)&fs);
    if(EFI_ERROR(st)||!fs)return st;
    st=uefi_call_wrapper(fs->OpenVolume,2,fs,&root);
    if(EFI_ERROR(st)||!root)return st;

    STEVEOS_BOOT_FILE_UEFI *files=AllocatePool(sizeof(*files)*STEVEOS_MAX_BOOT_FILES);
    if(!files){
        uefi_call_wrapper(root->Close,1,root);
        return EFI_OUT_OF_RESOURCES;
    }
    ZeroMem(files,sizeof(*files)*STEVEOS_MAX_BOOT_FILES);

    const UINTN buf_size=8192;
    VOID *buf=AllocatePool(buf_size);
    if(!buf){
        FreePool(files);
        uefi_call_wrapper(root->Close,1,root);
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN count=0;
    snapshot_dir(root,files,&count,buf,buf_size,(const CHAR16*)L"",0);

    boot->boot_files=(UINT64)(UINTN)files;
    boot->boot_file_count=count;
    boot->boot_device_handle=(UINT64)(UINTN)loaded->DeviceHandle;
    steveos_boot_device=loaded->DeviceHandle;
    steveos_image_handle=image_handle;
    FreePool(buf);
    uefi_call_wrapper(root->Close,1,root);
    return EFI_SUCCESS;
}

typedef struct {
    EFI_HANDLE handle;
    UINT64 blocks;
    UINT32 block_size;
    BOOLEAN removable;
    BOOLEAN present;
} STEVEOS_TARGET_INTERNAL;

static EFI_STATUS steveos_target_handles(STEVEOS_TARGET_INTERNAL *out,UINTN cap,UINTN *count){
    if(!out||!count)return EFI_INVALID_PARAMETER;
    *count=0;
    UINTN sz=0;EFI_HANDLE *handles=NULL;
    EFI_STATUS st=uefi_call_wrapper(BS->LocateHandle,5,ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&sz,NULL);
    if(st!=EFI_BUFFER_TOO_SMALL)return st;
    handles=AllocatePool(sz);if(!handles)return EFI_OUT_OF_RESOURCES;
    st=uefi_call_wrapper(BS->LocateHandle,5,ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&sz,handles);
    if(!EFI_ERROR(st)){
        UINTN total=sz/sizeof(EFI_HANDLE);
        for(UINTN i=0;i<total&&*count<cap;i++){
            if(handles[i]==steveos_boot_device)continue;
            EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;
            EFI_BLOCK_IO_PROTOCOL *bio=NULL;
            if(EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol,3,handles[i],&gEfiSimpleFileSystemProtocolGuid,(VOID**)&fs))||!fs)continue;
            if(EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol,3,handles[i],&gEfiBlockIoProtocolGuid,(VOID**)&bio))||!bio||!bio->Media||!bio->Media->MediaPresent)continue;
            STEVEOS_TARGET_INTERNAL*t=&out[*count];
            t->handle=handles[i];
            t->blocks=(UINT64)bio->Media->LastBlock+1ULL;
            t->block_size=(UINT32)bio->Media->BlockSize;
            t->removable=bio->Media->RemovableMedia;
            t->present=bio->Media->MediaPresent;
            (*count)++;
        }
    }
    FreePool(handles);
    return st;
}

UINT64 steveos_list_install_targets(STEVEOS_INSTALL_TARGET *out,UINT64 capacity){
    if(!out||capacity==0)return 0;
    STEVEOS_TARGET_INTERNAL tmp[32];UINTN cap=capacity>32?32:(UINTN)capacity,count=0;
    if(EFI_ERROR(steveos_target_handles(tmp,cap,&count)))return 0;
    for(UINTN i=0;i<count;i++){
        ZeroMem(&out[i],sizeof(out[i]));
        out[i].handle=(UINT64)(UINTN)tmp[i].handle;
        out[i].blocks=tmp[i].blocks;
        out[i].block_size=tmp[i].block_size;
        out[i].removable=tmp[i].removable?1:0;
        out[i].present=tmp[i].present?1:0;
        out[i].filesystem=1;
    }
    return (UINT64)count;
}

static EFI_STATUS steveos_register_boot_option(EFI_HANDLE target){
    if(!target)return EFI_INVALID_PARAMETER;
    EFI_DEVICE_PATH *dp=FileDevicePath(target,L"\\EFI\\BOOT\\BOOTX64.EFI");
    if(!dp)return EFI_OUT_OF_RESOURCES;
    UINTN dpsz=DevicePathSize(dp);
    UINTN slot=0;
    CHAR16 name[16];
    UINT32 attrs=0;UINTN sz=0;UINT8 probe[4];
    for(UINTN n=0;n<0x10000;n++){
        SPrint(name,sizeof(name),L"Boot%04x",(UINT16)n);
        sz=sizeof(probe);
        EFI_STATUS gst=uefi_call_wrapper(RT->GetVariable,5,name,&EfiGlobalVariable,&attrs,&sz,probe);
        if(gst==EFI_NOT_FOUND){slot=n;break;}
        if(n==0xFFFF){FreePool(dp);return EFI_OUT_OF_RESOURCES;}
    }
    CHAR16 desc[]=L"SteveOS";
    UINTN desc_bytes=sizeof(desc);
    UINTN option_size=sizeof(UINT32)+sizeof(UINT16)+desc_bytes+dpsz;
    UINT8 *option=AllocatePool(option_size);
    if(!option){FreePool(dp);return EFI_OUT_OF_RESOURCES;}
    ZeroMem(option,option_size);
    *(UINT32*)option=LOAD_OPTION_ACTIVE;
    *(UINT16*)(option+sizeof(UINT32))=(UINT16)dpsz;
    CopyMem(option+sizeof(UINT32)+sizeof(UINT16),desc,desc_bytes);
    CopyMem(option+sizeof(UINT32)+sizeof(UINT16)+desc_bytes,dp,dpsz);
    SPrint(name,sizeof(name),L"Boot%04x",(UINT16)slot);
    EFI_STATUS st=uefi_call_wrapper(RT->SetVariable,5,name,&EfiGlobalVariable,7,option_size,option);
    if(!EFI_ERROR(st)){
        CHAR16 order_name[]=L"BootOrder";UINT16 order[256];UINTN order_size=sizeof(order);UINT32 order_attr=0;
        EFI_STATUS gst=uefi_call_wrapper(RT->GetVariable,5,order_name,&EfiGlobalVariable,&order_attr,&order_size,order);
        UINTN count=(gst==EFI_BUFFER_TOO_SMALL||EFI_ERROR(gst))?0:order_size/sizeof(UINT16);
        if(count>255)count=255;
        int already=0;
        for(UINTN i=0;i<count;i++)if(order[i]==(UINT16)slot)already=1;
        if(!already){order[count++]=(UINT16)slot;st=uefi_call_wrapper(RT->SetVariable,5,order_name,&EfiGlobalVariable,7,count*sizeof(UINT16),order);}
    }
    FreePool(option);FreePool(dp);
    return st;
}

static EFI_STATUS steveos_copy_volume_file(EFI_FILE_PROTOCOL *root,const CHAR16 *dst_path,const VOID *data,UINTN size){
    if(!root||!dst_path||!data)return EFI_INVALID_PARAMETER;
    EFI_FILE_PROTOCOL *file=NULL;
    EFI_STATUS st=uefi_call_wrapper(root->Open,5,root,&file,(CHAR16*)dst_path,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(EFI_ERROR(st)||!file)return st;
    EFI_FILE_INFO *info=NULL;UINTN info_sz=0;
    if(uefi_call_wrapper(file->GetInfo,4,file,&gEfiFileInfoGuid,&info_sz,NULL)==EFI_BUFFER_TOO_SMALL){
        info=AllocatePool(info_sz);
        if(info&&!EFI_ERROR(uefi_call_wrapper(file->GetInfo,4,file,&gEfiFileInfoGuid,&info_sz,info))){
            info->FileSize=0;info->PhysicalSize=0;
            uefi_call_wrapper(file->SetInfo,4,file,&gEfiFileInfoGuid,info_sz,info);
        }
        if(info)FreePool(info);
    }
    st=uefi_call_wrapper(file->SetPosition,2,file,0);
    if(!EFI_ERROR(st)){UINTN wr=size;st=uefi_call_wrapper(file->Write,3,file,&wr,(VOID*)data);if(!EFI_ERROR(st)&&wr!=size)st=EFI_DEVICE_ERROR;}
    uefi_call_wrapper(file->Close,1,file);
    return st;
}
static EFI_STATUS steveos_install_server_files(EFI_HANDLE target){
    EFI_FILE_PROTOCOL *src_root=NULL,*dst_root=NULL,*steveos_dir=NULL,*server_dir=NULL;
    EFI_STATUS st=steveos_fs_open_volume(steveos_boot_device,&src_root);
    if(EFI_ERROR(st))return st;
    st=steveos_fs_open_volume(target,&dst_root);
    if(EFI_ERROR(st)){uefi_call_wrapper(src_root->Close,1,src_root);return st;}
    st=uefi_call_wrapper(dst_root->Open,5,dst_root,&steveos_dir,L"SteveOS",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(!EFI_ERROR(st))st=uefi_call_wrapper(steveos_dir->Open,5,steveos_dir,&server_dir,L"Server",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(EFI_ERROR(st)){if(steveos_dir)uefi_call_wrapper(steveos_dir->Close,1,steveos_dir);uefi_call_wrapper(dst_root->Close,1,dst_root);uefi_call_wrapper(src_root->Close,1,src_root);return st;}
    static const CHAR16 *files[]={L"\\SteveOS\\Server\\ServerBoot.efi",L"\\SteveOS\\Server\\vmlinuz-virt",L"\\SteveOS\\Server\\server-initramfs.img",L"\\SteveOS\\Server\\server.conf"};
    static const CHAR16 *dst[]={L"ServerBoot.efi",L"vmlinuz-virt",L"server-initramfs.img",L"server.conf"};
    for(UINTN i=0;i<4&&!EFI_ERROR(st);i++){
        VOID *buf=NULL;UINTN size=0;
        st=steveos_fs_read_file(src_root,(CHAR16*)files[i],&buf,&size);
        if(!EFI_ERROR(st))st=steveos_copy_volume_file(server_dir,dst[i],buf,size);
        if(buf)FreePool(buf);
    }
    uefi_call_wrapper(server_dir->Close,1,server_dir);
    uefi_call_wrapper(steveos_dir->Close,1,steveos_dir);
    uefi_call_wrapper(dst_root->Close,1,dst_root);
    uefi_call_wrapper(src_root->Close,1,src_root);
    return st;
}
static EFI_STATUS steveos_register_named_boot_option(EFI_HANDLE target,const CHAR16 *path,const CHAR16 *desc){
    if(!target||!path||!desc)return EFI_INVALID_PARAMETER;
    EFI_DEVICE_PATH *dp=FileDevicePath(target,(CHAR16*)path);if(!dp)return EFI_OUT_OF_RESOURCES;
    UINTN dpsz=DevicePathSize(dp),slot=0;CHAR16 name[16];UINT32 attrs=0;UINTN sz=0;UINT8 probe[4];
    for(UINTN n=0;n<0x10000;n++){
        SPrint(name,sizeof(name),L"Boot%04x",(UINT16)n);sz=sizeof(probe);
        EFI_STATUS gst=uefi_call_wrapper(RT->GetVariable,5,name,&EfiGlobalVariable,&attrs,&sz,probe);
        if(gst==EFI_NOT_FOUND){slot=n;break;}
        if(n==0xFFFF){FreePool(dp);return EFI_OUT_OF_RESOURCES;}
    }
    UINTN desc_bytes=StrSize((CHAR16*)desc);
    UINTN option_size=sizeof(UINT32)+sizeof(UINT16)+desc_bytes+dpsz;
    UINT8 *option=AllocatePool(option_size);if(!option){FreePool(dp);return EFI_OUT_OF_RESOURCES;}
    ZeroMem(option,option_size);*(UINT32*)option=LOAD_OPTION_ACTIVE;*(UINT16*)(option+sizeof(UINT32))=(UINT16)dpsz;
    CopyMem(option+sizeof(UINT32)+sizeof(UINT16),desc,desc_bytes);
    CopyMem(option+sizeof(UINT32)+sizeof(UINT16)+desc_bytes,dp,dpsz);
    SPrint(name,sizeof(name),L"Boot%04x",(UINT16)slot);
    EFI_STATUS st=uefi_call_wrapper(RT->SetVariable,5,name,&EfiGlobalVariable,7,option_size,option);
    if(!EFI_ERROR(st)){
        CHAR16 order_name[]=L"BootOrder";UINT16 order[256];UINTN order_size=sizeof(order);UINT32 order_attr=0;
        EFI_STATUS gst=uefi_call_wrapper(RT->GetVariable,5,order_name,&EfiGlobalVariable,&order_attr,&order_size,order);
        UINTN count=(gst==EFI_BUFFER_TOO_SMALL||EFI_ERROR(gst))?0:order_size/sizeof(UINT16);if(count>255)count=255;
        int already=0;for(UINTN i=0;i<count;i++)if(order[i]==(UINT16)slot)already=1;
        if(!already){order[count++]=(UINT16)slot;st=uefi_call_wrapper(RT->SetVariable,5,order_name,&EfiGlobalVariable,7,count*sizeof(UINT16),order);}
    }
    FreePool(option);FreePool(dp);return st;
}
static EFI_STATUS steveos_install_server(UINT64 target_index){
    STEVEOS_TARGET_INTERNAL tmp[32];UINTN count=0;
    if(EFI_ERROR(steveos_target_handles(tmp,32,&count))||target_index>=count)return EFI_NOT_FOUND;
    EFI_STATUS st=steveos_install_server_files(tmp[target_index].handle);
    if(!EFI_ERROR(st))st=steveos_register_named_boot_option(tmp[target_index].handle,L"\\SteveOS\\Server\\ServerBoot.efi",L"SteveOS Server");
    return st;
}
EFI_STATUS steveos_launch_server(void){
    return steveos_launch_app(L"\\SteveOS\\Server\\ServerBoot.efi");
}


static const CHAR16*steveos_basename(const CHAR16*path){
    const CHAR16*last=path;
    if(!path)return last;
    for(const CHAR16*p=path;*p;p++)if(*p==L'\\'||*p==L'/')last=p+1;
    return last;
}

static BOOLEAN steveos_is_efi_name(const CHAR16 *name){
    if(!name)return FALSE;
    UINTN n=0;while(name[n])n++;
    return n>=4&&name[n-4]==L'.'&&
           (name[n-3]==L'e'||name[n-3]==L'E')&&
           (name[n-2]==L'f'||name[n-2]==L'F')&&
           (name[n-1]==L'i'||name[n-1]==L'I');
}
static BOOLEAN steveos_is_exe_name(const CHAR16 *name){
    if(!name)return FALSE;
    UINTN n=0;while(name[n])n++;
    return n>=4&&name[n-4]==L'.'&&
           (name[n-3]==L'e'||name[n-3]==L'E')&&
           (name[n-2]==L'x'||name[n-2]==L'X')&&
           (name[n-1]==L'e'||name[n-1]==L'E');
}
static BOOLEAN steveos_is_windows_safe_name(const CHAR16 *name){
    if(!name||!name[0])return FALSE;
    for(UINTN i=0;name[i];i++){
        CHAR16 c=name[i];
        if((c>=L'A'&&c<=L'Z')||(c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||
           c==L'.'||c==L'_'||c==L'-'||c==L' ')continue;
        return FALSE;
    }
    return TRUE;
}
EFI_STATUS steveos_install_app(const CHAR16 *source_path){
    if(!source_path||!steveos_boot_device)return EFI_INVALID_PARAMETER;
    const CHAR16*name=steveos_basename(source_path);
    if(!name||!name[0]||(!steveos_is_efi_name(name)&&!steveos_is_exe_name(name)))return EFI_INVALID_PARAMETER;
    EFI_FILE_PROTOCOL *root=NULL,*apps=NULL,*src=NULL,*dst=NULL;EFI_STATUS st;
    st=steveos_fs_open_volume(steveos_boot_device,&root);if(EFI_ERROR(st))return st;
    st=uefi_call_wrapper(root->Open,5,root,&src,source_path,EFI_FILE_MODE_READ,0);
    if(EFI_ERROR(st)){uefi_call_wrapper(root->Close,1,root);return st;}
    EFI_FILE_INFO *info=NULL;UINTN info_sz=0;
    st=uefi_call_wrapper(src->GetInfo,4,src,&gEfiFileInfoGuid,&info_sz,NULL);
    if(st==EFI_BUFFER_TOO_SMALL){info=AllocatePool(info_sz);if(info)st=uefi_call_wrapper(src->GetInfo,4,src,&gEfiFileInfoGuid,&info_sz,info);}
    if(EFI_ERROR(st)||!info||info->FileSize>1024ULL*1024ULL){if(info)FreePool(info);uefi_call_wrapper(src->Close,1,src);uefi_call_wrapper(root->Close,1,root);return EFI_BAD_BUFFER_SIZE;}
    UINTN size=(UINTN)info->FileSize;FreePool(info);
    VOID*buf=AllocatePool(size?size:1);if(!buf){uefi_call_wrapper(src->Close,1,src);uefi_call_wrapper(root->Close,1,root);return EFI_OUT_OF_RESOURCES;}
    UINTN read=size;st=uefi_call_wrapper(src->Read,3,src,&read,buf);uefi_call_wrapper(src->Close,1,src);
    if(EFI_ERROR(st)||read!=size){FreePool(buf);uefi_call_wrapper(root->Close,1,root);return EFI_DEVICE_ERROR;}
    EFI_FILE_PROTOCOL *steveos_dir=NULL;
    st=uefi_call_wrapper(root->Open,5,root,&steveos_dir,L"SteveOS",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(!EFI_ERROR(st))st=uefi_call_wrapper(steveos_dir->Open,5,steveos_dir,&apps,L"Apps",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(!EFI_ERROR(st))st=uefi_call_wrapper(apps->Open,5,apps,&dst,(CHAR16*)name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(!EFI_ERROR(st)){UINTN wr=size;uefi_call_wrapper(dst->SetPosition,2,dst,0);st=uefi_call_wrapper(dst->Write,3,dst,&wr,buf);if(!EFI_ERROR(st)&&wr!=size)st=EFI_DEVICE_ERROR;}
    if(dst)uefi_call_wrapper(dst->Close,1,dst);if(apps)uefi_call_wrapper(apps->Close,1,apps);if(steveos_dir)uefi_call_wrapper(steveos_dir->Close,1,steveos_dir);uefi_call_wrapper(root->Close,1,root);FreePool(buf);
    return st;
}

EFI_STATUS steveos_download_app(const CHAR16 *url,const CHAR16 *filename){
    if(!url||!filename||!steveos_boot_device||
       (!steveos_is_efi_name(filename)&&!steveos_is_exe_name(filename)))return EFI_INVALID_PARAMETER;
    const UINTN max_download=16ULL*1024ULL*1024ULL;
    CHAR8 *data=AllocatePool(max_download);
    if(!data)return EFI_OUT_OF_RESOURCES;
    UINTN len=0;UINT32 status=0;
    EFI_STATUS st=steveos_http_get(url,data,max_download-1,&len,&status);
    if(EFI_ERROR(st)||status<200||status>=300||len<4){FreePool(data);return EFI_ABORTED;}
    EFI_FILE_PROTOCOL *root=NULL,*apps=NULL,*file=NULL;
    st=steveos_fs_open_volume(steveos_boot_device,&root);
    EFI_FILE_PROTOCOL *steveos_dir=NULL;
    if(!EFI_ERROR(st))st=uefi_call_wrapper(root->Open,5,root,&steveos_dir,L"SteveOS",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(!EFI_ERROR(st))st=uefi_call_wrapper(steveos_dir->Open,5,steveos_dir,&apps,L"Apps",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(!EFI_ERROR(st))st=uefi_call_wrapper(apps->Open,5,apps,&file,(CHAR16*)filename,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(!EFI_ERROR(st)){
        UINTN info_size=0;EFI_FILE_INFO *info=NULL;
        if(uefi_call_wrapper(file->GetInfo,4,file,&gEfiFileInfoGuid,&info_size,NULL)==EFI_BUFFER_TOO_SMALL){
            info=AllocatePool(info_size);
            if(info&&!EFI_ERROR(uefi_call_wrapper(file->GetInfo,4,file,&gEfiFileInfoGuid,&info_size,info))){
                info->FileSize=0;info->PhysicalSize=0;
                uefi_call_wrapper(file->SetInfo,4,file,&gEfiFileInfoGuid,info_size,info);
            }
            if(info)FreePool(info);
        }
        uefi_call_wrapper(file->SetPosition,2,file,0);
        UINTN wr=len;st=uefi_call_wrapper(file->Write,3,file,&wr,data);
        if(!EFI_ERROR(st)&&wr!=len)st=EFI_DEVICE_ERROR;
        uefi_call_wrapper(file->Close,1,file);
    }
    if(apps)uefi_call_wrapper(apps->Close,1,apps);
    if(steveos_dir)uefi_call_wrapper(steveos_dir->Close,1,steveos_dir);
    if(root)uefi_call_wrapper(root->Close,1,root);
    FreePool(data);
    return st;
}

static EFI_STATUS steveos_launch_app(const CHAR16 *path){
    if(!path||!steveos_boot_device||!steveos_image_handle)return EFI_INVALID_PARAMETER;
    EFI_DEVICE_PATH *dp=FileDevicePath(steveos_boot_device,(CHAR16*)path);
    if(!dp)return EFI_OUT_OF_RESOURCES;
    EFI_HANDLE child=NULL;
    EFI_STATUS st=uefi_call_wrapper(BS->LoadImage,6,FALSE,steveos_image_handle,dp,NULL,0,&child);
    FreePool(dp);
    if(EFI_ERROR(st)||!child)return st;
    st=uefi_call_wrapper(BS->StartImage,3,child,NULL,NULL);
    return st;
}

static BOOLEAN steveos_is_installed_windows_path(const CHAR16 *path){
    const CHAR16 *prefix=L"\\SteveOS\\Apps\\";
    if(!path)return FALSE;
    for(UINTN i=0;prefix[i];i++)if(path[i]!=prefix[i])return FALSE;
    return TRUE;
}

EFI_STATUS steveos_install_windows_app(const CHAR16 *source_path){
    if(!source_path||!steveos_boot_device||!steveos_is_exe_name(steveos_basename(source_path))||
       !steveos_is_windows_safe_name(steveos_basename(source_path)))return EFI_INVALID_PARAMETER;

    const CHAR16 *name=steveos_basename(source_path);
    if(steveos_is_installed_windows_path(source_path))return EFI_SUCCESS;

    EFI_FILE_PROTOCOL *root=NULL,*src=NULL,*dir=NULL,*apps=NULL,*dst=NULL;
    EFI_STATUS st=steveos_fs_open_volume(steveos_boot_device,&root);
    if(EFI_ERROR(st))return st;

    st=uefi_call_wrapper(root->Open,5,root,&src,(CHAR16*)source_path,EFI_FILE_MODE_READ,0);
    if(EFI_ERROR(st)){uefi_call_wrapper(root->Close,1,root);return st;}

    st=uefi_call_wrapper(root->Open,5,root,&dir,L"SteveOS",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(!EFI_ERROR(st))st=uefi_call_wrapper(dir->Open,5,dir,&apps,L"Apps",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(!EFI_ERROR(st))st=uefi_call_wrapper(apps->Open,5,apps,&dst,(CHAR16*)name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(EFI_ERROR(st)){
        if(dst)uefi_call_wrapper(dst->Close,1,dst);
        if(apps)uefi_call_wrapper(apps->Close,1,apps);
        if(dir)uefi_call_wrapper(dir->Close,1,dir);
        uefi_call_wrapper(src->Close,1,src);uefi_call_wrapper(root->Close,1,root);
        return st;
    }

    EFI_FILE_INFO info;
    ZeroMem(&info,sizeof(info));info.Size=sizeof(info);info.FileSize=0;info.PhysicalSize=0;
    st=uefi_call_wrapper(dst->SetInfo,4,dst,&gEfiFileInfoGuid,info.Size,&info);
    VOID *buf=NULL;
    if(!EFI_ERROR(st))buf=AllocatePool(128*1024);
    if(!buf&&!EFI_ERROR(st))st=EFI_OUT_OF_RESOURCES;

    while(!EFI_ERROR(st)){
        UINTN got=128*1024;
        st=uefi_call_wrapper(src->Read,3,src,&got,buf);
        if(EFI_ERROR(st)||got==0)break;
        UINTN wr=got;
        st=uefi_call_wrapper(dst->Write,3,dst,&wr,buf);
        if(EFI_ERROR(st)||wr!=got){st=EFI_DEVICE_ERROR;break;}
    }

    if(buf)FreePool(buf);
    uefi_call_wrapper(dst->Close,1,dst);
    uefi_call_wrapper(apps->Close,1,apps);
    uefi_call_wrapper(dir->Close,1,dir);
    uefi_call_wrapper(src->Close,1,src);
    uefi_call_wrapper(root->Close,1,root);
    return st;
}

EFI_STATUS steveos_run_windows_app(const CHAR16 *source_path){
    if(!source_path||!steveos_boot_device||!steveos_is_exe_name(steveos_basename(source_path))||
       !steveos_is_windows_safe_name(steveos_basename(source_path)))return EFI_INVALID_PARAMETER;

    EFI_FILE_PROTOCOL *root=NULL,*server=NULL,*probe=NULL;
    EFI_STATUS st=steveos_fs_open_volume(steveos_boot_device,&root);
    if(EFI_ERROR(st))return st;
    st=uefi_call_wrapper(root->Open,5,root,&server,L"SteveOS\\Server",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,EFI_FILE_DIRECTORY);
    if(EFI_ERROR(st)){uefi_call_wrapper(root->Close,1,root);return EFI_NOT_FOUND;}
    st=uefi_call_wrapper(server->Open,5,server,&probe,L"ServerBoot.efi",EFI_FILE_MODE_READ,0);
    if(EFI_ERROR(st)){uefi_call_wrapper(server->Close,1,server);uefi_call_wrapper(root->Close,1,root);return EFI_NOT_FOUND;}
    uefi_call_wrapper(probe->Close,1,probe);

    st=steveos_install_windows_app(source_path);
    if(EFI_ERROR(st)){uefi_call_wrapper(server->Close,1,server);uefi_call_wrapper(root->Close,1,root);return st;}

    const CHAR16 *name=steveos_basename(source_path);
    CHAR8 cfg[256];UINTN p=0;
    const char *prefix="EXE_AUTORUN=/efi/SteveOS/Apps/";
    for(UINTN i=0;prefix[i]&&p+1<sizeof(cfg);i++)cfg[p++]=(CHAR8)prefix[i];
    for(UINTN i=0;name[i]&&p+1<sizeof(cfg)-2;i++)cfg[p++]=(CHAR8)(name[i]<128?name[i]:'_');
    cfg[p++]='\n';cfg[p]=0;
    st=steveos_write_boot_text(L"\\SteveOS\\Server\\run-exe.conf",cfg,p);
    uefi_call_wrapper(server->Close,1,server);
    uefi_call_wrapper(root->Close,1,root);
    if(EFI_ERROR(st))return st;
    return steveos_launch_server();
}

EFI_STATUS steveos_network_info(STEVEOS_NETWORK_INFO *out){
    if(!out)return EFI_INVALID_PARAMETER;
    ZeroMem(out,sizeof(*out));
    EFI_SIMPLE_NETWORK_PROTOCOL *snp=NULL;
    EFI_STATUS st=uefi_call_wrapper(BS->LocateProtocol,3,&gEfiSimpleNetworkProtocolGuid,NULL,(VOID**)&snp);
    if(EFI_ERROR(st)||!snp||!snp->Mode)return st;
    out->state=(UINT32)snp->Mode->State;
    out->media_present=snp->Mode->MediaPresent?1u:0u;
    out->mac_size=(UINT32)snp->Mode->HwAddressSize;
    if(out->mac_size>32)out->mac_size=32;
    for(UINT32 i=0;i<out->mac_size;i++)out->mac[i]=snp->Mode->CurrentAddress.Addr[i];
    return EFI_SUCCESS;
}

EFI_STATUS steveos_write_boot_text(const CHAR16 *path,const void *data,UINTN size){
    if(!path||!data)return EFI_INVALID_PARAMETER;
    if(!steveos_boot_device)return EFI_NOT_FOUND;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_FILE_PROTOCOL *root=NULL;EFI_FILE_PROTOCOL *file=NULL;
    EFI_STATUS st=uefi_call_wrapper(BS->HandleProtocol,3,steveos_boot_device,&gEfiSimpleFileSystemProtocolGuid,(VOID**)&fs);
    if(EFI_ERROR(st)||!fs)return st;
    st=uefi_call_wrapper(fs->OpenVolume,2,fs,&root);if(EFI_ERROR(st)||!root)return st;
    st=uefi_call_wrapper(root->Open,5,root,&file,path,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(!EFI_ERROR(st)&&file){
        EFI_FILE_INFO info;
        ZeroMem(&info,sizeof(info));
        info.Size=sizeof(info);
        info.FileSize=0;
        info.PhysicalSize=0;
        uefi_call_wrapper(file->SetInfo,4,file,&gEfiFileInfoGuid,info.Size,&info);
        UINT64 pos=0;
        uefi_call_wrapper(file->SetPosition,2,file,pos);
        st=uefi_call_wrapper(file->Write,3,file,&size,(VOID*)data);
        uefi_call_wrapper(file->Close,1,file);
    }
    uefi_call_wrapper(root->Close,1,root);
    return st;
}

EFI_STATUS steveos_kernel_prepare(void) {
    const UINTN raw_size = (UINTN)(_binary_build_native_kernel_raw_end -
                                    _binary_build_native_kernel_raw_start);
    return raw_size ? EFI_SUCCESS : EFI_NOT_FOUND;
}

EFI_STATUS steveos_kernel_boot(EFI_HANDLE image_handle,
                               EFI_GRAPHICS_OUTPUT_PROTOCOL *gop) {
    if (!gop || !gop->Mode || !gop->Mode->Info)
        return EFI_INVALID_PARAMETER;
    if (EFI_ERROR(steveos_kernel_prepare()))
        return EFI_NOT_FOUND;

    const UINTN kernel_size = (UINTN)(_binary_build_native_kernel_raw_end -
                                      _binary_build_native_kernel_raw_start);
    const UINTN kernel_pages = (kernel_size + 4095) / 4096;
    const UINTN stack_pages = 16;
    UINT64 fb_bytes64 = (UINT64)gop->Mode->Info->PixelsPerScanLine *
                        (UINT64)gop->Mode->Info->VerticalResolution * 4ULL;
    const UINTN backbuffer_pages = (UINTN)((fb_bytes64 + 4095ULL) / 4096ULL);

    EFI_PHYSICAL_ADDRESS kernel_addr = allocate_kernel_pages(kernel_pages);
    EFI_PHYSICAL_ADDRESS stack_addr = allocate_pages(stack_pages);
    EFI_PHYSICAL_ADDRESS backbuffer_addr = allocate_pages(backbuffer_pages);
    STEVEOS_BOOT_INFO *boot = AllocatePool(sizeof(STEVEOS_BOOT_INFO));
    if (!kernel_addr || !stack_addr || !backbuffer_addr || !boot) {
        if (kernel_addr) uefi_call_wrapper(BS->FreePages, 2, kernel_addr, kernel_pages);
        if (stack_addr) uefi_call_wrapper(BS->FreePages, 2, stack_addr, stack_pages);
        if (backbuffer_addr) uefi_call_wrapper(BS->FreePages, 2, backbuffer_addr, backbuffer_pages);
        if (boot) FreePool(boot);
        return EFI_OUT_OF_RESOURCES;
    }

    CopyMem((VOID *)(UINTN)kernel_addr,
            _binary_build_native_kernel_raw_start, kernel_size);
    ZeroMem((VOID *)(UINTN)backbuffer_addr, (UINTN)fb_bytes64);
    ZeroMem(boot, sizeof(*boot));
    boot->magic = STEVEOS_BOOT_MAGIC;
    boot->framebuffer_base = gop->Mode->FrameBufferBase;
    boot->framebuffer_size = gop->Mode->FrameBufferSize;
    boot->width = gop->Mode->Info->HorizontalResolution;
    boot->height = gop->Mode->Info->VerticalResolution;
    boot->pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
    boot->pixel_format = gop->Mode->Info->PixelFormat;
    boot->acpi_rsdp = find_acpi_rsdp();
    boot->kernel_base = kernel_addr;
    boot->kernel_size = kernel_size;
    boot->kernel_stack_top = stack_addr + stack_pages * 4096ULL - 16;
    boot->uefi_get_variable = (UINT64)(UINTN)RT->GetVariable;
    boot->uefi_set_variable = (UINT64)(UINTN)RT->SetVariable;
    boot->uefi_get_time = (UINT64)(UINTN)RT->GetTime;
    boot->uefi_http_get = (UINT64)(UINTN)steveos_http_get;
    boot->uefi_write_text = (UINT64)(UINTN)steveos_write_boot_text;
    boot->uefi_list_install_targets = (UINT64)(UINTN)steveos_list_install_targets;
    boot->uefi_install_self = (UINT64)(UINTN)steveos_install_self;
    boot->uefi_install_server = (UINT64)(UINTN)steveos_install_server;
    boot->uefi_launch_server = (UINT64)(UINTN)steveos_launch_server;
    boot->uefi_install_app = (UINT64)(UINTN)steveos_install_app;
    boot->uefi_download_app = (UINT64)(UINTN)steveos_download_app;
    boot->uefi_launch_app = (UINT64)(UINTN)steveos_launch_app;
    boot->uefi_install_windows_app = (UINT64)(UINTN)steveos_install_windows_app;
    boot->uefi_run_windows_app = (UINT64)(UINTN)steveos_run_windows_app;
    boot->uefi_network_info = (UINT64)(UINTN)steveos_network_info;
    boot->backbuffer_base = backbuffer_addr;
    boot->backbuffer_size = fb_bytes64;
    (void)snapshot_boot_files(image_handle, boot);

    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_size = 0, map_key = 0, descriptor_size = 0;
    UINT32 descriptor_version = 0;
    EFI_STATUS st = get_memory_map(&map, &map_size, &map_key,
                                   &descriptor_size, &descriptor_version);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(BS->FreePages, 2, kernel_addr, kernel_pages);
        uefi_call_wrapper(BS->FreePages, 2, stack_addr, stack_pages);
        uefi_call_wrapper(BS->FreePages, 2, backbuffer_addr, backbuffer_pages);
        FreePool(boot);
        return st;
    }
    boot->memory_map = (UINT64)(UINTN)map;
    boot->memory_map_size = map_size;
    boot->memory_descriptor_size = descriptor_size;
    boot->memory_descriptor_version = descriptor_version;

    /* Keep UEFI Boot Services alive. The native desktop uses the firmware's
     * HTTP protocol as its network bridge, so ExitBootServices() would make
     * the browser impossible. The native framebuffer and input drivers still
     * own their hardware paths, while firmware services remain available for
     * explicit browser requests. */
    (void)map_key;

    STEVEOS_NATIVE_ENTRY entry = (STEVEOS_NATIVE_ENTRY)(UINTN)kernel_addr;
    entry(boot, (VOID *)(UINTN)boot->kernel_stack_top);
    for (;;) __asm__ __volatile__("cli; hlt");
}

EFI_STATUS steveos_kernel_handoff(void) {
    return EFI_UNSUPPORTED;
}

void steveos_kernel_panic(const CHAR8 *message) {
    (void)message;
    for (;;) __asm__ __volatile__("cli; hlt");
}
