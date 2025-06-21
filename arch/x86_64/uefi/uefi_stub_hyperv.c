#include <efi.h>
#include <efilib.h>

// Helper macros
#define MIN(a, b) ((a) < (b) ? (a) : (b))

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
#define MULTIBOOT_INFO_FLAG_MMAP (1 << 6)

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
#define MULTIBOOT_INFO_FLAG_MEM         (1 << 0)
#define MULTIBOOT_INFO_FLAG_FRAMEBUFFER (1 << 12)
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB  1

// Global variables
static struct rendezvos_setup_info g_setup_info;
static struct rendezvos_multiboot_info_extended g_multiboot_info;
static EFI_HANDLE g_image_handle; // Save ImageHandle for later use

// External symbols for embedded kernel (if present)
#ifdef KERNEL_EMBEDDED
extern char kernel_start[];
extern char kernel_end[];
extern char kernel_size[];
#endif

// Function declarations
static EFI_STATUS
get_uefi_memory_map(struct rendezvos_multiboot_info_extended *mb_info);
static EFI_STATUS get_acpi_rsdp(UINT64 *rsdp_addr);
static EFI_STATUS chunked_copymem(void *dest, void *src, UINTN total_size);
static void jump_to_kernel(void *kernel_data, UINTN kernel_size);

/**
 * Load kernel from embedded data
 */
static EFI_STATUS load_embedded_kernel(void **kernel_data, UINTN *kernel_size)
{
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
static EFI_STATUS load_kernel_from_file(EFI_HANDLE ImageHandle,
                                        void **kernel_data, UINTN *kernel_size)
{
        EFI_STATUS Status;
        EFI_LOADED_IMAGE *LoadedImage;
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
        EFI_FILE_PROTOCOL *Root;
        EFI_FILE_PROTOCOL *KernelFile;
        EFI_FILE_INFO *FileInfo;
        UINTN FileInfoSize;

        // Get loaded image protocol
        Status = uefi_call_wrapper(BS->HandleProtocol,
                                   3,
                                   ImageHandle,
                                   &LoadedImageProtocol,
                                   (VOID **)&LoadedImage);
        if (EFI_ERROR(Status)) {
                Print(L"Failed to get loaded image protocol: %r\r\n", Status);
                return Status;
        }

        // Get file system protocol
        Status = uefi_call_wrapper(BS->HandleProtocol,
                                   3,
                                   LoadedImage->DeviceHandle,
                                   &FileSystemProtocol,
                                   (VOID **)&FileSystem);
        if (EFI_ERROR(Status)) {
                Print(L"Failed to get file system protocol: %r\r\n", Status);
                return Status;
        }

        // Open root directory
        Status =
                uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &Root);
        if (EFI_ERROR(Status)) {
                Print(L"Failed to open root directory: %r\r\n", Status);
                return Status;
        }

        // Open kernel.bin file
        Status = uefi_call_wrapper(Root->Open,
                                   5,
                                   Root,
                                   &KernelFile,
                                   L"kernel.bin",
                                   EFI_FILE_MODE_READ,
                                   0);
        if (EFI_ERROR(Status)) {
                Print(L"Failed to open kernel.bin: %r\r\n", Status);
                uefi_call_wrapper(Root->Close, 1, Root);
                return Status;
        }

        Print(L"Found kernel.bin file\r\n");

        // Get file size
        FileInfoSize = sizeof(EFI_FILE_INFO) + 256;
        Status = uefi_call_wrapper(BS->AllocatePool,
                                   3,
                                   EfiLoaderData,
                                   FileInfoSize,
                                   (VOID **)&FileInfo);
        if (EFI_ERROR(Status)) {
                Print(L"Failed to allocate file info buffer: %r\r\n", Status);
                uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
                uefi_call_wrapper(Root->Close, 1, Root);
                return Status;
        }

        Status = uefi_call_wrapper(KernelFile->GetInfo,
                                   4,
                                   KernelFile,
                                   &GenericFileInfo,
                                   &FileInfoSize,
                                   FileInfo);
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
        Status = uefi_call_wrapper(
                BS->AllocatePool, 3, EfiLoaderData, *kernel_size, kernel_data);
        if (EFI_ERROR(Status)) {
                Print(L"Failed to allocate kernel buffer: %r\r\n", Status);
                uefi_call_wrapper(BS->FreePool, 1, FileInfo);
                uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
                uefi_call_wrapper(Root->Close, 1, Root);
                return Status;
        }

        // Read kernel data
        Status = uefi_call_wrapper(
                KernelFile->Read, 3, KernelFile, kernel_size, *kernel_data);
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
 * Chunked memory copy for large kernels - UEFI safe version
 */
static EFI_STATUS chunked_copymem(void *dest, void *src, UINTN total_size)
{
        UINTN chunk_size = 64 * 1024; // 64KB chunks
        UINTN copied = 0;
        UINTN chunks_total;
        UINTN current_chunk;
        UINTN percent;

        Print(L"=== Chunked CopyMem ===\r\n");
        Print(L"Total size: %d bytes (%d KB)\r\n",
              total_size,
              total_size / 1024);
        Print(L"Chunk size: %d KB\r\n", chunk_size / 1024);

        // Calculate total chunks (avoid division in loop)
        chunks_total = (total_size + chunk_size - 1) / chunk_size;

        while (copied < total_size) {
                current_chunk = MIN(chunk_size, total_size - copied);

                Print(L"Copying chunk %d/%d (offset: %d KB, size: %d KB)\r\n",
                      (copied / chunk_size) + 1,
                      chunks_total,
                      copied / 1024,
                      current_chunk / 1024);

                // Execute small block copy
                CopyMem((UINT8 *)dest + copied,
                        (UINT8 *)src + copied,
                        current_chunk);

                copied += current_chunk;

                // Show progress (avoid floating point)
                percent = (copied * 100) / total_size;
                Print(L"Progress: %d%% (%d/%d KB)\r\n",
                      percent,
                      copied / 1024,
                      total_size / 1024);

                // Small delay to avoid UEFI timeout and give Hyper-V time to
                // process
                if (copied < total_size) {
                        uefi_call_wrapper(BS->Stall, 1, 50000); // 0.05 seconds
                }
        }

        Print(L"Chunked copy completed successfully!\r\n");

        // Verify copy result
        Print(L"Verifying copy...\r\n");
        if (CompareMem(dest, src, MIN(total_size, 4096)) == 0) {
                Print(L"First 4KB verification: PASSED\r\n");

                // Check middle part
                UINTN mid_offset = total_size / 2;
                if (mid_offset + 4096 <= total_size
                    && CompareMem((UINT8 *)dest + mid_offset,
                                  (UINT8 *)src + mid_offset,
                                  4096)
                               == 0) {
                        Print(L"Middle 4KB verification: PASSED\r\n");

                        // Check last part
                        if (total_size >= 4096) {
                                UINTN end_offset = total_size - 4096;
                                if (CompareMem((UINT8 *)dest + end_offset,
                                               (UINT8 *)src + end_offset,
                                               4096)
                                    == 0) {
                                        Print(L"Last 4KB verification: PASSED\r\n");
                                        Print(L"Chunked copy verification: ALL PASSED\r\n");
                                        return EFI_SUCCESS;
                                }
                        }
                }
        }

        Print(L"ERROR: Copy verification failed!\r\n");
        return EFI_DEVICE_ERROR;
}

/**
 * Safe method to modify UEFI page table with write protection handling
 */
static EFI_STATUS safe_modify_uefi_pagetable(UINT64 l1_phys)
{
        UINT64 current_cr3;
        UINT64 current_cr0;
        UINT64 *current_pml4;
        EFI_STATUS result = EFI_SUCCESS;

        Print(L"=== Safe UEFI Page Table Modification ===\r\n");

        // Get current page table
        __asm__ volatile("movq %%cr3, %0" : "=r"(current_cr3));
        current_pml4 = (UINT64 *)(current_cr3 & 0xFFFFFFFFFFFFF000ULL);

        Print(L"Current PML4: 0x%lx\r\n", (UINT64)current_pml4);
        Print(L"Target PML4[256]: 0x%lx\r\n", (UINT64)&current_pml4[256]);
        Print(L"Current PML4[256]: 0x%lx\r\n", current_pml4[256]);

        // Check if already mapped
        if (current_pml4[256] != 0) {
                Print(L"High virtual space already mapped, skipping modification\r\n");
                return EFI_SUCCESS;
        }

        // Method 1: Try to disable write protection temporarily
        Print(L"Attempting to disable write protection...\r\n");

        // Get current CR0
        __asm__ volatile("movq %%cr0, %0" : "=r"(current_cr0));
        Print(L"Current CR0: 0x%lx\r\n", current_cr0);

        // Check WP bit (bit 16)
        if (current_cr0 & (1ULL << 16)) {
                Print(L"Write protection is enabled, attempting to disable...\r\n");

                // Disable write protection
                UINT64 new_cr0 = current_cr0 & ~(1ULL << 16);
                __asm__ volatile("movq %0, %%cr0" : : "r"(new_cr0) : "memory");

                // Verify it was disabled
                UINT64 verify_cr0;
                __asm__ volatile("movq %%cr0, %0" : "=r"(verify_cr0));

                if (verify_cr0 & (1ULL << 16)) {
                        Print(L"Failed to disable write protection\r\n");
                        result = EFI_ACCESS_DENIED;
                } else {
                        Print(L"Write protection disabled successfully\r\n");

                        // Now try to modify the page table
                        current_pml4[256] = l1_phys | 0x7;
                        Print(L"PML4[256] modified successfully\r\n");

                        // Restore write protection
                        __asm__ volatile("movq %0, %%cr0"
                                         :
                                         : "r"(current_cr0)
                                         : "memory");
                        Print(L"Write protection restored\r\n");

                        result = EFI_SUCCESS;
                }
        } else {
                Print(L"Write protection is already disabled\r\n");

                // Try direct modification
                current_pml4[256] = l1_phys | 0x7;
                Print(L"PML4[256] modified successfully\r\n");
                result = EFI_SUCCESS;
        }

        return result;
}

/**
 * Alternative method: Create and switch to our own page table
 */
static EFI_STATUS create_and_switch_pagetable(void *l0_table, void *l1_table,
                                              void *l2_table)
{
        Print(L"=== Creating New Page Table ===\r\n");

        UINT64 *l0_entries = (UINT64 *)l0_table;
        UINT64 *l1_entries = (UINT64 *)l1_table;
        UINT64 *l2_entries = (UINT64 *)l2_table;

        // Clear page tables
        Print(L"Clearing new page tables...\r\n");
        for (int i = 0; i < 512; i++) {
                l0_entries[i] = 0;
                l1_entries[i] = 0;
                l2_entries[i] = 0;
        }

        // Set up page table entries
        Print(L"Setting up new page table entries...\r\n");
        UINT64 l1_phys = (UINT64)l1_table;
        UINT64 l2_phys = (UINT64)l2_table;

        l0_entries[0] = l1_phys | 0x3; // Low virtual space (0x0...)
        l0_entries[256] = l1_phys | 0x7; // High virtual space (0xffff8...)
        l1_entries[0] = l2_phys | 0x3; // First 1GB

        // Identity map first 1GB (512 * 2MB = 1GB)
        Print(L"Setting up identity mapping...\r\n");
        for (int i = 0; i < 512; i++) {
                UINT64 phys_addr = (UINT64)i * 0x200000;
                l2_entries[i] = phys_addr | 0x87; // Present | RW | PS | Global
        }

        Print(L"New page table created successfully\r\n");
        Print(L"Switching to new page table...\r\n");

        // Switch to our page table
        UINT64 new_cr3 = (UINT64)l0_table;
        __asm__ volatile("movq %0, %%cr3" : : "r"(new_cr3) : "memory");

        // Verify the switch
        UINT64 verify_cr3;
        __asm__ volatile("movq %%cr3, %0" : "=r"(verify_cr3));

        if (verify_cr3 == new_cr3) {
                Print(L"Successfully switched to new page table: 0x%lx\r\n",
                      new_cr3);
                return EFI_SUCCESS;
        } else {
                Print(L"Failed to switch page table: expected 0x%lx, got 0x%lx\r\n",
                      new_cr3,
                      verify_cr3);
                return EFI_DEVICE_ERROR;
        }
}

/**
 * Updated safe page table kernel launch with better error handling
 */
static void safe_pagetable_kernel_launch(void *kernel_data, UINTN kernel_size)
{
        Print(L"=== Safe Page Table Kernel Launch ===\r\n");
        Print(L"Kernel size: %d bytes (0x%x)\r\n", kernel_size, kernel_size);

        // Step 1: Copy kernel
        Print(L"Step 1: Copying kernel to 0x100000...\r\n");
        EFI_STATUS Status =
                chunked_copymem((void *)0x100000, kernel_data, kernel_size);
        if (EFI_ERROR(Status)) {
                Print(L"Kernel copy failed: %r\r\n", Status);
                return;
        }
        Print(L"Kernel copied successfully\r\n");

        // Step 2: Basic setup
        Print(L"Step 2: Setting up multiboot info...\r\n");
        struct rendezvos_multiboot_info_extended *mb_info_32 =
                (struct rendezvos_multiboot_info_extended *)0x7E000;

        SetMem(mb_info_32, sizeof(*mb_info_32), 0);
        mb_info_32->flags = g_multiboot_info.flags;
        mb_info_32->mem.mem_lower = g_multiboot_info.mem.mem_lower;
        mb_info_32->mem.mem_upper = g_multiboot_info.mem.mem_upper;
        mb_info_32->mmap.mmap_addr = g_multiboot_info.mmap.mmap_addr;
        mb_info_32->mmap.mmap_length = g_multiboot_info.mmap.mmap_length;

        UINT32 magic = g_setup_info.multiboot_magic;
        UINT32 info = (UINT32)(UINTN)mb_info_32;

        // Step 3: Set up kernel setup_info
        Print(L"Step 3: Setting up kernel setup_info...\r\n");
        struct rendezvos_setup_info *kernel_setup_info =
                (struct rendezvos_setup_info *)(0x100000 + 0x10d0);

        kernel_setup_info->multiboot_magic = magic;
        kernel_setup_info->multiboot_info_struct_ptr = info;
        kernel_setup_info->phy_addr_width = g_setup_info.phy_addr_width;
        kernel_setup_info->vir_addr_width = g_setup_info.vir_addr_width;
        kernel_setup_info->log_buffer_addr = g_setup_info.log_buffer_addr;
        kernel_setup_info->rsdp_addr = g_setup_info.rsdp_addr;
        kernel_setup_info->ap_boot_stack_ptr = g_setup_info.ap_boot_stack_ptr;
        kernel_setup_info->cpu_id = g_setup_info.cpu_id;

        Print(L"Setup info configured\r\n");

        // Step 4: Calculate safe page table locations (after kernel)
        Print(L"Step 4: Calculating safe page table locations...\r\n");

        // Place page tables after kernel end, aligned to page boundary
        UINT64 kernel_end = 0x100000 + kernel_size;
        UINT64 safe_base = (kernel_end + 4095) & ~4095ULL; // 4KB align

        void *l0_table = (void *)(safe_base);
        void *l1_table = (void *)(safe_base + 0x1000);
        void *l2_table = (void *)(safe_base + 0x2000);

        Print(L"Safe page table locations:\r\n");
        Print(L"  Kernel end: 0x%lx\r\n", kernel_end);
        Print(L"  L0 table: 0x%lx\r\n", (UINT64)l0_table);
        Print(L"  L1 table: 0x%lx\r\n", (UINT64)l1_table);
        Print(L"  L2 table: 0x%lx\r\n", (UINT64)l2_table);

        // Step 5: Try different page table strategies
        Print(L"Step 5: Setting up page tables (trying multiple methods)...\r\n");

        UINT64 l1_phys = (UINT64)l1_table;
        BOOLEAN virtual_mapping_enabled = FALSE;

        // Method A: Try to safely modify UEFI page table
        Print(L"Method A: Attempting to modify UEFI page table...\r\n");
        Status = safe_modify_uefi_pagetable(l1_phys);
        if (!EFI_ERROR(Status)) {
                Print(L"Method A: SUCCESS - UEFI page table modified\r\n");
                virtual_mapping_enabled = TRUE;

                // Set up our page tables for the high virtual space
                UINT64 *l1_entries = (UINT64 *)l1_table;
                UINT64 *l2_entries = (UINT64 *)l2_table;
                UINT64 l2_phys = (UINT64)l2_table;

                // Clear and set up our tables
                for (int i = 0; i < 512; i++) {
                        l1_entries[i] = 0;
                        l2_entries[i] = 0;
                }

                l1_entries[0] = l2_phys | 0x3;

                for (int i = 0; i < 512; i++) {
                        UINT64 phys_addr = (UINT64)i * 0x200000;
                        l2_entries[i] = phys_addr | 0x87;
                }

                // Flush TLB
                __asm__ volatile("movq %%cr3, %%rax\n\t"
                                 "movq %%rax, %%cr3\n\t"
                                 :
                                 :
                                 : "rax", "memory");

        } else {
                Print(L"Method A: FAILED - Cannot modify UEFI page table\r\n");

                // Method B: Create and switch to our own page table
                Print(L"Method B: Creating our own page table...\r\n");
                Status = create_and_switch_pagetable(
                        l0_table, l1_table, l2_table);
                if (!EFI_ERROR(Status)) {
                        Print(L"Method B: SUCCESS - Using our own page table\r\n");
                        virtual_mapping_enabled = TRUE;
                } else {
                        Print(L"Method B: FAILED - Cannot create new page table\r\n");
                        Print(L"Method C: Falling back to physical addresses only\r\n");
                        virtual_mapping_enabled = FALSE;
                }
        }

        // Step 6: Call kernel with appropriate addressing
        Print(L"Step 6: Calling kernel...\r\n");

        Print(L"Using physical addresses (virtual mapping failed)\r\n");

        void *cmain_phys_addr = (void *)(0x100000 + 0x245b3);

        Print(L"cmain physical address: 0x%lx\r\n", (UINT64)cmain_phys_addr);
        Print(L"setup_info physical address: 0x%lx\r\n",
              (UINT64)kernel_setup_info);

        __asm__ volatile("cli");

        typedef void (*cmain_func_t)(struct rendezvos_setup_info *);
        cmain_func_t cmain_func = (cmain_func_t)cmain_phys_addr;

        Print(L"Calling cmain with physical addresses...\r\n");
        uefi_call_wrapper(BS->Stall, 1, 1000000);

        cmain_func(kernel_setup_info);
        // }

        Print(L"ERROR: cmain returned!\r\n");
        while (1) {
                __asm__ volatile("hlt");
        }
}

/**
 * Jump to kernel with conflict-aware approach
 */
static void jump_to_kernel(void *kernel_data, UINTN kernel_size)
{
        Print(L"=== Jump to Kernel (Conflict-Aware) ===\r\n");

        // Avoid floating point by using integer arithmetic
        UINTN size_mb = kernel_size / (1024 * 1024);
        UINTN size_kb_remainder = (kernel_size % (1024 * 1024)) / 1024;

        Print(L"Kernel size: %d bytes (%d MB %d KB)\r\n",
              kernel_size,
              size_mb,
              size_kb_remainder);

        // Check for page table conflict
        if (kernel_size > 0x2000) {
                Print(L"Large kernel detected (>8KB), using safe page table placement\r\n");
        } else {
                Print(L"Small kernel detected, conflict unlikely\r\n");
        }

        // Use safe page table approach for all cases
        safe_pagetable_kernel_launch(kernel_data, kernel_size);
}

/**
 * Get UEFI memory map and convert to multiboot format
 */
static EFI_STATUS
get_uefi_memory_map(struct rendezvos_multiboot_info_extended *mb_info)
{
        EFI_STATUS Status;
        UINTN MemoryMapSize = 0;
        EFI_MEMORY_DESCRIPTOR *MemoryMap = NULL;
        UINTN MapKey;
        UINTN DescriptorSize;
        UINT32 DescriptorVersion;

        Print(L"Getting UEFI memory map...\r\n");

        // Get memory map size
        Status = uefi_call_wrapper(BS->GetMemoryMap,
                                   5,
                                   &MemoryMapSize,
                                   MemoryMap,
                                   &MapKey,
                                   &DescriptorSize,
                                   &DescriptorVersion);
        if (Status != EFI_BUFFER_TOO_SMALL) {
                Print(L"Failed to get memory map size: %r\r\n", Status);
                return Status;
        }

        // Allocate buffer for memory map (add some extra space for potential
        // changes)
        MemoryMapSize += 2 * DescriptorSize;
        Status = uefi_call_wrapper(BS->AllocatePool,
                                   3,
                                   EfiLoaderData,
                                   MemoryMapSize,
                                   (VOID **)&MemoryMap);
        if (EFI_ERROR(Status)) {
                Print(L"Failed to allocate memory map buffer: %r\r\n", Status);
                return Status;
        }

        // Get actual memory map
        Status = uefi_call_wrapper(BS->GetMemoryMap,
                                   5,
                                   &MemoryMapSize,
                                   MemoryMap,
                                   &MapKey,
                                   &DescriptorSize,
                                   &DescriptorVersion);
        if (EFI_ERROR(Status)) {
                Print(L"Failed to get memory map: %r\r\n", Status);
                uefi_call_wrapper(BS->FreePool, 1, MemoryMap);
                return Status;
        }

        Print(L"UEFI memory map: %d bytes, %d entries, descriptor size: %d\r\n",
              MemoryMapSize,
              MemoryMapSize / DescriptorSize,
              DescriptorSize);

        // Allocate space for multiboot memory map (in low memory)
        UINTN max_entries = MemoryMapSize / DescriptorSize;
        UINTN mmap_buffer_size =
                max_entries * sizeof(struct multiboot_mmap_entry);

        EFI_PHYSICAL_ADDRESS mmap_phys = 0x7C000; // 496KB - safe area below 1MB
        Status = uefi_call_wrapper(BS->AllocatePages,
                                   4,
                                   AllocateAddress,
                                   EfiLoaderData,
                                   (mmap_buffer_size + 4095) / 4096,
                                   &mmap_phys);
        if (EFI_ERROR(Status)) {
                Print(L"Failed to allocate multiboot memory map buffer: %r\r\n",
                      Status);
                // Use a fixed safe address
                mmap_phys = 0x7C000;
        }

        struct multiboot_mmap_entry *mb_mmap =
                (struct multiboot_mmap_entry *)mmap_phys;
        UINTN mb_entry_count = 0;

        // Convert UEFI memory map to multiboot format
        EFI_MEMORY_DESCRIPTOR *desc = MemoryMap;
        for (UINTN i = 0; i < MemoryMapSize / DescriptorSize; i++) {
                struct multiboot_mmap_entry *mb_entry =
                        &mb_mmap[mb_entry_count];

                mb_entry->size =
                        sizeof(struct multiboot_mmap_entry) - sizeof(UINT32);
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
                                UINT32 mem_kb =
                                        (UINT32)((end_addr - 0x100000) / 1024);
                                if (mem_kb > mb_info->mem.mem_upper) {
                                        mb_info->mem.mem_upper = mem_kb;
                                }
                        }
                }

                mb_entry_count++;
                desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)desc
                                                 + DescriptorSize);
        }

        // Set up multiboot memory info
        mb_info->flags |= MULTIBOOT_INFO_FLAG_MEM | MULTIBOOT_INFO_FLAG_MMAP;
        mb_info->mem.mem_lower = 640; // Standard lower memory
        mb_info->mmap.mmap_addr = (UINT32)mmap_phys;
        mb_info->mmap.mmap_length =
                mb_entry_count * sizeof(struct multiboot_mmap_entry);

        Print(L"Multiboot memory map: %d entries at 0x%x\r\n",
              mb_entry_count,
              mb_info->mmap.mmap_addr);
        Print(L"Memory: lower=%dKB, upper=%dKB\r\n",
              mb_info->mem.mem_lower,
              mb_info->mem.mem_upper);

        uefi_call_wrapper(BS->FreePool, 1, MemoryMap);
        return EFI_SUCCESS;
}

/**
 * Get ACPI RSDP table from UEFI configuration tables
 */
static EFI_STATUS get_acpi_rsdp(UINT64 *rsdp_addr)
{
        EFI_GUID acpi_20_table_guid = ACPI_20_TABLE_GUID;
        EFI_GUID acpi_table_guid = ACPI_TABLE_GUID;

        Print(L"Looking for ACPI RSDP in UEFI configuration tables...\r\n");

        for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
                EFI_CONFIGURATION_TABLE *table = &ST->ConfigurationTable[i];

                if (CompareMem(&table->VendorGuid,
                               &acpi_table_guid,
                               sizeof(EFI_GUID))
                    == 0) {
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
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *SystemTable)
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
        g_setup_info.multiboot_magic = 0x2BADB002; // Correct Multiboot magic
                                                   // number

        // Set basic multiboot info
        g_multiboot_info.flags |= MULTIBOOT_INFO_FLAG_MEM;
        g_multiboot_info.mem.mem_lower = 640;
        g_multiboot_info.mem.mem_upper = 128 * 1024; // 128MB default

        // Set up setup_info
        g_setup_info.phy_addr_width = 46;
        g_setup_info.vir_addr_width = 48;
        g_setup_info.multiboot_info_struct_ptr =
                (UINT32)(UINTN)&g_multiboot_info;

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
                Status = load_kernel_from_file(
                        ImageHandle, &kernel_data, &kernel_size);
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
                uefi_call_wrapper(
                        BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &Index);
                uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);

                return EFI_LOAD_ERROR;
        }

        Print(L"Kernel loaded successfully: %d bytes\r\n", kernel_size);
        Print(L"Preparing to jump to kernel...\r\n");

        // Wait a moment for output to be visible
        uefi_call_wrapper(BS->Stall, 1, 2000000); // 2 seconds

        // Jump to kernel using conflict-aware approach
        jump_to_kernel(kernel_data, kernel_size);

        return EFI_SUCCESS;
}