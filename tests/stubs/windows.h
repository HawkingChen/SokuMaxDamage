#pragma once
#include <cstdint>
#include <cstddef>
#define __fastcall
#define __declspec(x)
#define APIENTRY
#define MAX_PATH 260
#define TRUE 1
#define RDATA_SECTION_OFFSET 0x00857000
#define RDATA_SECTION_SIZE 0x0002B000
#define FW_NORMAL 400
#define PAGE_EXECUTE_WRITECOPY 0x80
using BYTE = unsigned char;
using DWORD = unsigned long;
using HMODULE = void *;
using LPVOID = void *;
using PVOID = void *;
using HANDLE = void *;
extern "C" DWORD GetModuleFileNameA(HMODULE, char*, DWORD);
extern "C" unsigned GetPrivateProfileIntA(const char *,const char *, int,const char*);
extern "C" int VirtualProtect(PVOID, std::size_t, DWORD, DWORD*);
extern "C" int FlushInstructionCache(HANDLE, PVOID, std::size_t);
extern "C" HANDLE GetCurrentProcess();
