#include <efi.h>
#include <efilib.h>

// Multiboot memory map entry structure
struct multiboot_mmap_entry {
    UINT32 size;
    UINT64 addr;
    UINT64 len;
    UINT32 type;
} __attribute__((packed));

// Multiboot memory map structure  
struct multiboot_mmap {
    UINT32 mmap_length;
    UINT32 mmap_addr;
} __attribute__((packed));

// Extended multiboot info structure with memory map
struct rendezvos_multiboot_info_extended {
    UINT32 flags;
    struct {
        UINT32 mem_lower;
        UINT32 mem_upper;
    } mem;
    UINT32 boot_device;
    UINT32 cmdline;
    UINT32 mods_count;
    UINT32 mods_addr;
    UINT32 syms[4];
    struct multiboot_mmap mmap;
    UINT32 reserved[12]; // Additional fields
} __attribute__((packed));

// Memory map constants
#define MULTIBOOT_MEMORY_AVAILABLE        1
#define MULTIBOOT_MEMORY_RESERVED         2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS              4
#define MULTIBOOT_MEMORY_BADRAM           5

// Additional multiboot flags
#define MULTIBOOT_INFO_FLAG_MMAP          (1 << 6)

// Function to get UEFI memory map and convert to multiboot format
static EFI_STATUS get_uefi_memory_map(struct rendezvos_multiboot_info_extended *mb_info);

// Minimal structures for RendezvOS
struct rendezvos_setup_info {
    UINT32 multiboot_magic;
    UINT32 multiboot_info_struct_ptr;
    UINT32 phy_addr_width;
    UINT32 vir_addr_width;
    UINT64 log_buffer_addr;
    UINT64 rsdp_addr;
    UINT64 ap_boot_stack_ptr;
    UINT64 cpu_id;
} __attribute__((packed));

struct rendezvos_multiboot_mem {
    UINT32 mem_lower;
    UINT32 mem_upper;
} __attribute__((packed));

struct rendezvos_multiboot_framebuffer {
    UINT64 framebuffer_addr;
    UINT32 framebuffer_pitch;
    UINT32 framebuffer_width;
    UINT32 framebuffer_height;
    UINT8 framebuffer_bpp;
    UINT8 framebuffer_type;
    UINT8 framebuffer_red_field_position;
    UINT8 framebuffer_red_mask_size;
    UINT8 framebuffer_green_field_position;
    UINT8 framebuffer_green_mask_size;
    UINT8 framebuffer_blue_field_position;
    UINT8 framebuffer_blue_mask_size;
} __attribute__((packed));

struct rendezvos_multiboot_info {
    UINT32 flags;
    struct rendezvos_multiboot_mem mem;
    UINT32 reserved[20]; // Placeholder for other multiboot fields
} __attribute__((packed));

// Multiboot flags
#define MULTIBOOT_INFO_FLAG_MEM               (1 << 0)
#define MULTIBOOT_INFO_FLAG_FRAMEBUFFER       (1 << 12)
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB        1

// Global variables
static struct rendezvos_setup_info g_setup_info;
static struct rendezvos_multiboot_info_extended g_multiboot_info;
static EFI_HANDLE g_image_handle;  // Save ImageHandle for later use

// External symbols for embedded kernel (if present)
#ifdef KERNEL_EMBEDDED
extern char kernel_start[];
extern char kernel_end[];
extern char kernel_size[];
#endif

/**
 * Load kernel from embedded data
 */
static EFI_STATUS load_embedded_kernel(void **kernel_data, UINTN *kernel_size) {
#ifdef KERNEL_EMBEDDED
    *kernel_data = kernel_start;
    *kernel_size = (UINTN)kernel_size;
    Print(L"Found embedded kernel: %d bytes\r\n", *kernel_size);
    return EFI_SUCCESS;
#else
    return EFI_NOT_FOUND;
#endif
}

/**
 * Load kernel from file system
 */
static EFI_STATUS load_kernel_from_file(EFI_HANDLE ImageHandle, void **kernel_data, UINTN *kernel_size) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_PROTOCOL *Root;
    EFI_FILE_PROTOCOL *KernelFile;
    EFI_FILE_INFO *FileInfo;
    UINTN FileInfoSize;
    
    // Get loaded image protocol
    Status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &LoadedImageProtocol, (VOID**)&LoadedImage);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to get loaded image protocol: %r\r\n", Status);
        return Status;
    }
    
    // Get file system protocol
    Status = uefi_call_wrapper(BS->HandleProtocol, 3, LoadedImage->DeviceHandle, &FileSystemProtocol, (VOID**)&FileSystem);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to get file system protocol: %r\r\n", Status);
        return Status;
    }
    
    // Open root directory
    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &Root);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to open root directory: %r\r\n", Status);
        return Status;
    }
    
    // Open kernel.bin file
    Status = uefi_call_wrapper(Root->Open, 5, Root, &KernelFile, L"kernel.bin", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to open kernel.bin: %r\r\n", Status);
        uefi_call_wrapper(Root->Close, 1, Root);
        return Status;
    }
    
    Print(L"Found kernel.bin file\r\n");
    
    // Get file size
    FileInfoSize = sizeof(EFI_FILE_INFO) + 256;
    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, FileInfoSize, (VOID**)&FileInfo);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to allocate file info buffer: %r\r\n", Status);
        uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
        uefi_call_wrapper(Root->Close, 1, Root);
        return Status;
    }
    
    Status = uefi_call_wrapper(KernelFile->GetInfo, 4, KernelFile, &GenericFileInfo, &FileInfoSize, FileInfo);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to get file info: %r\r\n", Status);
        uefi_call_wrapper(BS->FreePool, 1, FileInfo);
        uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
        uefi_call_wrapper(Root->Close, 1, Root);
        return Status;
    }
    
    *kernel_size = (UINTN)FileInfo->FileSize;
    Print(L"Kernel file size: %d bytes\r\n", *kernel_size);
    
    // Allocate memory for kernel
    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, *kernel_size, kernel_data);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to allocate kernel buffer: %r\r\n", Status);
        uefi_call_wrapper(BS->FreePool, 1, FileInfo);
        uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
        uefi_call_wrapper(Root->Close, 1, Root);
        return Status;
    }
    
    // Read kernel data
    Status = uefi_call_wrapper(KernelFile->Read, 3, KernelFile, kernel_size, *kernel_data);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to read kernel file: %r\r\n", Status);
        uefi_call_wrapper(BS->FreePool, 1, *kernel_data);
        uefi_call_wrapper(BS->FreePool, 1, FileInfo);
        uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
        uefi_call_wrapper(Root->Close, 1, Root);
        return Status;
    }
    
    Print(L"Successfully loaded kernel from file\r\n");
    
    // Clean up
    uefi_call_wrapper(BS->FreePool, 1, FileInfo);
    uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
    uefi_call_wrapper(Root->Close, 1, Root);
    
    return EFI_SUCCESS;
}

/**
 * Copy kernel to target address and jump to it
 * Direct 64-bit approach: Call cmain directly, bypassing 32-bit multiboot code
 */
static void jump_to_kernel(void *kernel_data, UINTN kernel_size) {
    void *kernel_target = (void*)0x100000;  // 1MB
    EFI_STATUS Status;
    
    Print(L"Copying kernel to 0x100000...\r\n");
    
    // Copy kernel to target address
    CopyMem(kernel_target, kernel_data, kernel_size);
    
    Print(L"Kernel copied, preparing to jump...\r\n");
    Print(L"Magic: 0x%x\r\n", g_setup_info.multiboot_magic);
    
    // Allocate multiboot info in low memory (below 1MB)
    struct rendezvos_multiboot_info_extended *mb_info_32;
    EFI_PHYSICAL_ADDRESS mb_info_phys = 0x7E000;  // 504KB - safe area below 1MB
    
    // Allocate multiboot info in low memory
    Status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress, EfiLoaderData, 1, &mb_info_phys);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to allocate multiboot info in low memory: %r\r\n", Status);
        // Use a fixed safe address
        mb_info_32 = (struct rendezvos_multiboot_info_extended*)0x7E000;
    } else {
        mb_info_32 = (struct rendezvos_multiboot_info_extended*)mb_info_phys;
    }
    
    // Copy multiboot info to low memory
    CopyMem(mb_info_32, &g_multiboot_info, sizeof(g_multiboot_info));
    
    UINT32 magic = g_setup_info.multiboot_magic;
    UINT32 info = (UINT32)(UINTN)mb_info_32;
    
    Print(L"Multiboot info: 0x%x\r\n", info);
    Print(L"Magic: 0x%x (Multiboot compliant)\r\n", magic);
    
    // Find cmain function in the kernel
    // Based on boot.S analysis, cmain is called from run_main after 64-bit setup
    // We need to locate it in the kernel binary
    
    Print(L"Looking for cmain function...\r\n");
    
    // Setup the setup_info structure at the expected location
    // From objdump: setup_info is at ffff8000001010d0, offset 0x10d0 from kernel start
    struct rendezvos_setup_info *kernel_setup_info = (struct rendezvos_setup_info*)((char*)kernel_target + 0x10d0);
    
    // Copy our setup info to the kernel's setup_info location
    kernel_setup_info->multiboot_magic = magic;
    kernel_setup_info->multiboot_info_struct_ptr = info;
    kernel_setup_info->phy_addr_width = g_setup_info.phy_addr_width;
    kernel_setup_info->vir_addr_width = g_setup_info.vir_addr_width;
    kernel_setup_info->log_buffer_addr = g_setup_info.log_buffer_addr;
    kernel_setup_info->rsdp_addr = g_setup_info.rsdp_addr;
    kernel_setup_info->ap_boot_stack_ptr = g_setup_info.ap_boot_stack_ptr;
    kernel_setup_info->cpu_id = g_setup_info.cpu_id;
    
    Print(L"Setup info prepared at 0x%lx (offset 0x10d0)\r\n", (UINT64)kernel_setup_info);
    
    // Calculate cmain virtual address
    UINT64 kernel_virt_offset = 0xffff800000000000ULL;
    void *cmain_virt_addr = (void*)(kernel_virt_offset + 0x100000 + 0x245b8);
    struct rendezvos_setup_info *kernel_setup_info_virt = 
        (struct rendezvos_setup_info*)(kernel_virt_offset + 0x100000 + 0x10d0);
    
    Print(L"Calling cmain at VIRTUAL address: 0x%lx\r\n", (UINT64)cmain_virt_addr);
    Print(L"Setup info VIRTUAL address: 0x%lx\r\n", (UINT64)kernel_setup_info_virt);
    
    // Setup kernel page tables with proper kernel mappings
    // Based on boot.S calculate_kernel_pages logic and complete page table structure
    
    Print(L"Setting up kernel page tables...\r\n");
    
    // Get page table addresses (offsets from kernel start)
    void *l0_table = (void*)((char*)kernel_target + 0x2000);  // L0_table offset
    void *l1_table = (void*)((char*)kernel_target + 0x3000);  // L1_table offset  
    void *l2_table = (void*)((char*)kernel_target + 0x4000);  // L2_table offset
    
    Print(L"L0_table (PML4) at: 0x%lx\r\n", (UINT64)l0_table);
    Print(L"L1_table (PDPT) at: 0x%lx\r\n", (UINT64)l1_table);
    Print(L"L2_table (PD) at: 0x%lx\r\n", (UINT64)l2_table);
    
    // Initialize page tables according to boot.S structure
    UINT64 *l0_entries = (UINT64*)l0_table;
    UINT64 *l1_entries = (UINT64*)l1_table;
    UINT64 *l2_entries = (UINT64*)l2_table;
    
    // Clear all page tables first
    for (int i = 0; i < 512; i++) {
        l0_entries[i] = 0;
        l1_entries[i] = 0;
        l2_entries[i] = 0;
    }
    
    // Setup L0_table (PML4) according to boot.S
    // Entry 0: maps low virtual addresses (0x0 - 0x7FFFFFFFFF)
    UINT64 l1_phys = (UINT64)l1_table;
    l0_entries[0] = l1_phys | 0x3;  // PML4E_P | PML4E_RW
    
    // Entry 256: maps high virtual addresses (0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF)
    l0_entries[256] = l1_phys | 0x7;  // PML4E_P | PML4E_RW | PML4E_US
    
    Print(L"L0[0] = 0x%lx (low virtual space)\r\n", l0_entries[0]);
    Print(L"L0[256] = 0x%lx (high virtual space)\r\n", l0_entries[256]);
    
    // Setup L1_table (PDPT) according to boot.S
    // Entry 0: maps the first 1GB of both low and high virtual space
    UINT64 l2_phys = (UINT64)l2_table;
    l1_entries[0] = l2_phys | 0x3;  // PDPTE_P | PDPTE_RW
    
    Print(L"L1[0] = 0x%lx (points to L2_table)\r\n", l1_entries[0]);
    
    // SIMPLIFIED APPROACH: Identity map the entire first 1GB using 2MB pages
    // This covers both kernel (0-4MB) and current execution area (up to ~400MB)
    Print(L"Setting up simplified identity mapping for first 1GB...\r\n");
    
    // Get current execution address for debugging
    void *current_addr;
    __asm__ volatile ("leaq (%%rip), %0" : "=r" (current_addr));
    Print(L"Current execution address: 0x%lx\r\n", (UINT64)current_addr);
    
    // Map kernel pages using boot.S logic
    // PDE flags: PDE_P | PDE_RW | PDE_PS | PDE_G (from boot.S)
    UINT64 pde_flags = 0x87;  // Present | Read/Write | Page Size | Global
    
    // Identity map the entire first 1GB (512 * 2MB = 1GB)
    // This ensures we cover kernel area (0-4MB) and most UEFI execution areas
    for (int i = 0; i < 512; i++) {
        UINT64 phys_addr = (UINT64)i * 0x200000;  // i * 2MB
        l2_entries[i] = phys_addr | pde_flags;
        
        // Only print some entries to avoid spam
        if (i < 10 || i % 50 == 0 || phys_addr >= ((UINT64)current_addr & 0xFFE00000) - 0x400000 && phys_addr <= ((UINT64)current_addr & 0xFFE00000) + 0x400000) {
            Print(L"PDE[%d] = 0x%lx (phys=0x%lx)\r\n", i, l2_entries[i], phys_addr);
        }
    }
    
    Print(L"Kernel page mappings initialized\r\n");
    
    Print(L"Modifying current UEFI page table to add kernel mappings...\r\n");
    
    // Get current CR3 value (current page table)
    UINT64 current_cr3;
    __asm__ volatile ("movq %%cr3, %0" : "=r" (current_cr3));
    Print(L"Current CR3 (UEFI page table): 0x%lx\r\n", current_cr3);
    
    // Current page table is at current_cr3 address
    UINT64 *current_pml4 = (UINT64*)(current_cr3 & 0xFFFFFFFFFFFFF000ULL); // Clear lower 12 bits
    
    Print(L"Current PML4 table at: 0x%lx\r\n", (UINT64)current_pml4);
    
    // Check if we can access current page table
    Print(L"PML4[0] = 0x%lx\r\n", current_pml4[0]);
    Print(L"PML4[256] = 0x%lx\r\n", current_pml4[256]);
    
    // Add mapping for high virtual address space (0xFFFF800000000000)
    // PML4[256] should point to a PDPT that maps kernel space
    
    if (current_pml4[256] == 0) {
        Print(L"PML4[256] is empty, adding kernel mapping...\r\n");
        
        // Temporarily disable write protection to modify page table
        UINT64 cr0_value;
        __asm__ volatile ("movq %%cr0, %0" : "=r" (cr0_value));
        Print(L"Current CR0: 0x%lx\r\n", cr0_value);
        
        // Clear WP bit (bit 16) to allow writing to read-only pages
        UINT64 cr0_modified = cr0_value & ~(1ULL << 16);
        __asm__ volatile ("movq %0, %%cr0" : : "r" (cr0_modified) : "memory");
        Print(L"Disabled write protection, CR0: 0x%lx\r\n", cr0_modified);
        
        // Use our prepared L1 table as PDPT for high virtual space
        UINT64 l1_phys = (UINT64)l1_table;
        current_pml4[256] = l1_phys | 0x7;  // Present | Read/Write | User
        
        Print(L"Added PML4[256] = 0x%lx (points to our L1_table)\r\n", current_pml4[256]);
        
        // Restore write protection
        __asm__ volatile ("movq %0, %%cr0" : : "r" (cr0_value) : "memory");
        Print(L"Restored write protection\r\n");
    } else {
        Print(L"PML4[256] already exists: 0x%lx\r\n", current_pml4[256]);
        
        // Get existing PDPT and add our mappings to it
        UINT64 existing_pdpt_addr = current_pml4[256] & 0xFFFFFFFFFFFFF000ULL;
        UINT64 *existing_pdpt = (UINT64*)existing_pdpt_addr;
        
        Print(L"Existing PDPT at: 0x%lx\r\n", existing_pdpt_addr);
        Print(L"PDPT[0] = 0x%lx\r\n", existing_pdpt[0]);
        
        // Add our L2 table to PDPT[0] if it's empty
        if (existing_pdpt[0] == 0) {
            UINT64 l2_phys = (UINT64)l2_table;
            existing_pdpt[0] = l2_phys | 0x3;  // Present | Read/Write
            Print(L"Added PDPT[0] = 0x%lx (points to our L2_table)\r\n", existing_pdpt[0]);
        } else {
            Print(L"PDPT[0] already exists: 0x%lx\r\n", existing_pdpt[0]);
        }
    }
    
    // Flush TLB to ensure new mappings take effect
    __asm__ volatile (
        "movq %%cr3, %%rax\n\t"
        "movq %%rax, %%cr3\n\t"
        :
        :
        : "rax", "memory"
    );
    
    Print(L"TLB flushed, testing virtual address access...\r\n");
    
    // Test if we can access the virtual address space
    UINT64 test_virt_addr = 0xffff800000100000ULL;
    volatile UINT64 *test_ptr = (volatile UINT64*)test_virt_addr;
    UINT64 test_value = *test_ptr;  // This should work if page tables are correct
    
    Print(L"Virtual address test passed! Value at 0x%lx = 0x%lx\r\n", test_virt_addr, test_value);
    
    Print(L"Kernel page tables loaded successfully\r\n");
    
    // Note: We're now running in the kernel's address space
    Print(L"Calling cmain in current address space...\r\n");
    
    // Disable interrupts before jumping
    __asm__ volatile ("cli");
    
    // Call cmain with VIRTUAL addresses now that page tables are loaded
    typedef void (*cmain_func_t)(struct rendezvos_setup_info *setup_info);
    
    cmain_func_t cmain_func = (cmain_func_t)cmain_virt_addr;
    cmain_func(kernel_setup_info_virt);
    
    // Should never reach here
    Print(L"ERROR: cmain returned unexpectedly!\r\n");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

/**
 * Get UEFI memory map and convert to multiboot format
 */
static EFI_STATUS get_uefi_memory_map(struct rendezvos_multiboot_info_extended *mb_info) {
    EFI_STATUS Status;
    UINTN MemoryMapSize = 0;
    EFI_MEMORY_DESCRIPTOR *MemoryMap = NULL;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    
    Print(L"Getting UEFI memory map...\r\n");
    
    // Get memory map size
    Status = uefi_call_wrapper(BS->GetMemoryMap, 5, &MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL) {
        Print(L"Failed to get memory map size: %r\r\n", Status);
        return Status;
    }
    
    // Allocate buffer for memory map (add some extra space for potential changes)
    MemoryMapSize += 2 * DescriptorSize;
    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, MemoryMapSize, (VOID**)&MemoryMap);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to allocate memory map buffer: %r\r\n", Status);
        return Status;
    }
    
    // Get actual memory map
    Status = uefi_call_wrapper(BS->GetMemoryMap, 5, &MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to get memory map: %r\r\n", Status);
        uefi_call_wrapper(BS->FreePool, 1, MemoryMap);
        return Status;
    }
    
    Print(L"UEFI memory map: %d bytes, %d entries, descriptor size: %d\r\n", 
          MemoryMapSize, MemoryMapSize / DescriptorSize, DescriptorSize);
    
    // Allocate space for multiboot memory map (in low memory)
    UINTN max_entries = MemoryMapSize / DescriptorSize;
    UINTN mmap_buffer_size = max_entries * sizeof(struct multiboot_mmap_entry);
    
    EFI_PHYSICAL_ADDRESS mmap_phys = 0x7C000;  // 496KB - safe area below 1MB
    Status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress, EfiLoaderData, 
                              (mmap_buffer_size + 4095) / 4096, &mmap_phys);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to allocate multiboot memory map buffer: %r\r\n", Status);
        // Use a fixed safe address
        mmap_phys = 0x7C000;
    }
    
    struct multiboot_mmap_entry *mb_mmap = (struct multiboot_mmap_entry*)mmap_phys;
    UINTN mb_entry_count = 0;
    
    // Convert UEFI memory map to multiboot format
    EFI_MEMORY_DESCRIPTOR *desc = MemoryMap;
    for (UINTN i = 0; i < MemoryMapSize / DescriptorSize; i++) {
        struct multiboot_mmap_entry *mb_entry = &mb_mmap[mb_entry_count];
        
        mb_entry->size = sizeof(struct multiboot_mmap_entry) - sizeof(UINT32);
        mb_entry->addr = desc->PhysicalStart;
        mb_entry->len = desc->NumberOfPages * 4096;
        
        // Convert UEFI memory type to multiboot type
        switch (desc->Type) {
            case EfiConventionalMemory:
                mb_entry->type = MULTIBOOT_MEMORY_AVAILABLE;
                break;
            case EfiACPIReclaimMemory:
                mb_entry->type = MULTIBOOT_MEMORY_ACPI_RECLAIMABLE;
                break;
            case EfiACPIMemoryNVS:
                mb_entry->type = MULTIBOOT_MEMORY_NVS;
                break;
            default:
                mb_entry->type = MULTIBOOT_MEMORY_RESERVED;
                break;
        }
        
        // Calculate total memory for mem_upper
        if (mb_entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            UINT64 end_addr = mb_entry->addr + mb_entry->len;
            if (end_addr > 0x100000) { // Above 1MB
                UINT32 mem_kb = (UINT32)((end_addr - 0x100000) / 1024);
                if (mem_kb > mb_info->mem.mem_upper) {
                    mb_info->mem.mem_upper = mem_kb;
                }
            }
        }
        
        mb_entry_count++;
        desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)desc + DescriptorSize);
    }
    
    // Set up multiboot memory info
    mb_info->flags |= MULTIBOOT_INFO_FLAG_MEM | MULTIBOOT_INFO_FLAG_MMAP;
    mb_info->mem.mem_lower = 640; // Standard lower memory
    mb_info->mmap.mmap_addr = (UINT32)mmap_phys;
    mb_info->mmap.mmap_length = mb_entry_count * sizeof(struct multiboot_mmap_entry);
    
    Print(L"Multiboot memory map: %d entries at 0x%x\r\n", mb_entry_count, mb_info->mmap.mmap_addr);
    Print(L"Memory: lower=%dKB, upper=%dKB\r\n", mb_info->mem.mem_lower, mb_info->mem.mem_upper);
    
    uefi_call_wrapper(BS->FreePool, 1, MemoryMap);
    return EFI_SUCCESS;
}

/**
 * Get ACPI RSDP table from UEFI configuration tables
 */
static EFI_STATUS get_acpi_rsdp(UINT64 *rsdp_addr) {
    EFI_GUID acpi_20_table_guid = ACPI_20_TABLE_GUID;
    EFI_GUID acpi_table_guid = ACPI_TABLE_GUID;
    
    Print(L"Looking for ACPI RSDP in UEFI configuration tables...\r\n");
    
    // Fallback to ACPI 1.0
    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *table = &ST->ConfigurationTable[i];
        
        if (CompareMem(&table->VendorGuid, &acpi_table_guid, sizeof(EFI_GUID)) == 0) {
            *rsdp_addr = (UINT64)table->VendorTable;
            Print(L"Found ACPI 1.0 RSDP at: 0x%lx\r\n", *rsdp_addr);
            return EFI_SUCCESS;
        }
    }
    
    Print(L"ACPI RSDP not found in UEFI configuration tables\r\n");
    return EFI_NOT_FOUND;
}

/**
 * RendezvOS UEFI Application Entry Point
 */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status;
    void *kernel_data = NULL;
    UINTN kernel_size = 0;
    BOOLEAN kernel_loaded = FALSE;
    
    // Save ImageHandle for later use
    g_image_handle = ImageHandle;
    
    // Initialize EFI library
    InitializeLib(ImageHandle, SystemTable);
    
    Print(L"=== RendezvOS UEFI Stub ===\r\n");
    Print(L"Initializing UEFI environment...\r\n");
    
    // Initialize structures
    SetMem(&g_setup_info, sizeof(g_setup_info), 0);
    SetMem(&g_multiboot_info, sizeof(g_multiboot_info), 0);
    
    // Set UEFI boot magic
    g_setup_info.multiboot_magic = 0x2BADB002;  // Correct Multiboot magic number
    
    // Set basic multiboot info
    g_multiboot_info.flags |= MULTIBOOT_INFO_FLAG_MEM;
    g_multiboot_info.mem.mem_lower = 640;
    g_multiboot_info.mem.mem_upper = 128 * 1024; // 128MB default
    
    // Set up setup_info
    g_setup_info.phy_addr_width = 46;
    g_setup_info.vir_addr_width = 48;
    g_setup_info.multiboot_info_struct_ptr = (UINT32)(UINTN)&g_multiboot_info;
    
    // Get UEFI memory map and convert to multiboot format
    Print(L"Getting system memory information...\r\n");
    Status = get_uefi_memory_map(&g_multiboot_info);
    if (EFI_ERROR(Status)) {
        Print(L"Warning: Failed to get memory map, using defaults\r\n");
        // Keep default values
        g_multiboot_info.flags |= MULTIBOOT_INFO_FLAG_MEM;
        g_multiboot_info.mem.mem_lower = 640;
        g_multiboot_info.mem.mem_upper = 128 * 1024; // 128MB default
    }
    
    // Get ACPI RSDP table
    Print(L"Getting ACPI information...\r\n");
    Status = get_acpi_rsdp(&g_setup_info.rsdp_addr);
    if (EFI_ERROR(Status)) {
        Print(L"Warning: Failed to get ACPI RSDP, ACPI may not work\r\n");
        g_setup_info.rsdp_addr = 0;
    }
    
    Print(L"Basic setup complete\r\n");
    
    // Try to load kernel - first try embedded, then file system
    Print(L"Looking for kernel...\r\n");
    
    Status = load_embedded_kernel(&kernel_data, &kernel_size);
    if (!EFI_ERROR(Status)) {
        Print(L"Using embedded kernel\r\n");
        kernel_loaded = TRUE;
    } else {
        Print(L"No embedded kernel, trying file system...\r\n");
        Status = load_kernel_from_file(ImageHandle, &kernel_data, &kernel_size);
        if (!EFI_ERROR(Status)) {
            Print(L"Using kernel from file system\r\n");
            kernel_loaded = TRUE;
        } else {
            Print(L"Failed to load kernel from file system\r\n");
        }
    }
    
    if (!kernel_loaded) {
        Print(L"ERROR: No kernel found!\r\n");
        Print(L"This stub needs either:\r\n");
        Print(L"  1. An embedded kernel (built with build_uefi_with_kernel.sh)\r\n");
        Print(L"  2. A kernel.bin file in the root directory\r\n");
        Print(L"\r\nPress any key to exit...\r\n");
        
        UINTN Index;
        EFI_INPUT_KEY Key;
        uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &Index);
        uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);
        
        return EFI_LOAD_ERROR;
    }
    
    Print(L"Kernel loaded successfully: %d bytes\r\n", kernel_size);
    Print(L"Preparing to jump to kernel at 0x100000...\r\n");
    
    // Wait a moment for output to be visible
    uefi_call_wrapper(BS->Stall, 1, 2000000); // 2 seconds
    
    // Jump to kernel (this will not return)
    jump_to_kernel(kernel_data, kernel_size);
    
    return EFI_SUCCESS;
} 