#ifndef __XEN_EFI_H__
#define __XEN_EFI_H__

#include <xen/types.h>

#if defined(__ia64__)
# #include <linux/efi.h>
#else

# if defined(__i386__)
#  define efi_enabled 0
# else
extern const bool_t efi_enabled;
# endif

#define EFI_INVALID_TABLE_ADDR (~0UL)

/* Add fields here only if they need to be referenced from non-EFI code. */
struct efi {
    unsigned long acpi;         /* ACPI table (IA64 ext 0.71) */
    unsigned long acpi20;       /* ACPI table (ACPI 2.0) */
    unsigned long smbios;       /* SM BIOS table */
};

extern struct efi efi;

#endif

union xenpf_efi_info;
union compat_pf_efi_info;

void efi_init_memory(void);
#ifndef COMPAT
int efi_get_info(uint32_t idx, union xenpf_efi_info *);
#endif
int efi_compat_get_info(uint32_t idx, union compat_pf_efi_info *);

#endif /* __XEN_EFI_H__ */
