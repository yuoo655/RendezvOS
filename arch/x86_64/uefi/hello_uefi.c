#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    
    Print(L"Hello, UEFI World!\r\n");
    Print(L"This is a test application.\r\n");
    
    // Wait for user input or timeout
    UINTN Index;
    EFI_INPUT_KEY Key;
    
    Print(L"Press any key to continue...\r\n");
    
    // Wait for keystroke
    uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &Index);
    uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);
    
    Print(L"Key pressed: %c\r\n", Key.UnicodeChar);
    Print(L"Goodbye!\r\n");
    
    return EFI_SUCCESS;
} 