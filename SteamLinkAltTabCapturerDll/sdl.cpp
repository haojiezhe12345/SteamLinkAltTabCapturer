#include "pch.h"
#include "log.h"
#include "sdl.h"

// ============================================================================
// FindSDLWindow
// ============================================================================

HWND FindSDLWindow(void)
{
    DWORD pid = GetCurrentProcessId();

    for (HWND hw = FindWindowExA(NULL, NULL, NULL, NULL);
         hw != NULL;
         hw = FindWindowExA(NULL, hw, NULL, NULL))
    {
        DWORD dwPid = 0;
        GetWindowThreadProcessId(hw, &dwPid);
        if (dwPid != pid || !IsWindowVisible(hw))
            continue;

        char cls[64] = "";
        GetClassNameA(hw, cls, sizeof(cls));
        if (strcmp(cls, "SDL_app") == 0)
            return hw;

        for (HWND h1 = FindWindowExA(hw, NULL, NULL, NULL);
             h1 != NULL;
             h1 = FindWindowExA(hw, h1, NULL, NULL))
        {
            dwPid = 0;
            GetWindowThreadProcessId(h1, &dwPid);
            if (dwPid != pid || !IsWindowVisible(h1))
                continue;

            char cls1[64] = "";
            GetClassNameA(h1, cls1, sizeof(cls1));
            if (strcmp(cls1, "SDL_app") == 0)
                return h1;

            for (HWND h2 = FindWindowExA(h1, NULL, NULL, NULL);
                 h2 != NULL;
                 h2 = FindWindowExA(h1, h2, NULL, NULL))
            {
                dwPid = 0;
                GetWindowThreadProcessId(h2, &dwPid);
                if (dwPid != pid || !IsWindowVisible(h2))
                    continue;

                char cls2[64] = "";
                GetClassNameA(h2, cls2, sizeof(cls2));
                if (strcmp(cls2, "SDL_app") == 0)
                    return h2;
            }
        }
    }

    return NULL;
}

// ============================================================================
// SDL3 function pointer types
// ============================================================================

typedef int (*pfn_SetKeyboardGrab)(SDL_Window*, int);
typedef int (*pfn_GetKeyboardGrab)(SDL_Window*);
typedef int (*pfn_SetHint)(const char*, const char*);

// ============================================================================
// Module-scope state
// ============================================================================

static pfn_SetKeyboardGrab g_pfnSetGrab  = NULL;
static pfn_GetKeyboardGrab g_pfnGetGrab  = NULL;
static pfn_SetHint         g_pfnSetHint  = NULL;

// ============================================================================
// Public interface
// ============================================================================

BOOL SDL3_Init(void)
{
    HMODULE hMod = GetModuleHandleA("SDL3.dll");
    if (!hMod)
        return FALSE;

    g_pfnSetGrab = (pfn_SetKeyboardGrab)GetProcAddress(hMod, "SDL_SetWindowKeyboardGrab");
    g_pfnGetGrab = (pfn_GetKeyboardGrab)GetProcAddress(hMod, "SDL_GetWindowKeyboardGrab");
    g_pfnSetHint = (pfn_SetHint)GetProcAddress(hMod, "SDL_SetHint");

    return g_pfnSetGrab != NULL;
}

SDL_Window* SDL3_GetWindow(HWND hwnd)
{
    void* pData = GetPropA(hwnd, "SDL_WindowData");
    if (!pData)
        return NULL;

    __try
    {
        SDL_Window* pWindow  = *(SDL_Window**)pData;
        HWND        hStored  = *(HWND*)((BYTE*)pData + 4);

        if (hStored == hwnd)
            return pWindow;

        void* pData2 = GetPropA(hStored, "SDL_WindowData");
        if (pData2)
        {
            SDL_Window* pWindow2 = *(SDL_Window**)pData2;
            HWND        hStored2 = *(HWND*)((BYTE*)pData2 + 4);
            if (hStored2 == hStored)
                return pWindow2;
        }

        return NULL;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return NULL;
    }
}

BOOL SDL3_SetKeyboardGrab(SDL_Window* window, BOOL enable)
{
    if (!g_pfnSetGrab || !window)
        return FALSE;
    BOOL ok = g_pfnSetGrab(window, enable ? 1 : 0) != 0;
    if (ok)
        Log(enable ? "Grab ON" : "Grab OFF");
    return ok;
}

BOOL SDL3_GetKeyboardGrab(SDL_Window* window)
{
    if (!g_pfnGetGrab || !window)
        return FALSE;
    return g_pfnGetGrab(window) != 0;
}

BOOL SDL3_SetHint(const char* name, const char* value)
{
    if (!g_pfnSetHint)
        return FALSE;
    return g_pfnSetHint(name, value) != 0;
}
