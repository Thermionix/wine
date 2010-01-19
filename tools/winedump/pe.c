/*
 *	PE dumping utility
 *
 * 	Copyright 2001 Eric Pouech
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <time.h>
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#include <fcntl.h>

#define NONAMELESSUNION
#define NONAMELESSSTRUCT
#include "windef.h"
#include "winbase.h"
#include "winedump.h"

static const IMAGE_NT_HEADERS32*        PE_nt_headers;

const char *get_machine_str(int mach)
{
    switch (mach)
    {
    case IMAGE_FILE_MACHINE_UNKNOWN:	return "Unknown";
    case IMAGE_FILE_MACHINE_I860:	return "i860";
    case IMAGE_FILE_MACHINE_I386:	return "i386";
    case IMAGE_FILE_MACHINE_R3000:	return "R3000";
    case IMAGE_FILE_MACHINE_R4000:	return "R4000";
    case IMAGE_FILE_MACHINE_R10000:	return "R10000";
    case IMAGE_FILE_MACHINE_ALPHA:	return "Alpha";
    case IMAGE_FILE_MACHINE_POWERPC:	return "PowerPC";
    case IMAGE_FILE_MACHINE_AMD64:      return "AMD64";
    case IMAGE_FILE_MACHINE_IA64:       return "IA64";
    case IMAGE_FILE_MACHINE_ARM:        return "ARM";
    }
    return "???";
}

static const void*	RVA(unsigned long rva, unsigned long len)
{
    IMAGE_SECTION_HEADER*	sectHead;
    int				i;

    if (rva == 0) return NULL;

    sectHead = IMAGE_FIRST_SECTION(PE_nt_headers);
    for (i = PE_nt_headers->FileHeader.NumberOfSections - 1; i >= 0; i--)
    {
        if (sectHead[i].VirtualAddress <= rva &&
            rva + len <= (DWORD)sectHead[i].VirtualAddress + sectHead[i].SizeOfRawData)
        {
            /* return image import directory offset */
            return PRD(sectHead[i].PointerToRawData + rva - sectHead[i].VirtualAddress, len);
        }
    }

    return NULL;
}

static const IMAGE_NT_HEADERS32 *get_nt_header( void )
{
    const IMAGE_DOS_HEADER *dos;
    dos = PRD(0, sizeof(*dos));
    if (!dos) return NULL;
    return PRD(dos->e_lfanew, sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER));
}

static int is_fake_dll( void )
{
    static const char fakedll_signature[] = "Wine placeholder DLL";
    const IMAGE_DOS_HEADER *dos;

    dos = PRD(0, sizeof(*dos) + sizeof(fakedll_signature));

    if (dos && dos->e_lfanew >= sizeof(*dos) + sizeof(fakedll_signature) &&
        !memcmp( dos + 1, fakedll_signature, sizeof(fakedll_signature) )) return TRUE;
    return FALSE;
}

static const void *get_dir_and_size(unsigned int idx, unsigned int *size)
{
    if(PE_nt_headers->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        const IMAGE_OPTIONAL_HEADER64 *opt = (const IMAGE_OPTIONAL_HEADER64*)&PE_nt_headers->OptionalHeader;
        if (idx >= opt->NumberOfRvaAndSizes)
            return NULL;
        if(size)
            *size = opt->DataDirectory[idx].Size;
        return RVA(opt->DataDirectory[idx].VirtualAddress,
                   opt->DataDirectory[idx].Size);
    }
    else
    {
        const IMAGE_OPTIONAL_HEADER32 *opt = (const IMAGE_OPTIONAL_HEADER32*)&PE_nt_headers->OptionalHeader;
        if (idx >= opt->NumberOfRvaAndSizes)
            return NULL;
        if(size)
            *size = opt->DataDirectory[idx].Size;
        return RVA(opt->DataDirectory[idx].VirtualAddress,
                   opt->DataDirectory[idx].Size);
    }
}

static	const void*	get_dir(unsigned idx)
{
    return get_dir_and_size(idx, 0);
}

static const char * const DirectoryNames[16] = {
    "EXPORT",		"IMPORT",	"RESOURCE", 	"EXCEPTION",
    "SECURITY", 	"BASERELOC", 	"DEBUG", 	"ARCHITECTURE",
    "GLOBALPTR", 	"TLS", 		"LOAD_CONFIG",	"Bound IAT",
    "IAT", 		"Delay IAT",	"CLR Header", ""
};

static const char *get_magic_type(WORD magic)
{
    switch(magic) {
        case IMAGE_NT_OPTIONAL_HDR32_MAGIC:
            return "32bit";
        case IMAGE_NT_OPTIONAL_HDR64_MAGIC:
            return "64bit";
        case IMAGE_ROM_OPTIONAL_HDR_MAGIC:
            return "ROM";
    }
    return "???";
}

static inline void print_word(const char *title, WORD value)
{
    printf("  %-34s 0x%-4X         %u\n", title, value, value);
}

static inline void print_dword(const char *title, DWORD value)
{
    printf("  %-34s 0x%-8x     %u\n", title, value, value);
}

static inline void print_longlong(const char *title, ULONGLONG value)
{
    printf("  %-34s 0x", title);
    if(value >> 32)
        printf("%lx%08lx\n", (unsigned long)(value >> 32), (unsigned long)value);
    else
        printf("%lx\n", (unsigned long)value);
}

static inline void print_ver(const char *title, BYTE major, BYTE minor)
{
    printf("  %-34s %u.%02u\n", title, major, minor);
}

static inline void print_subsys(const char *title, WORD value)
{
    const char *str;
    switch (value)
    {
        default:
        case IMAGE_SUBSYSTEM_UNKNOWN:       str = "Unknown";        break;
        case IMAGE_SUBSYSTEM_NATIVE:        str = "Native";         break;
        case IMAGE_SUBSYSTEM_WINDOWS_GUI:   str = "Windows GUI";    break;
        case IMAGE_SUBSYSTEM_WINDOWS_CUI:   str = "Windows CUI";    break;
        case IMAGE_SUBSYSTEM_OS2_CUI:       str = "OS/2 CUI";       break;
        case IMAGE_SUBSYSTEM_POSIX_CUI:     str = "Posix CUI";      break;
    }
    printf("  %-34s 0x%X (%s)\n", title, value, str);
}

static inline void print_dllflags(const char *title, WORD value)
{
    printf("  %-34s 0x%X\n", title, value);
#define X(f,s) if (value & f) printf("    %s\n", s)
    X(IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE,          "DYNAMIC_BASE");
    X(IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY,       "FORCE_INTEGRITY");
    X(IMAGE_DLLCHARACTERISTICS_NX_COMPAT,             "NX_COMPAT");
    X(IMAGE_DLLCHARACTERISTICS_NO_ISOLATION,          "NO_ISOLATION");
    X(IMAGE_DLLCHARACTERISTICS_NO_SEH,                "NO_SEH");
    X(IMAGE_DLLCHARACTERISTICS_NO_BIND,               "NO_BIND");
    X(IMAGE_DLLCHARACTERISTICS_WDM_DRIVER,            "WDM_DRIVER");
    X(IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE, "TERMINAL_SERVER_AWARE");
#undef X
}

static inline void print_datadirectory(DWORD n, const IMAGE_DATA_DIRECTORY *directory)
{
    unsigned i;
    printf("Data Directory\n");

    for (i = 0; i < n && i < 16; i++)
    {
        printf("  %-12s rva: 0x%-8x  size: 0x%-8x\n",
               DirectoryNames[i], directory[i].VirtualAddress,
               directory[i].Size);
    }
}

static void dump_optional_header32(const IMAGE_OPTIONAL_HEADER32 *image_oh, UINT header_size)
{
    IMAGE_OPTIONAL_HEADER32 oh;
    const IMAGE_OPTIONAL_HEADER32 *optionalHeader;

    /* in case optional header is missing or partial */
    memset(&oh, 0, sizeof(oh));
    memcpy(&oh, image_oh, min(header_size, sizeof(oh)));
    optionalHeader = &oh;

    print_word("Magic", optionalHeader->Magic);
    print_ver("linker version",
              optionalHeader->MajorLinkerVersion, optionalHeader->MinorLinkerVersion);
    print_dword("size of code", optionalHeader->SizeOfCode);
    print_dword("size of initialized data", optionalHeader->SizeOfInitializedData);
    print_dword("size of uninitialized data", optionalHeader->SizeOfUninitializedData);
    print_dword("entrypoint RVA", optionalHeader->AddressOfEntryPoint);
    print_dword("base of code", optionalHeader->BaseOfCode);
    print_dword("base of data", optionalHeader->BaseOfData);
    print_dword("image base", optionalHeader->ImageBase);
    print_dword("section align", optionalHeader->SectionAlignment);
    print_dword("file align", optionalHeader->FileAlignment);
    print_ver("required OS version",
              optionalHeader->MajorOperatingSystemVersion, optionalHeader->MinorOperatingSystemVersion);
    print_ver("image version",
              optionalHeader->MajorImageVersion, optionalHeader->MinorImageVersion);
    print_ver("subsystem version",
              optionalHeader->MajorSubsystemVersion, optionalHeader->MinorSubsystemVersion);
    print_dword("Win32 Version", optionalHeader->Win32VersionValue);
    print_dword("size of image", optionalHeader->SizeOfImage);
    print_dword("size of headers", optionalHeader->SizeOfHeaders);
    print_dword("checksum", optionalHeader->CheckSum);
    print_subsys("Subsystem", optionalHeader->Subsystem);
    print_dllflags("DLL characteristics:", optionalHeader->DllCharacteristics);
    print_dword("stack reserve size", optionalHeader->SizeOfStackReserve);
    print_dword("stack commit size", optionalHeader->SizeOfStackCommit);
    print_dword("heap reserve size", optionalHeader->SizeOfHeapReserve);
    print_dword("heap commit size", optionalHeader->SizeOfHeapCommit);
    print_dword("loader flags", optionalHeader->LoaderFlags);
    print_dword("RVAs & sizes", optionalHeader->NumberOfRvaAndSizes);
    printf("\n");
    print_datadirectory(optionalHeader->NumberOfRvaAndSizes, optionalHeader->DataDirectory);
    printf("\n");
}

static void dump_optional_header64(const IMAGE_OPTIONAL_HEADER64 *image_oh, UINT header_size)
{
    IMAGE_OPTIONAL_HEADER64 oh;
    const IMAGE_OPTIONAL_HEADER64 *optionalHeader;

    /* in case optional header is missing or partial */
    memset(&oh, 0, sizeof(oh));
    memcpy(&oh, image_oh, min(header_size, sizeof(oh)));
    optionalHeader = &oh;

    print_word("Magic", optionalHeader->Magic);
    print_ver("linker version",
              optionalHeader->MajorLinkerVersion, optionalHeader->MinorLinkerVersion);
    print_dword("size of code", optionalHeader->SizeOfCode);
    print_dword("size of initialized data", optionalHeader->SizeOfInitializedData);
    print_dword("size of uninitialized data", optionalHeader->SizeOfUninitializedData);
    print_dword("entrypoint RVA", optionalHeader->AddressOfEntryPoint);
    print_dword("base of code", optionalHeader->BaseOfCode);
    print_longlong("image base", optionalHeader->ImageBase);
    print_dword("section align", optionalHeader->SectionAlignment);
    print_dword("file align", optionalHeader->FileAlignment);
    print_ver("required OS version",
              optionalHeader->MajorOperatingSystemVersion, optionalHeader->MinorOperatingSystemVersion);
    print_ver("image version",
              optionalHeader->MajorImageVersion, optionalHeader->MinorImageVersion);
    print_ver("subsystem version",
              optionalHeader->MajorSubsystemVersion, optionalHeader->MinorSubsystemVersion);
    print_dword("Win32 Version", optionalHeader->Win32VersionValue);
    print_dword("size of image", optionalHeader->SizeOfImage);
    print_dword("size of headers", optionalHeader->SizeOfHeaders);
    print_dword("checksum", optionalHeader->CheckSum);
    print_subsys("Subsystem", optionalHeader->Subsystem);
    print_dllflags("DLL characteristics:", optionalHeader->DllCharacteristics);
    print_longlong("stack reserve size", optionalHeader->SizeOfStackReserve);
    print_longlong("stack commit size", optionalHeader->SizeOfStackCommit);
    print_longlong("heap reserve size", optionalHeader->SizeOfHeapReserve);
    print_longlong("heap commit size", optionalHeader->SizeOfHeapCommit);
    print_dword("loader flags", optionalHeader->LoaderFlags);
    print_dword("RVAs & sizes", optionalHeader->NumberOfRvaAndSizes);
    printf("\n");
    print_datadirectory(optionalHeader->NumberOfRvaAndSizes, optionalHeader->DataDirectory);
    printf("\n");
}

void dump_optional_header(const IMAGE_OPTIONAL_HEADER32 *optionalHeader, UINT header_size)
{
    printf("Optional Header (%s)\n", get_magic_type(optionalHeader->Magic));

    switch(optionalHeader->Magic) {
        case IMAGE_NT_OPTIONAL_HDR32_MAGIC:
            dump_optional_header32(optionalHeader, header_size);
            break;
        case IMAGE_NT_OPTIONAL_HDR64_MAGIC:
            dump_optional_header64((const IMAGE_OPTIONAL_HEADER64 *)optionalHeader, header_size);
            break;
        default:
            printf("  Unknown optional header magic: 0x%-4X\n", optionalHeader->Magic);
            break;
    }
}

void dump_file_header(const IMAGE_FILE_HEADER *fileHeader)
{
    printf("File Header\n");

    printf("  Machine:                      %04X (%s)\n",
	   fileHeader->Machine, get_machine_str(fileHeader->Machine));
    printf("  Number of Sections:           %d\n", fileHeader->NumberOfSections);
    printf("  TimeDateStamp:                %08X (%s) offset %lu\n",
	   fileHeader->TimeDateStamp, get_time_str(fileHeader->TimeDateStamp),
	   Offset(&(fileHeader->TimeDateStamp)));
    printf("  PointerToSymbolTable:         %08X\n", fileHeader->PointerToSymbolTable);
    printf("  NumberOfSymbols:              %08X\n", fileHeader->NumberOfSymbols);
    printf("  SizeOfOptionalHeader:         %04X\n", fileHeader->SizeOfOptionalHeader);
    printf("  Characteristics:              %04X\n", fileHeader->Characteristics);
#define	X(f,s)	if (fileHeader->Characteristics & f) printf("    %s\n", s)
    X(IMAGE_FILE_RELOCS_STRIPPED, 	"RELOCS_STRIPPED");
    X(IMAGE_FILE_EXECUTABLE_IMAGE, 	"EXECUTABLE_IMAGE");
    X(IMAGE_FILE_LINE_NUMS_STRIPPED, 	"LINE_NUMS_STRIPPED");
    X(IMAGE_FILE_LOCAL_SYMS_STRIPPED, 	"LOCAL_SYMS_STRIPPED");
    X(IMAGE_FILE_AGGRESIVE_WS_TRIM, 	"AGGRESIVE_WS_TRIM");
    X(IMAGE_FILE_LARGE_ADDRESS_AWARE, 	"LARGE_ADDRESS_AWARE");
    X(IMAGE_FILE_16BIT_MACHINE, 	"16BIT_MACHINE");
    X(IMAGE_FILE_BYTES_REVERSED_LO, 	"BYTES_REVERSED_LO");
    X(IMAGE_FILE_32BIT_MACHINE, 	"32BIT_MACHINE");
    X(IMAGE_FILE_DEBUG_STRIPPED, 	"DEBUG_STRIPPED");
    X(IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP, 	"REMOVABLE_RUN_FROM_SWAP");
    X(IMAGE_FILE_NET_RUN_FROM_SWAP, 	"NET_RUN_FROM_SWAP");
    X(IMAGE_FILE_SYSTEM, 		"SYSTEM");
    X(IMAGE_FILE_DLL, 			"DLL");
    X(IMAGE_FILE_UP_SYSTEM_ONLY, 	"UP_SYSTEM_ONLY");
    X(IMAGE_FILE_BYTES_REVERSED_HI, 	"BYTES_REVERSED_HI");
#undef X
    printf("\n");
}

static	void	dump_pe_header(void)
{
    dump_file_header(&PE_nt_headers->FileHeader);
    dump_optional_header((const IMAGE_OPTIONAL_HEADER32*)&PE_nt_headers->OptionalHeader, PE_nt_headers->FileHeader.SizeOfOptionalHeader);
}

void dump_section(const IMAGE_SECTION_HEADER *sectHead, const char* strtable)
{
        unsigned offset;

        /* long section name ? */
        if (strtable && sectHead->Name[0] == '/' &&
            ((offset = atoi((const char*)sectHead->Name + 1)) < *(DWORD*)strtable))
            printf("  %.8s (%s)", sectHead->Name, strtable + offset);
        else
	    printf("  %-8.8s", sectHead->Name);
	printf("   VirtSize: 0x%08x  VirtAddr:  0x%08x\n",
               sectHead->Misc.VirtualSize, sectHead->VirtualAddress);
	printf("    raw data offs:   0x%08x  raw data size: 0x%08x\n",
	       sectHead->PointerToRawData, sectHead->SizeOfRawData);
	printf("    relocation offs: 0x%08x  relocations:   0x%08x\n",
	       sectHead->PointerToRelocations, sectHead->NumberOfRelocations);
	printf("    line # offs:     %-8u  line #'s:      %-8u\n",
	       sectHead->PointerToLinenumbers, sectHead->NumberOfLinenumbers);
	printf("    characteristics: 0x%08x\n", sectHead->Characteristics);
	printf("    ");
#define X(b,s)	if (sectHead->Characteristics & b) printf("  " s)
/* #define IMAGE_SCN_TYPE_REG			0x00000000 - Reserved */
/* #define IMAGE_SCN_TYPE_DSECT			0x00000001 - Reserved */
/* #define IMAGE_SCN_TYPE_NOLOAD		0x00000002 - Reserved */
/* #define IMAGE_SCN_TYPE_GROUP			0x00000004 - Reserved */
/* #define IMAGE_SCN_TYPE_NO_PAD		0x00000008 - Reserved */
/* #define IMAGE_SCN_TYPE_COPY			0x00000010 - Reserved */

	X(IMAGE_SCN_CNT_CODE, 			"CODE");
	X(IMAGE_SCN_CNT_INITIALIZED_DATA, 	"INITIALIZED_DATA");
	X(IMAGE_SCN_CNT_UNINITIALIZED_DATA, 	"UNINITIALIZED_DATA");

	X(IMAGE_SCN_LNK_OTHER, 			"LNK_OTHER");
	X(IMAGE_SCN_LNK_INFO, 			"LNK_INFO");
/* #define	IMAGE_SCN_TYPE_OVER		0x00000400 - Reserved */
	X(IMAGE_SCN_LNK_REMOVE, 		"LNK_REMOVE");
	X(IMAGE_SCN_LNK_COMDAT, 		"LNK_COMDAT");

/* 						0x00002000 - Reserved */
/* #define IMAGE_SCN_MEM_PROTECTED 		0x00004000 - Obsolete */
	X(IMAGE_SCN_MEM_FARDATA, 		"MEM_FARDATA");

/* #define IMAGE_SCN_MEM_SYSHEAP		0x00010000 - Obsolete */
	X(IMAGE_SCN_MEM_PURGEABLE, 		"MEM_PURGEABLE");
	X(IMAGE_SCN_MEM_16BIT, 			"MEM_16BIT");
	X(IMAGE_SCN_MEM_LOCKED, 		"MEM_LOCKED");
	X(IMAGE_SCN_MEM_PRELOAD, 		"MEM_PRELOAD");

        switch (sectHead->Characteristics & IMAGE_SCN_ALIGN_MASK)
        {
#define X2(b,s)	case b: printf("  " s); break
        X2(IMAGE_SCN_ALIGN_1BYTES, 		"ALIGN_1BYTES");
        X2(IMAGE_SCN_ALIGN_2BYTES, 		"ALIGN_2BYTES");
        X2(IMAGE_SCN_ALIGN_4BYTES, 		"ALIGN_4BYTES");
        X2(IMAGE_SCN_ALIGN_8BYTES, 		"ALIGN_8BYTES");
        X2(IMAGE_SCN_ALIGN_16BYTES, 		"ALIGN_16BYTES");
        X2(IMAGE_SCN_ALIGN_32BYTES, 		"ALIGN_32BYTES");
        X2(IMAGE_SCN_ALIGN_64BYTES, 		"ALIGN_64BYTES");
        X2(IMAGE_SCN_ALIGN_128BYTES, 		"ALIGN_128BYTES");
        X2(IMAGE_SCN_ALIGN_256BYTES, 		"ALIGN_256BYTES");
        X2(IMAGE_SCN_ALIGN_512BYTES, 		"ALIGN_512BYTES");
        X2(IMAGE_SCN_ALIGN_1024BYTES, 		"ALIGN_1024BYTES");
        X2(IMAGE_SCN_ALIGN_2048BYTES, 		"ALIGN_2048BYTES");
        X2(IMAGE_SCN_ALIGN_4096BYTES, 		"ALIGN_4096BYTES");
        X2(IMAGE_SCN_ALIGN_8192BYTES, 		"ALIGN_8192BYTES");
#undef X2
        }

	X(IMAGE_SCN_LNK_NRELOC_OVFL, 		"LNK_NRELOC_OVFL");

	X(IMAGE_SCN_MEM_DISCARDABLE, 		"MEM_DISCARDABLE");
	X(IMAGE_SCN_MEM_NOT_CACHED, 		"MEM_NOT_CACHED");
	X(IMAGE_SCN_MEM_NOT_PAGED, 		"MEM_NOT_PAGED");
	X(IMAGE_SCN_MEM_SHARED, 		"MEM_SHARED");
	X(IMAGE_SCN_MEM_EXECUTE, 		"MEM_EXECUTE");
	X(IMAGE_SCN_MEM_READ, 			"MEM_READ");
	X(IMAGE_SCN_MEM_WRITE, 			"MEM_WRITE");
#undef X
	printf("\n\n");
}

static void dump_sections(const void *base, const void* addr, unsigned num_sect)
{
    const IMAGE_SECTION_HEADER*	sectHead = addr;
    unsigned			i;
    const char*                 strtable;

    if (PE_nt_headers->FileHeader.PointerToSymbolTable && PE_nt_headers->FileHeader.NumberOfSymbols)
    {
        strtable = (const char*)base +
            PE_nt_headers->FileHeader.PointerToSymbolTable +
            PE_nt_headers->FileHeader.NumberOfSymbols * sizeof(IMAGE_SYMBOL);
    }
    else strtable = NULL;

    printf("Section Table\n");
    for (i = 0; i < num_sect; i++, sectHead++)
    {
        dump_section(sectHead, strtable);

        if (globals.do_dump_rawdata)
        {
            dump_data((const unsigned char *)base + sectHead->PointerToRawData, sectHead->SizeOfRawData, "    " );
            printf("\n");
        }
    }
}

static	void	dump_dir_exported_functions(void)
{
    unsigned int size = 0;
    const IMAGE_EXPORT_DIRECTORY*exportDir = get_dir_and_size(IMAGE_FILE_EXPORT_DIRECTORY, &size);
    unsigned int		i;
    const DWORD*		pFunc;
    const DWORD*		pName;
    const WORD* 		pOrdl;
    DWORD*		        funcs;

    if (!exportDir) return;

    printf("Exports table:\n");
    printf("\n");
    printf("  Name:            %s\n", (const char*)RVA(exportDir->Name, sizeof(DWORD)));
    printf("  Characteristics: %08x\n", exportDir->Characteristics);
    printf("  TimeDateStamp:   %08X %s\n",
	   exportDir->TimeDateStamp, get_time_str(exportDir->TimeDateStamp));
    printf("  Version:         %u.%02u\n", exportDir->MajorVersion, exportDir->MinorVersion);
    printf("  Ordinal base:    %u\n", exportDir->Base);
    printf("  # of functions:  %u\n", exportDir->NumberOfFunctions);
    printf("  # of Names:      %u\n", exportDir->NumberOfNames);
    printf("Addresses of functions: %08X\n", exportDir->AddressOfFunctions);
    printf("Addresses of name ordinals: %08X\n", exportDir->AddressOfNameOrdinals);
    printf("Addresses of names: %08X\n", exportDir->AddressOfNames);
    printf("\n");
    printf("  Entry Pt  Ordn  Name\n");

    pFunc = RVA(exportDir->AddressOfFunctions, exportDir->NumberOfFunctions * sizeof(DWORD));
    if (!pFunc) {printf("Can't grab functions' address table\n"); return;}
    pName = RVA(exportDir->AddressOfNames, exportDir->NumberOfNames * sizeof(DWORD));
    pOrdl = RVA(exportDir->AddressOfNameOrdinals, exportDir->NumberOfNames * sizeof(WORD));

    funcs = calloc( exportDir->NumberOfFunctions, sizeof(*funcs) );
    if (!funcs) fatal("no memory");

    for (i = 0; i < exportDir->NumberOfNames; i++) funcs[pOrdl[i]] = pName[i];

    for (i = 0; i < exportDir->NumberOfFunctions; i++)
    {
        if (!pFunc[i]) continue;
        printf("  %08X %5u ", pFunc[i], exportDir->Base + i);
        if (funcs[i])
            printf("%s", get_symbol_str((const char*)RVA(funcs[i], sizeof(DWORD))));
        else
            printf("<by ordinal>");

        /* check for forwarded function */
        if ((const char *)RVA(pFunc[i],1) >= (const char *)exportDir &&
            (const char *)RVA(pFunc[i],1) < (const char *)exportDir + size)
            printf(" (-> %s)", (const char *)RVA(pFunc[i],1));
        printf("\n");
    }
    free(funcs);
    printf("\n");
}


struct runtime_function
{
    DWORD BeginAddress;
    DWORD EndAddress;
    DWORD UnwindData;
};

union handler_data
{
    struct runtime_function chain;
    DWORD handler;
};

struct opcode
{
    BYTE offset;
    BYTE code : 4;
    BYTE info : 4;
};

struct unwind_info
{
    BYTE version : 3;
    BYTE flags : 5;
    BYTE prolog;
    BYTE count;
    BYTE frame_reg : 4;
    BYTE frame_offset : 4;
    struct opcode opcodes[1];  /* count entries */
    /* followed by union handler_data */
};

#define UWOP_PUSH_NONVOL     0
#define UWOP_ALLOC_LARGE     1
#define UWOP_ALLOC_SMALL     2
#define UWOP_SET_FPREG       3
#define UWOP_SAVE_NONVOL     4
#define UWOP_SAVE_NONVOL_FAR 5
#define UWOP_SAVE_XMM128     8
#define UWOP_SAVE_XMM128_FAR 9
#define UWOP_PUSH_MACHFRAME  10

#define UNW_FLAG_EHANDLER  1
#define UNW_FLAG_UHANDLER  2
#define UNW_FLAG_CHAININFO 4

static void dump_x86_64_unwind_info( const struct runtime_function *function )
{
    static const char * const reg_names[16] =
        { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
          "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15" };

    union handler_data *handler_data;
    const struct unwind_info *info;
    unsigned int i, count;

    printf( "\nFunction %08x-%08x:\n", function->BeginAddress, function->EndAddress );
    if (function->UnwindData & 1)
    {
        const struct runtime_function *next = RVA( function->UnwindData & ~1, sizeof(*next) );
        printf( "  -> function %08x-%08x\n", next->BeginAddress, next->EndAddress );
        return;
    }
    info = RVA( function->UnwindData, sizeof(*info) );

    printf( "  unwind info at %08x\n", function->UnwindData );
    if (info->version != 1)
    {
        printf( "    *** unknown version %u\n", info->version );
        return;
    }
    printf( "    flags %x", info->flags );
    if (info->flags & UNW_FLAG_EHANDLER) printf( " EHANDLER" );
    if (info->flags & UNW_FLAG_UHANDLER) printf( " UHANDLER" );
    if (info->flags & UNW_FLAG_CHAININFO) printf( " CHAININFO" );
    printf( "\n    prolog 0x%x bytes\n", info->prolog );

    if (info->frame_reg)
        printf( "    frame register %s offset 0x%x(%%rsp)\n",
                reg_names[info->frame_reg], info->frame_offset * 16 );

    for (i = 0; i < info->count; i++)
    {
        printf( "      0x%02x: ", info->opcodes[i].offset );
        switch (info->opcodes[i].code)
        {
        case UWOP_PUSH_NONVOL:
            printf( "push %%%s\n", reg_names[info->opcodes[i].info] );
            break;
        case UWOP_ALLOC_LARGE:
            if (info->opcodes[i].info)
            {
                count = *(DWORD *)&info->opcodes[i+1];
                i += 2;
            }
            else
            {
                count = *(USHORT *)&info->opcodes[i+1] * 8;
                i++;
            }
            printf( "sub $0x%x,%%rsp\n", count );
            break;
        case UWOP_ALLOC_SMALL:
            count = (info->opcodes[i].info + 1) * 8;
            printf( "sub $0x%x,%%rsp\n", count );
            break;
        case UWOP_SET_FPREG:
            printf( "lea 0x%x(%%rsp),%s\n",
                    info->frame_offset * 16, reg_names[info->frame_reg] );
            break;
        case UWOP_SAVE_NONVOL:
            count = *(USHORT *)&info->opcodes[i+1] * 8;
            printf( "mov %%%s,0x%x(%%rsp)\n", reg_names[info->opcodes[i].info], count );
            i++;
            break;
        case UWOP_SAVE_NONVOL_FAR:
            count = *(DWORD *)&info->opcodes[i+1];
            printf( "mov %%%s,0x%x(%%rsp)\n", reg_names[info->opcodes[i].info], count );
            i += 2;
            break;
        case UWOP_SAVE_XMM128:
            count = *(USHORT *)&info->opcodes[i+1] * 16;
            printf( "movaps %%xmm%u,0x%x(%%rsp)\n", info->opcodes[i].info, count );
            i++;
            break;
        case UWOP_SAVE_XMM128_FAR:
            count = *(DWORD *)&info->opcodes[i+1];
            printf( "movaps %%xmm%u,0x%x(%%rsp)\n", info->opcodes[i].info, count );
            i += 2;
            break;
        case UWOP_PUSH_MACHFRAME:
            printf( "PUSH_MACHFRAME %u\n", info->opcodes[i].info );
            break;
        default:
            printf( "*** unknown code %u\n", info->opcodes[i].code );
            break;
        }
    }

    handler_data = (union handler_data *)&info->opcodes[(info->count + 1) & ~1];
    if (info->flags & UNW_FLAG_CHAININFO)
    {
        printf( "    -> function %08x-%08x\n",
                handler_data->chain.BeginAddress, handler_data->chain.EndAddress );
        return;
    }
    if (info->flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER))
        printf( "    handler %08x data at %08x\n", handler_data->handler,
                (ULONG)(function->UnwindData + (char *)(&handler_data->handler + 1) - (char *)info ));
}

static void dump_dir_exceptions(void)
{
    unsigned int i, size = 0;
    const struct runtime_function *funcs = get_dir_and_size(IMAGE_FILE_EXCEPTION_DIRECTORY, &size);
    const IMAGE_FILE_HEADER *file_header = &PE_nt_headers->FileHeader;

    if (!funcs) return;

    if (file_header->Machine == IMAGE_FILE_MACHINE_AMD64)
    {
        size /= sizeof(*funcs);
        printf( "Exception info (%u functions):\n", size );
        for (i = 0; i < size; i++) dump_x86_64_unwind_info( funcs + i );
    }
    else printf( "Exception information not supported for %s binaries\n",
                 get_machine_str(file_header->Machine));
}


static void dump_image_thunk_data64(const IMAGE_THUNK_DATA64 *il)
{
    /* FIXME: This does not properly handle large images */
    const IMAGE_IMPORT_BY_NAME* iibn;
    for (; il->u1.Ordinal; il++)
    {
        if (IMAGE_SNAP_BY_ORDINAL64(il->u1.Ordinal))
            printf("  %4u  <by ordinal>\n", (DWORD)IMAGE_ORDINAL64(il->u1.Ordinal));
        else
        {
            iibn = RVA((DWORD)il->u1.AddressOfData, sizeof(DWORD));
            if (!iibn)
                printf("Can't grab import by name info, skipping to next ordinal\n");
            else
                printf("  %4u  %s %x\n", iibn->Hint, iibn->Name, (DWORD)il->u1.AddressOfData);
        }
    }
}

static void dump_image_thunk_data32(const IMAGE_THUNK_DATA32 *il, int offset)
{
    const IMAGE_IMPORT_BY_NAME* iibn;
    for (; il->u1.Ordinal; il++)
    {
        if (IMAGE_SNAP_BY_ORDINAL32(il->u1.Ordinal))
            printf("  %4u  <by ordinal>\n", IMAGE_ORDINAL32(il->u1.Ordinal));
        else
        {
            iibn = RVA((DWORD)il->u1.AddressOfData - offset, sizeof(DWORD));
            if (!iibn)
                printf("Can't grab import by name info, skipping to next ordinal\n");
            else
                printf("  %4u  %s %x\n", iibn->Hint, iibn->Name, (DWORD)il->u1.AddressOfData);
        }
    }
}

static	void	dump_dir_imported_functions(void)
{
    const IMAGE_IMPORT_DESCRIPTOR	*importDesc = get_dir(IMAGE_FILE_IMPORT_DIRECTORY);
    DWORD directorySize;

    if (!importDesc)	return;
    if(PE_nt_headers->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        const IMAGE_OPTIONAL_HEADER64 *opt = (const IMAGE_OPTIONAL_HEADER64*)&PE_nt_headers->OptionalHeader;
        directorySize = opt->DataDirectory[IMAGE_FILE_IMPORT_DIRECTORY].Size;
    }
    else
    {
        const IMAGE_OPTIONAL_HEADER32 *opt = (const IMAGE_OPTIONAL_HEADER32*)&PE_nt_headers->OptionalHeader;
        directorySize = opt->DataDirectory[IMAGE_FILE_IMPORT_DIRECTORY].Size;
    }

    printf("Import Table size: %08x\n", directorySize);/* FIXME */

    for (;;)
    {
	const IMAGE_THUNK_DATA32*	il;

        if (!importDesc->Name || !importDesc->FirstThunk) break;

	printf("  offset %08lx %s\n", Offset(importDesc), (const char*)RVA(importDesc->Name, sizeof(DWORD)));
	printf("  Hint/Name Table: %08X\n", (DWORD)importDesc->u.OriginalFirstThunk);
	printf("  TimeDateStamp:   %08X (%s)\n",
	       importDesc->TimeDateStamp, get_time_str(importDesc->TimeDateStamp));
	printf("  ForwarderChain:  %08X\n", importDesc->ForwarderChain);
	printf("  First thunk RVA: %08X\n", (DWORD)importDesc->FirstThunk);

	printf("  Ordn  Name\n");

	il = (importDesc->u.OriginalFirstThunk != 0) ?
	    RVA((DWORD)importDesc->u.OriginalFirstThunk, sizeof(DWORD)) :
	    RVA((DWORD)importDesc->FirstThunk, sizeof(DWORD));

	if (!il)
            printf("Can't grab thunk data, going to next imported DLL\n");
        else
        {
            if(PE_nt_headers->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                dump_image_thunk_data64((const IMAGE_THUNK_DATA64*)il);
            else
                dump_image_thunk_data32(il, 0);
            printf("\n");
        }
	importDesc++;
    }
    printf("\n");
}

static void dump_dir_delay_imported_functions(void)
{
    const struct ImgDelayDescr
    {
        DWORD grAttrs;
        DWORD szName;
        DWORD phmod;
        DWORD pIAT;
        DWORD pINT;
        DWORD pBoundIAT;
        DWORD pUnloadIAT;
        DWORD dwTimeStamp;
    } *importDesc = get_dir(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
    DWORD directorySize;

    if (!importDesc) return;
    if (PE_nt_headers->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        const IMAGE_OPTIONAL_HEADER64 *opt = (const IMAGE_OPTIONAL_HEADER64 *)&PE_nt_headers->OptionalHeader;
        directorySize = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size;
    }
    else
    {
        const IMAGE_OPTIONAL_HEADER32 *opt = (const IMAGE_OPTIONAL_HEADER32 *)&PE_nt_headers->OptionalHeader;
        directorySize = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size;
    }

    printf("Delay Import Table size: %08x\n", directorySize); /* FIXME */

    for (;;)
    {
        const IMAGE_THUNK_DATA32*       il;
        int                             offset = (importDesc->grAttrs & 1) ? 0 : PE_nt_headers->OptionalHeader.ImageBase;

        if (!importDesc->szName || !importDesc->pIAT || !importDesc->pINT) break;

        printf("  grAttrs %08x offset %08lx %s\n", importDesc->grAttrs, Offset(importDesc),
               (const char *)RVA(importDesc->szName - offset, sizeof(DWORD)));
        printf("  Hint/Name Table: %08x\n", importDesc->pINT);
        printf("  TimeDateStamp:   %08X (%s)\n",
               importDesc->dwTimeStamp, get_time_str(importDesc->dwTimeStamp));

        printf("  Ordn  Name\n");

        il = RVA(importDesc->pINT - offset, sizeof(DWORD));

        if (!il)
            printf("Can't grab thunk data, going to next imported DLL\n");
        else
        {
            if (PE_nt_headers->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                dump_image_thunk_data64((const IMAGE_THUNK_DATA64 *)il);
            else
                dump_image_thunk_data32(il, offset);
            printf("\n");
        }
        importDesc++;
    }
    printf("\n");
}

static	void	dump_dir_debug_dir(const IMAGE_DEBUG_DIRECTORY* idd, int idx)
{
    const	char*	str;

    printf("Directory %02u\n", idx + 1);
    printf("  Characteristics:   %08X\n", idd->Characteristics);
    printf("  TimeDateStamp:     %08X %s\n",
	   idd->TimeDateStamp, get_time_str(idd->TimeDateStamp));
    printf("  Version            %u.%02u\n", idd->MajorVersion, idd->MinorVersion);
    switch (idd->Type)
    {
    default:
    case IMAGE_DEBUG_TYPE_UNKNOWN:	str = "UNKNOWN"; 	break;
    case IMAGE_DEBUG_TYPE_COFF:		str = "COFF"; 		break;
    case IMAGE_DEBUG_TYPE_CODEVIEW:	str = "CODEVIEW"; 	break;
    case IMAGE_DEBUG_TYPE_FPO:		str = "FPO"; 		break;
    case IMAGE_DEBUG_TYPE_MISC:		str = "MISC"; 		break;
    case IMAGE_DEBUG_TYPE_EXCEPTION:	str = "EXCEPTION"; 	break;
    case IMAGE_DEBUG_TYPE_FIXUP:	str = "FIXUP"; 		break;
    case IMAGE_DEBUG_TYPE_OMAP_TO_SRC:	str = "OMAP_TO_SRC"; 	break;
    case IMAGE_DEBUG_TYPE_OMAP_FROM_SRC:str = "OMAP_FROM_SRC"; 	break;
    case IMAGE_DEBUG_TYPE_BORLAND:	str = "BORLAND"; 	break;
    case IMAGE_DEBUG_TYPE_RESERVED10:	str = "RESERVED10"; 	break;
    }
    printf("  Type:              %u (%s)\n", idd->Type, str);
    printf("  SizeOfData:        %u\n", idd->SizeOfData);
    printf("  AddressOfRawData:  %08X\n", idd->AddressOfRawData);
    printf("  PointerToRawData:  %08X\n", idd->PointerToRawData);

    switch (idd->Type)
    {
    case IMAGE_DEBUG_TYPE_UNKNOWN:
	break;
    case IMAGE_DEBUG_TYPE_COFF:
	dump_coff(idd->PointerToRawData, idd->SizeOfData, 
                  (const char*)PE_nt_headers + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + PE_nt_headers->FileHeader.SizeOfOptionalHeader);
	break;
    case IMAGE_DEBUG_TYPE_CODEVIEW:
	dump_codeview(idd->PointerToRawData, idd->SizeOfData);
	break;
    case IMAGE_DEBUG_TYPE_FPO:
	dump_frame_pointer_omission(idd->PointerToRawData, idd->SizeOfData);
	break;
    case IMAGE_DEBUG_TYPE_MISC:
    {
	const IMAGE_DEBUG_MISC* misc = PRD(idd->PointerToRawData, idd->SizeOfData);
	if (!misc) {printf("Can't get misc debug information\n"); break;}
	printf("    DataType:          %u (%s)\n",
	       misc->DataType,
	       (misc->DataType == IMAGE_DEBUG_MISC_EXENAME) ? "Exe name" : "Unknown");
	printf("    Length:            %u\n", misc->Length);
	printf("    Unicode:           %s\n", misc->Unicode ? "Yes" : "No");
	printf("    Data:              %s\n", misc->Data);
    }
    break;
    case IMAGE_DEBUG_TYPE_EXCEPTION:
	break;
    case IMAGE_DEBUG_TYPE_FIXUP:
	break;
    case IMAGE_DEBUG_TYPE_OMAP_TO_SRC:
	break;
    case IMAGE_DEBUG_TYPE_OMAP_FROM_SRC:
	break;
    case IMAGE_DEBUG_TYPE_BORLAND:
	break;
    case IMAGE_DEBUG_TYPE_RESERVED10:
	break;
    }
    printf("\n");
}

static void	dump_dir_debug(void)
{
    const IMAGE_DEBUG_DIRECTORY*debugDir = get_dir(IMAGE_FILE_DEBUG_DIRECTORY);
    unsigned			nb_dbg, i;

    if (!debugDir) return;
    nb_dbg = PE_nt_headers->OptionalHeader.DataDirectory[IMAGE_FILE_DEBUG_DIRECTORY].Size /
	sizeof(*debugDir);
    if (!nb_dbg) return;

    printf("Debug Table (%u directories)\n", nb_dbg);

    for (i = 0; i < nb_dbg; i++)
    {
	dump_dir_debug_dir(debugDir, i);
	debugDir++;
    }
    printf("\n");
}

static inline void print_clrflags(const char *title, WORD value)
{
    printf("  %-34s 0x%X\n", title, value);
#define X(f,s) if (value & f) printf("    %s\n", s)
    X(COMIMAGE_FLAGS_ILONLY,           "ILONLY");
    X(COMIMAGE_FLAGS_32BITREQUIRED,    "32BITREQUIRED");
    X(COMIMAGE_FLAGS_IL_LIBRARY,       "IL_LIBRARY");
    X(COMIMAGE_FLAGS_STRONGNAMESIGNED, "STRONGNAMESIGNED");
    X(COMIMAGE_FLAGS_TRACKDEBUGDATA,   "TRACKDEBUGDATA");
#undef X
}

static inline void print_clrdirectory(const char *title, const IMAGE_DATA_DIRECTORY *dir)
{
    printf("  %-23s rva: 0x%-8x  size: 0x%-8x\n", title, dir->VirtualAddress, dir->Size);
}

static void dump_dir_clr_header(void)
{
    unsigned int size = 0;
    const IMAGE_COR20_HEADER *dir = get_dir_and_size(IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR, &size);

    if (!dir) return;

    printf( "CLR Header\n" );
    print_dword( "Header Size", dir->cb );
    print_ver( "Required runtime version", dir->MajorRuntimeVersion, dir->MinorRuntimeVersion );
    print_clrflags( "Flags", dir->Flags );
    print_dword( "EntryPointToken", dir->EntryPointToken );
    printf("\n");
    printf( "CLR Data Directory\n" );
    print_clrdirectory( "MetaData", &dir->MetaData );
    print_clrdirectory( "Resources", &dir->Resources );
    print_clrdirectory( "StrongNameSignature", &dir->StrongNameSignature );
    print_clrdirectory( "CodeManagerTable", &dir->CodeManagerTable );
    print_clrdirectory( "VTableFixups", &dir->VTableFixups );
    print_clrdirectory( "ExportAddressTableJumps", &dir->ExportAddressTableJumps );
    print_clrdirectory( "ManagedNativeHeader", &dir->ManagedNativeHeader );
    printf("\n");
}

static void dump_dir_reloc(void)
{
    unsigned int i, size = 0;
    const USHORT *relocs;
    const IMAGE_BASE_RELOCATION *rel = get_dir_and_size(IMAGE_DIRECTORY_ENTRY_BASERELOC, &size);
    const IMAGE_BASE_RELOCATION *end = (IMAGE_BASE_RELOCATION *)((char *)rel + size);
    static const char * const names[] =
    {
        "BASED_ABSOLUTE",
        "BASED_HIGH",
        "BASED_LOW",
        "BASED_HIGHLOW",
        "BASED_HIGHADJ",
        "BASED_MIPS_JMPADDR",
        "BASED_SECTION",
        "BASED_REL",
        "unknown 8",
        "BASED_IA64_IMM64",
        "BASED_DIR64",
        "BASED_HIGH3ADJ",
        "unknown 12",
        "unknown 13",
        "unknown 14",
        "unknown 15"
    };

    if (!rel) return;

    printf( "Relocations\n" );
    while (rel < end - 1 && rel->SizeOfBlock)
    {
        printf( "  Page %x\n", rel->VirtualAddress );
        relocs = (const USHORT *)(rel + 1);
        i = (rel->SizeOfBlock - sizeof(*rel)) / sizeof(USHORT);
        while (i--)
        {
            USHORT offset = *relocs & 0xfff;
            int type = *relocs >> 12;
            printf( "    off %04x type %s\n", offset, names[type] );
            relocs++;
        }
        rel = (const IMAGE_BASE_RELOCATION *)relocs;
    }
    printf("\n");
}

static void dump_dir_tls(void)
{
    IMAGE_TLS_DIRECTORY64 dir;
    const DWORD *callbacks;
    const IMAGE_TLS_DIRECTORY32 *pdir = get_dir(IMAGE_FILE_THREAD_LOCAL_STORAGE);

    if (!pdir) return;

    if(PE_nt_headers->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        memcpy(&dir, pdir, sizeof(dir));
    else
    {
        dir.StartAddressOfRawData = pdir->StartAddressOfRawData;
        dir.EndAddressOfRawData = pdir->EndAddressOfRawData;
        dir.AddressOfIndex = pdir->AddressOfIndex;
        dir.AddressOfCallBacks = pdir->AddressOfCallBacks;
        dir.SizeOfZeroFill = pdir->SizeOfZeroFill;
        dir.Characteristics = pdir->Characteristics;
    }

    /* FIXME: This does not properly handle large images */
    printf( "Thread Local Storage\n" );
    printf( "  Raw data        %08x-%08x (data size %x zero fill size %x)\n",
            (DWORD)dir.StartAddressOfRawData, (DWORD)dir.EndAddressOfRawData,
            (DWORD)(dir.EndAddressOfRawData - dir.StartAddressOfRawData),
            (DWORD)dir.SizeOfZeroFill );
    printf( "  Index address   %08x\n", (DWORD)dir.AddressOfIndex );
    printf( "  Characteristics %08x\n", dir.Characteristics );
    printf( "  Callbacks       %08x -> {", (DWORD)dir.AddressOfCallBacks );
    if (dir.AddressOfCallBacks)
    {
        DWORD   addr = (DWORD)dir.AddressOfCallBacks - PE_nt_headers->OptionalHeader.ImageBase;
        while ((callbacks = RVA(addr, sizeof(DWORD))) && *callbacks)
        {
            printf( " %08x", *callbacks );
            addr += sizeof(DWORD);
        }
    }
    printf(" }\n\n");
}

enum FileSig get_kind_dbg(void)
{
    const WORD*                pw;

    pw = PRD(0, sizeof(WORD));
    if (!pw) {printf("Can't get main signature, aborting\n"); return 0;}

    if (*pw == 0x4944 /* "DI" */) return SIG_DBG;
    return SIG_UNKNOWN;
}

void	dbg_dump(void)
{
    const IMAGE_SEPARATE_DEBUG_HEADER*  separateDebugHead;
    unsigned			        nb_dbg;
    unsigned			        i;
    const IMAGE_DEBUG_DIRECTORY*	debugDir;

    separateDebugHead = PRD(0, sizeof(*separateDebugHead));
    if (!separateDebugHead) {printf("Can't grab the separate header, aborting\n"); return;}

    printf ("Signature:          %.2s (0x%4X)\n",
	    (const char*)&separateDebugHead->Signature, separateDebugHead->Signature);
    printf ("Flags:              0x%04X\n", separateDebugHead->Flags);
    printf ("Machine:            0x%04X (%s)\n",
	    separateDebugHead->Machine, get_machine_str(separateDebugHead->Machine));
    printf ("Characteristics:    0x%04X\n", separateDebugHead->Characteristics);
    printf ("TimeDateStamp:      0x%08X (%s)\n",
	    separateDebugHead->TimeDateStamp, get_time_str(separateDebugHead->TimeDateStamp));
    printf ("CheckSum:           0x%08X\n", separateDebugHead->CheckSum);
    printf ("ImageBase:          0x%08X\n", separateDebugHead->ImageBase);
    printf ("SizeOfImage:        0x%08X\n", separateDebugHead->SizeOfImage);
    printf ("NumberOfSections:   0x%08X\n", separateDebugHead->NumberOfSections);
    printf ("ExportedNamesSize:  0x%08X\n", separateDebugHead->ExportedNamesSize);
    printf ("DebugDirectorySize: 0x%08X\n", separateDebugHead->DebugDirectorySize);

    if (!PRD(sizeof(IMAGE_SEPARATE_DEBUG_HEADER),
	     separateDebugHead->NumberOfSections * sizeof(IMAGE_SECTION_HEADER)))
    {printf("Can't get the sections, aborting\n"); return;}

    dump_sections(separateDebugHead, separateDebugHead + 1, separateDebugHead->NumberOfSections);

    nb_dbg = separateDebugHead->DebugDirectorySize / sizeof(IMAGE_DEBUG_DIRECTORY);
    debugDir = PRD(sizeof(IMAGE_SEPARATE_DEBUG_HEADER) +
		   separateDebugHead->NumberOfSections * sizeof(IMAGE_SECTION_HEADER) +
		   separateDebugHead->ExportedNamesSize,
		   nb_dbg * sizeof(IMAGE_DEBUG_DIRECTORY));
    if (!debugDir) {printf("Couldn't get the debug directory info, aborting\n");return;}

    printf("Debug Table (%u directories)\n", nb_dbg);

    for (i = 0; i < nb_dbg; i++)
    {
	dump_dir_debug_dir(debugDir, i);
	debugDir++;
    }
}

static const char *get_resource_type( unsigned int id )
{
    static const char * const types[] =
    {
        NULL,
        "CURSOR",
        "BITMAP",
        "ICON",
        "MENU",
        "DIALOG",
        "STRING",
        "FONTDIR",
        "FONT",
        "ACCELERATOR",
        "RCDATA",
        "MESSAGETABLE",
        "GROUP_CURSOR",
        NULL,
        "GROUP_ICON",
        NULL,
        "VERSION",
        "DLGINCLUDE",
        NULL,
        "PLUGPLAY",
        "VXD",
        "ANICURSOR",
        "ANIICON",
        "HTML",
        "RT_MANIFEST"
    };

    if ((size_t)id < sizeof(types)/sizeof(types[0])) return types[id];
    return NULL;
}

/* dump an ASCII string with proper escaping */
static int dump_strA( const unsigned char *str, size_t len )
{
    static const char escapes[32] = ".......abtnvfr.............e....";
    char buffer[256];
    char *pos = buffer;
    int count = 0;

    for (; len; str++, len--)
    {
        if (pos > buffer + sizeof(buffer) - 8)
        {
            fwrite( buffer, pos - buffer, 1, stdout );
            count += pos - buffer;
            pos = buffer;
        }
        if (*str > 127)  /* hex escape */
        {
            pos += sprintf( pos, "\\x%02x", *str );
            continue;
        }
        if (*str < 32)  /* octal or C escape */
        {
            if (!*str && len == 1) continue;  /* do not output terminating NULL */
            if (escapes[*str] != '.')
                pos += sprintf( pos, "\\%c", escapes[*str] );
            else if (len > 1 && str[1] >= '0' && str[1] <= '7')
                pos += sprintf( pos, "\\%03o", *str );
            else
                pos += sprintf( pos, "\\%o", *str );
            continue;
        }
        if (*str == '\\') *pos++ = '\\';
        *pos++ = *str;
    }
    fwrite( buffer, pos - buffer, 1, stdout );
    count += pos - buffer;
    return count;
}

/* dump a Unicode string with proper escaping */
static int dump_strW( const WCHAR *str, size_t len )
{
    static const char escapes[32] = ".......abtnvfr.............e....";
    char buffer[256];
    char *pos = buffer;
    int count = 0;

    for (; len; str++, len--)
    {
        if (pos > buffer + sizeof(buffer) - 8)
        {
            fwrite( buffer, pos - buffer, 1, stdout );
            count += pos - buffer;
            pos = buffer;
        }
        if (*str > 127)  /* hex escape */
        {
            if (len > 1 && str[1] < 128 && isxdigit((char)str[1]))
                pos += sprintf( pos, "\\x%04x", *str );
            else
                pos += sprintf( pos, "\\x%x", *str );
            continue;
        }
        if (*str < 32)  /* octal or C escape */
        {
            if (!*str && len == 1) continue;  /* do not output terminating NULL */
            if (escapes[*str] != '.')
                pos += sprintf( pos, "\\%c", escapes[*str] );
            else if (len > 1 && str[1] >= '0' && str[1] <= '7')
                pos += sprintf( pos, "\\%03o", *str );
            else
                pos += sprintf( pos, "\\%o", *str );
            continue;
        }
        if (*str == '\\') *pos++ = '\\';
        *pos++ = *str;
    }
    fwrite( buffer, pos - buffer, 1, stdout );
    count += pos - buffer;
    return count;
}

/* dump data for a STRING resource */
static void dump_string_data( const WCHAR *ptr, unsigned int size, unsigned int id, const char *prefix )
{
    int i;

    for (i = 0; i < 16 && size; i++)
    {
        unsigned len = *ptr++;

        if (len >= size)
        {
            len = size;
            size = 0;
        }
        else size -= len + 1;

        if (len)
        {
            printf( "%s%04x \"", prefix, (id - 1) * 16 + i );
            dump_strW( ptr, len );
            printf( "\"\n" );
            ptr += len;
        }
    }
}

/* dump data for a MESSAGETABLE resource */
static void dump_msgtable_data( const void *ptr, unsigned int size, unsigned int id, const char *prefix )
{
    const MESSAGE_RESOURCE_DATA *data = ptr;
    const MESSAGE_RESOURCE_BLOCK *block = data->Blocks;
    unsigned i, j;

    for (i = 0; i < data->NumberOfBlocks; i++, block++)
    {
        const MESSAGE_RESOURCE_ENTRY *entry;

        entry = (const MESSAGE_RESOURCE_ENTRY *)((const char *)data + block->OffsetToEntries);
        for (j = block->LowId; j <= block->HighId; j++)
        {
            if (entry->Flags & MESSAGE_RESOURCE_UNICODE)
            {
                const WCHAR *str = (const WCHAR *)entry->Text;
                printf( "%s%08x L\"", prefix, j );
                dump_strW( str, strlenW(str) );
                printf( "\"\n" );
            }
            else
            {
                const char *str = (const char *) entry->Text;
                printf( "%s%08x \"", prefix, j );
                dump_strA( entry->Text, strlen(str) );
                printf( "\"\n" );
            }
            entry = (const MESSAGE_RESOURCE_ENTRY *)((const char *)entry + entry->Length);
        }
    }
}

static void dump_dir_resource(void)
{
    const IMAGE_RESOURCE_DIRECTORY *root = get_dir(IMAGE_FILE_RESOURCE_DIRECTORY);
    const IMAGE_RESOURCE_DIRECTORY *namedir;
    const IMAGE_RESOURCE_DIRECTORY *langdir;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY *e1, *e2, *e3;
    const IMAGE_RESOURCE_DIR_STRING_U *string;
    const IMAGE_RESOURCE_DATA_ENTRY *data;
    int i, j, k;

    if (!root) return;

    printf( "Resources:" );

    for (i = 0; i< root->NumberOfNamedEntries + root->NumberOfIdEntries; i++)
    {
        e1 = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(root + 1) + i;
        namedir = (const IMAGE_RESOURCE_DIRECTORY *)((const char *)root + e1->u2.s3.OffsetToDirectory);
        for (j = 0; j < namedir->NumberOfNamedEntries + namedir->NumberOfIdEntries; j++)
        {
            e2 = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(namedir + 1) + j;
            langdir = (const IMAGE_RESOURCE_DIRECTORY *)((const char *)root + e2->u2.s3.OffsetToDirectory);
            for (k = 0; k < langdir->NumberOfNamedEntries + langdir->NumberOfIdEntries; k++)
            {
                e3 = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(langdir + 1) + k;

                printf( "\n  " );
                if (e1->u1.s1.NameIsString)
                {
                    string = (const IMAGE_RESOURCE_DIR_STRING_U*)((const char *)root + e1->u1.s1.NameOffset);
                    dump_unicode_str( string->NameString, string->Length );
                }
                else
                {
                    const char *type = get_resource_type( e1->u1.s2.Id );
                    if (type) printf( "%s", type );
                    else printf( "%04x", e1->u1.s2.Id );
                }

                printf( " Name=" );
                if (e2->u1.s1.NameIsString)
                {
                    string = (const IMAGE_RESOURCE_DIR_STRING_U*) ((const char *)root + e2->u1.s1.NameOffset);
                    dump_unicode_str( string->NameString, string->Length );
                }
                else
                    printf( "%04x", e2->u1.s2.Id );

                printf( " Language=%04x:\n", e3->u1.s2.Id );
                data = (const IMAGE_RESOURCE_DATA_ENTRY *)((const char *)root + e3->u2.OffsetToData);
                if (e1->u1.s1.NameIsString)
                {
                    dump_data( RVA( data->OffsetToData, data->Size ), data->Size, "    " );
                }
                else switch(e1->u1.s2.Id)
                {
                case 6:
                    dump_string_data( RVA( data->OffsetToData, data->Size ), data->Size,
                                      e2->u1.s2.Id, "    " );
                    break;
                case 11:
                    dump_msgtable_data( RVA( data->OffsetToData, data->Size ), data->Size,
                                        e2->u1.s2.Id, "    " );
                    break;
                default:
                    dump_data( RVA( data->OffsetToData, data->Size ), data->Size, "    " );
                    break;
                }
            }
        }
    }
    printf( "\n\n" );
}

static void dump_debug(void)
{
    const char* stabs = NULL;
    unsigned    szstabs = 0;
    const char* stabstr = NULL;
    unsigned    szstr = 0;
    unsigned    i;
    const IMAGE_SECTION_HEADER*	sectHead;

    sectHead = (const IMAGE_SECTION_HEADER*)
        ((const char*)PE_nt_headers + sizeof(DWORD) +
         sizeof(IMAGE_FILE_HEADER) + PE_nt_headers->FileHeader.SizeOfOptionalHeader);

    for (i = 0; i < PE_nt_headers->FileHeader.NumberOfSections; i++, sectHead++)
    {
        if (!strcmp((const char *)sectHead->Name, ".stab"))
        {
            stabs = RVA(sectHead->VirtualAddress, sectHead->Misc.VirtualSize); 
            szstabs = sectHead->Misc.VirtualSize;
        }
        if (!strncmp((const char *)sectHead->Name, ".stabstr", 8))
        {
            stabstr = RVA(sectHead->VirtualAddress, sectHead->Misc.VirtualSize);
            szstr = sectHead->Misc.VirtualSize;
        }
    }
    if (stabs && stabstr)
        dump_stabs(stabs, szstabs, stabstr, szstr);
}

static void dump_symbol_table(void)
{
    const IMAGE_SYMBOL* sym;
    int                 numsym;
    const char*         strtable;

    numsym = PE_nt_headers->FileHeader.NumberOfSymbols;
    if (!PE_nt_headers->FileHeader.PointerToSymbolTable || !numsym)
        return;
    sym = PRD(PE_nt_headers->FileHeader.PointerToSymbolTable,
                                   sizeof(*sym) * numsym);
    if (!sym) return;
    /* FIXME: no way to get strtable size */
    strtable = (const char*)&sym[numsym];

    dump_coff_symbol_table(sym, numsym, IMAGE_FIRST_SECTION(PE_nt_headers));
}

enum FileSig get_kind_exec(void)
{
    const WORD*                pw;
    const DWORD*               pdw;
    const IMAGE_DOS_HEADER*    dh;

    pw = PRD(0, sizeof(WORD));
    if (!pw) {printf("Can't get main signature, aborting\n"); return 0;}

    if (*pw != IMAGE_DOS_SIGNATURE) return SIG_UNKNOWN;

    if ((dh = PRD(0, sizeof(IMAGE_DOS_HEADER))))
    {
        /* the signature is the first DWORD */
        pdw = PRD(dh->e_lfanew, sizeof(DWORD));
        if (pdw)
        {
            if (*pdw == IMAGE_NT_SIGNATURE)                     return SIG_PE;
            if (*(const WORD *)pdw == IMAGE_OS2_SIGNATURE)      return SIG_NE;
            if (*(const WORD *)pdw == IMAGE_VXD_SIGNATURE)      return SIG_LE;
            return SIG_DOS;
        }
    }
    return 0;
}

void pe_dump(void)
{
    int	all = (globals.dumpsect != NULL) && strcmp(globals.dumpsect, "ALL") == 0;

    PE_nt_headers = get_nt_header();
    if (is_fake_dll()) printf( "*** This is a Wine fake DLL ***\n\n" );

    if (globals.do_dumpheader)
    {
	dump_pe_header();
	/* FIXME: should check ptr */
	dump_sections(PRD(0, 1), (const char*)PE_nt_headers + sizeof(DWORD) +
		      sizeof(IMAGE_FILE_HEADER) + PE_nt_headers->FileHeader.SizeOfOptionalHeader,
		      PE_nt_headers->FileHeader.NumberOfSections);
    }
    else if (!globals.dumpsect)
    {
	/* show at least something here */
	dump_pe_header();
    }

    if (globals.dumpsect)
    {
	if (all || !strcmp(globals.dumpsect, "import"))
        {
	    dump_dir_imported_functions();
	    dump_dir_delay_imported_functions();
        }
	if (all || !strcmp(globals.dumpsect, "export"))
	    dump_dir_exported_functions();
	if (all || !strcmp(globals.dumpsect, "debug"))
	    dump_dir_debug();
	if (all || !strcmp(globals.dumpsect, "resource"))
	    dump_dir_resource();
	if (all || !strcmp(globals.dumpsect, "tls"))
	    dump_dir_tls();
	if (all || !strcmp(globals.dumpsect, "clr"))
	    dump_dir_clr_header();
	if (all || !strcmp(globals.dumpsect, "reloc"))
	    dump_dir_reloc();
	if (all || !strcmp(globals.dumpsect, "except"))
	    dump_dir_exceptions();
    }
    if (globals.do_symbol_table)
        dump_symbol_table();
    if (globals.do_debug)
        dump_debug();
}

typedef struct _dll_symbol {
    size_t	ordinal;
    char       *symbol;
} dll_symbol;

static dll_symbol *dll_symbols = NULL;
static dll_symbol *dll_current_symbol = NULL;

/* Compare symbols by ordinal for qsort */
static int symbol_cmp(const void *left, const void *right)
{
    return ((const dll_symbol *)left)->ordinal > ((const dll_symbol *)right)->ordinal;
}

/*******************************************************************
 *         dll_close
 *
 * Free resources used by DLL
 */
/* FIXME: Not used yet
static void dll_close (void)
{
    dll_symbol*	ds;

    if (!dll_symbols) {
	fatal("No symbols");
    }
    for (ds = dll_symbols; ds->symbol; ds++)
	free(ds->symbol);
    free (dll_symbols);
    dll_symbols = NULL;
}
*/

static	void	do_grab_sym( void )
{
    const IMAGE_EXPORT_DIRECTORY*exportDir;
    unsigned			i, j;
    const DWORD*		pName;
    const DWORD*		pFunc;
    const WORD* 		pOrdl;
    const char*			ptr;
    DWORD*			map;

    PE_nt_headers = get_nt_header();
    if (!(exportDir = get_dir(IMAGE_FILE_EXPORT_DIRECTORY))) return;

    pName = RVA(exportDir->AddressOfNames, exportDir->NumberOfNames * sizeof(DWORD));
    if (!pName) {printf("Can't grab functions' name table\n"); return;}
    pOrdl = RVA(exportDir->AddressOfNameOrdinals, exportDir->NumberOfNames * sizeof(WORD));
    if (!pOrdl) {printf("Can't grab functions' ordinal table\n"); return;}
    pFunc = RVA(exportDir->AddressOfFunctions, exportDir->NumberOfFunctions * sizeof(DWORD));
    if (!pFunc) {printf("Can't grab functions' address table\n"); return;}

    /* dll_close(); */

    if (!(dll_symbols = malloc((exportDir->NumberOfFunctions + 1) * sizeof(dll_symbol))))
	fatal ("Out of memory");

    /* bit map of used funcs */
    map = calloc(((exportDir->NumberOfFunctions + 31) & ~31) / 32, sizeof(DWORD));
    if (!map) fatal("no memory");

    for (j = 0; j < exportDir->NumberOfNames; j++, pOrdl++)
    {
	map[*pOrdl / 32] |= 1 << (*pOrdl % 32);
	ptr = RVA(*pName++, sizeof(DWORD));
	if (!ptr) ptr = "cant_get_function";
	dll_symbols[j].symbol = strdup(ptr);
	dll_symbols[j].ordinal = exportDir->Base + *pOrdl;
	assert(dll_symbols[j].symbol);
    }

    for (i = 0; i < exportDir->NumberOfFunctions; i++)
    {
	if (pFunc[i] && !(map[i / 32] & (1 << (i % 32))))
	{
	    char ordinal_text[256];
	    /* Ordinal only entry */
            snprintf (ordinal_text, sizeof(ordinal_text), "%s_%u",
		      globals.forward_dll ? globals.forward_dll : OUTPUT_UC_DLL_NAME,
		      exportDir->Base + i);
	    str_toupper(ordinal_text);
	    dll_symbols[j].symbol = strdup(ordinal_text);
	    assert(dll_symbols[j].symbol);
	    dll_symbols[j].ordinal = exportDir->Base + i;
	    j++;
	    assert(j <= exportDir->NumberOfFunctions);
	}
    }
    free(map);

    if (NORMAL)
	printf("%u named symbols in DLL, %u total, %d unique (ordinal base = %d)\n",
	       exportDir->NumberOfNames, exportDir->NumberOfFunctions, j, exportDir->Base);

    qsort( dll_symbols, j, sizeof(dll_symbol), symbol_cmp );

    dll_symbols[j].symbol = NULL;

    dll_current_symbol = dll_symbols;
}

/*******************************************************************
 *         dll_open
 *
 * Open a DLL and read in exported symbols
 */
int dll_open (const char *dll_name)
{
    return dump_analysis(dll_name, do_grab_sym, SIG_PE);
}

/*******************************************************************
 *         dll_next_symbol
 *
 * Get next exported symbol from dll
 */
int dll_next_symbol (parsed_symbol * sym)
{
    if (!dll_current_symbol || !dll_current_symbol->symbol)
       return 1;
     assert (dll_symbols);
    sym->symbol = strdup (dll_current_symbol->symbol);
    sym->ordinal = dll_current_symbol->ordinal;
    dll_current_symbol++;
    return 0;
}
