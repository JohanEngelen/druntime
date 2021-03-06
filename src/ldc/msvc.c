/**
 * Implementation of support routines for synchronized blocks.
 *
 * Copyright: Copyright The LDC Developers 2012
 * License:   <a href="http://www.boost.org/LICENSE_1_0.txt">Boost License 1.0</a>.
 * Authors:   Kai Nacke <kai@redstar.de>
 */

/*          Copyright The LDC Developers 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

/* ================================= Win32 ============================ */

#if _WIN32

#if _MSC_VER || __MINGW64__

#include <Windows.h>
#include <string.h>

const char* _data_start__;
const char* _data_end__;
const char* _bss_start__;
const char* _bss_end__;

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

static void init_data_seg(void)
{
    // Get handle to this module (.exe/.dll)
    HMODULE hModule = (HMODULE) &__ImageBase;
    char* imageBase = (char*) hModule;

    // Get the DOS header
    PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER) hModule;

    // Get the address of the NT headers
    PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS) (imageBase + pDosHeader->e_lfanew);

    // After the NT headers comes the sections table
    PIMAGE_SECTION_HEADER pSectionHeader = (PIMAGE_SECTION_HEADER) (pNtHeaders + 1);

    // Iterate over all sections
    for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++)
    {
         BYTE* name = pSectionHeader->Name;
         if (memcmp(name, ".data", 6) == 0)
         {
            _data_start__ = imageBase + pSectionHeader->VirtualAddress;
            _data_end__ = _data_start__ + pSectionHeader->Misc.VirtualSize;
         }
         else if (memcmp(name, ".bss", 5) == 0)
         {
            _bss_start__ = imageBase + pSectionHeader->VirtualAddress;
            _bss_end__ = _bss_start__ + pSectionHeader->Misc.VirtualSize;
         }

         pSectionHeader++;
    }
}


typedef int  (__cdecl *_PF)(void);

static int __cdecl ctor(void)
{
    init_data_seg();
    return 0;
}

static int __cdecl dtor(void)
{
    return 0;
}


#pragma data_seg(push)

#pragma section(".CRT$XIY", long, read)
#pragma section(".CRT$XTY", long, read)

#pragma data_seg(".CRT$XIY")
__declspec(allocate(".CRT$XIY")) static _PF _ctor = &ctor;

#pragma data_seg(".CRT$XTY")
__declspec(allocate(".CRT$XTY")) static _PF _dtor = &dtor;

#pragma data_seg(pop)

/*********************************************************
 * Windows before Windows 8.1 does not support TLS alignment to anything
 *  higher than 8/16 bytes for Win32 and Win64, respectively.
 *  Some optimizations in LLVM (e.g. using aligned XMM access) do require
 *  higher alignments, though. In addition, the programmer can use align()
 *  to specify even larger requirements.
 * Fixing the alignment is done by adding a TLS callback that allocates
 *  a new copy of the TLS segment if the current one is not aligned properly.
 */

__declspec(thread) void* originalTLS; // saves the address of the original TLS to restore it before termination

extern void** GetTlsEntryAdr();

BOOL WINAPI fix_tlsAlignment(HINSTANCE hModule, DWORD  fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_DETACH || fdwReason == DLL_THREAD_DETACH)
    {
        if (originalTLS)
        {
            // restore original pointer
            void** tlsAdr = GetTlsEntryAdr();
            void* allocAdr = ((void**) *tlsAdr)[-1];
            *tlsAdr = originalTLS;
            HeapFree(GetProcessHeap(), 0, allocAdr);
        }
    }
    else
    {
        // crawl through the image to find the TLS alignment
        char* imageBase = (char*) hModule;
        PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER) hModule;
        PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS) (imageBase + pDosHeader->e_lfanew);
        PIMAGE_SECTION_HEADER pSectionHeader = (PIMAGE_SECTION_HEADER) (pNtHeaders + 1);
        PIMAGE_DATA_DIRECTORY dataDir = pNtHeaders->OptionalHeader.DataDirectory + IMAGE_DIRECTORY_ENTRY_TLS;
        if (dataDir->VirtualAddress) // any TLS entry
        {
            PIMAGE_TLS_DIRECTORY tlsDir = (PIMAGE_TLS_DIRECTORY) (imageBase + dataDir->VirtualAddress);
            int alignShift = ((tlsDir->Characteristics >> 20) & 0xf);

            if (alignShift)
            {
                int alignmentMask = (1 << (alignShift - 1)) - 1;
                void** tlsAdr = GetTlsEntryAdr();
                if ((SIZE_T)*tlsAdr & alignmentMask)
                {
                    // this implementation does about the same as Windows 8.1.
                    HANDLE heap = GetProcessHeap();
                    SIZE_T tlsSize = tlsDir->EndAddressOfRawData - tlsDir->StartAddressOfRawData + tlsDir->SizeOfZeroFill;
                    SIZE_T allocSize = tlsSize + alignmentMask + sizeof(void*);
                    void* p = HeapAlloc(heap, 0, allocSize);
                    if (!p)
                        return 0;

                    void* aligned = (void*) (((SIZE_T) p + alignmentMask + sizeof(PVOID)) & ~alignmentMask);
                    void* old = *tlsAdr;
                    ((void**) aligned)[-1] = p;    // save base pointer for freeing
                    memcpy(aligned, old, tlsSize);
                    *tlsAdr = aligned;
                    originalTLS = old;
                }
            }
        }
    }
    return 1;
}

// the C++ TLS callbacks are written to ".CRT$XLC", but actually start after ".CRT$XLA".
//  Using ".CRT$XLB" allows this to come first in the array of TLS callbacks. This
//  guarantees that pointers saved within C++ TLS callbacks are not pointing into
//  abandoned memory

typedef BOOL WINAPI _TLSCB(HINSTANCE, DWORD, LPVOID);

#pragma data_seg(push)

#pragma section(".CRT$XLB", long, read)

#pragma data_seg(".CRT$XLB")
__declspec(allocate(".CRT$XLB")) static _TLSCB* _pfix_tls = &fix_tlsAlignment;

#pragma data_seg(pop)

#endif

#endif // _WIN32
