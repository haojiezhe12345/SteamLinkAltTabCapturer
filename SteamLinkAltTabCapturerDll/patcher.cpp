#include "pch.h"
#include "patcher.h"
#include "log.h"

// Binary patch RVAs (image base 0x400000)
#define RVA_JNZ_TAB     0x1A533D
#define RVA_JZ_FLAG     0x1A534A
#define RVA_PUSH_CALL   0x1A5350
#define RVA_JMP_RET     0x1A5360

struct Patch { DWORD rva; const BYTE* exp; const BYTE* rep; size_t sz; const char* desc; };

static const BYTE nop6[]  = {0x90,0x90,0x90,0x90,0x90,0x90};
static const BYTE nop14[] = {0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};

static const BYTE e1[]={0x0F,0x85,0x95,0x0E,0x00,0x00};
static const BYTE e2[]={0x0F,0x84,0x88,0x0E,0x00,0x00};
static const BYTE e3[]={0xFF,0xB7,0x80,0x03,0x00,0x00,0xE8,0xDF,0x22,0x10,0x00,0x83,0xC4,0x04};
static const BYTE e4[]={0xE9,0x85,0x07,0x00,0x00};
static const BYTE n4[]={0xE9,0xE6,0x01,0x00,0x00};

static const Patch g_p[] = {
    { RVA_JNZ_TAB,   e1, nop6,  6,  "Alt+Tab KEYDOWN skip" },
    { RVA_JZ_FLAG,   e2, nop6,  6,  "Flag check skip" },
    { RVA_PUSH_CALL, e3, nop14, 14, "SDL_MinimizeWindow call" },
    { RVA_JMP_RET,   e4, n4,    5,  "JMP redirect" },
};

void PatchBin() {
    BYTE* base = (BYTE*)GetModuleHandleA(NULL);
    if (!base) return;
    for (int i = 0; i < 4; i++) {
        const Patch& p = g_p[i];
        BYTE* a = base + p.rva;
        DWORD prot;
        if (memcmp(a, p.rep, p.sz) == 0) { Log("Already patched: %s", p.desc); continue; }
        if (memcmp(a, p.exp, p.sz) != 0) { Log("Mismatch: %s @ %p", p.desc, a); continue; }
        if (VirtualProtect(a, p.sz, PAGE_EXECUTE_READWRITE, &prot)) {
            memcpy(a, p.rep, p.sz);
            VirtualProtect(a, p.sz, prot, &prot);
            Log("Patched: %s", p.desc);
        }
    }
}
