#ifndef __ARCH_X86_64_BOOT_UEFI_H__
#define __ARCH_X86_64_BOOT_UEFI_H__

#include <stdint.h>

// EFIAPI calling convention and parameter attributes
#define EFIAPI __attribute__((ms_abi))
#define IN
#define OUT
typedef int64_t INTN;

// EFI Basic Types
typedef void* EFI_HANDLE;
typedef uint64_t EFI_STATUS;
typedef uint64_t UINTN;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uint16_t UINT16;
typedef uint8_t UINT8;
typedef uint16_t CHAR16;
typedef uint8_t BOOLEAN;

// EFI Status Codes
#define EFI_SUCCESS                0
#define EFI_LOAD_ERROR             1
#define EFI_INVALID_PARAMETER      2
#define EFI_UNSUPPORTED            3
#define EFI_BAD_BUFFER_SIZE        4
#define EFI_BUFFER_TOO_SMALL       5
#define EFI_NOT_READY              6
#define EFI_DEVICE_ERROR           7
#define EFI_WRITE_PROTECTED        8
#define EFI_OUT_OF_RESOURCES       9
#define EFI_VOLUME_CORRUPTED       10
#define EFI_VOLUME_FULL            11
#define EFI_NO_MEDIA               12
#define EFI_MEDIA_CHANGED          13
#define EFI_NOT_FOUND              14
#define EFI_ACCESS_DENIED          15
#define EFI_NO_RESPONSE            16
#define EFI_NO_MAPPING             17
#define EFI_TIMEOUT                18
#define EFI_NOT_STARTED            19
#define EFI_ALREADY_STARTED        20
#define EFI_ABORTED                21
#define EFI_ICMP_ERROR             22
#define EFI_TFTP_ERROR             23
#define EFI_PROTOCOL_ERROR         24

#define EFI_ERROR(a)               (((INTN) a) < 0)

// EFI Memory Types
typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

// EFI Memory Descriptor
typedef struct {
    UINT32                Type;
    EFI_PHYSICAL_ADDRESS  PhysicalStart;
    EFI_VIRTUAL_ADDRESS   VirtualStart;
    UINT64                NumberOfPages;
    UINT64                Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;

// EFI GUID
typedef struct {
    UINT32  Data1;
    UINT16  Data2;
    UINT16  Data3;
    UINT8   Data4[8];
} EFI_GUID;

// Graphics Output Protocol
typedef struct {
    UINT32            RedMask;
    UINT32            GreenMask;
    UINT32            BlueMask;
    UINT32            ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32                     Version;
    UINT32                     HorizontalResolution;
    UINT32                     VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT  PixelFormat;
    EFI_PIXEL_BITMASK          PixelInformation;
    UINT32                     PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                                 MaxMode;
    UINT32                                 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION   *Info;
    UINTN                                  SizeOfInfo;
    EFI_PHYSICAL_ADDRESS                   FrameBufferBase;
    UINTN                                  FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_QUERY_MODE) (
    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
    IN  UINT32                                ModeNumber,
    OUT UINTN                                 *SizeOfInfo,
    OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
);

typedef EFI_STATUS (EFIAPI *EFI_GRAPHICS_OUTPUT_SET_MODE) (
    IN  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN  UINT32                       ModeNumber
);

struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_QUERY_MODE  QueryMode;
    EFI_GRAPHICS_OUTPUT_SET_MODE    SetMode;
    void                            *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

// EFI System Table
typedef struct _EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;
typedef struct _EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct _EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;

struct _EFI_BOOT_SERVICES {
    char _buf[1024]; // Simplified - actual structure is much larger
};

struct _EFI_RUNTIME_SERVICES {
    char _buf[1024]; // Simplified - actual structure is much larger
};

struct _EFI_SYSTEM_TABLE {
    UINT64                    Signature;
    UINT32                    Revision;
    UINT32                    HeaderSize;
    UINT32                    CRC32;
    UINT32                    Reserved;
    
    CHAR16                    *FirmwareVendor;
    UINT32                    FirmwareRevision;
    
    EFI_HANDLE                ConsoleInHandle;
    EFI_HANDLE                ConsoleOutHandle;
    EFI_HANDLE                StandardErrorHandle;
    
    EFI_RUNTIME_SERVICES      *RuntimeServices;
    EFI_BOOT_SERVICES         *BootServices;
    
    UINTN                     NumberOfTableEntries;
    void                      *ConfigurationTable;
};

// UEFI Boot Information Structure
struct uefi_boot_info {
    UINT32 magic;                    // Magic number to identify UEFI boot
    UINT64 memory_map_addr;          // Physical address of memory map
    UINT32 memory_map_size;          // Size of memory map
    UINT32 memory_map_descriptor_size;
    UINT64 framebuffer_addr;         // Framebuffer physical address
    UINT32 framebuffer_width;        // Framebuffer width
    UINT32 framebuffer_height;       // Framebuffer height
    UINT32 framebuffer_pitch;        // Framebuffer pitch
    UINT8  framebuffer_bpp;          // Bits per pixel
    UINT64 rsdp_addr;                // ACPI RSDP address
} __attribute__((packed));

#define UEFI_BOOT_MAGIC 0xEF1B007

#endif /* __ARCH_X86_64_BOOT_UEFI_H__ */ 