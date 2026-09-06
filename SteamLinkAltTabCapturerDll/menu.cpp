#include "pch.h"
#include "log.h"
#include "sdl.h"
#include "menu.h"

// ============================================================================
// Module-scope state
// ============================================================================

static SDL_Window* g_pWin      = NULL;
static BOOL*       g_pGrabbed  = NULL;
static WNDPROC     g_oldProc   = NULL;

// ============================================================================
// Subclass procedure
// ============================================================================

static void ToggleGrab(HWND hwnd)
{
    SDL3_SetKeyboardGrab(g_pWin, !*g_pGrabbed);
    *g_pGrabbed = !*g_pGrabbed;

    HMENU hSys = GetSystemMenu(hwnd, FALSE);
    if (hSys)
        CheckMenuItem(hSys, MENU_TOGGLE_GRAB,
                      MF_BYCOMMAND | (*g_pGrabbed ? MF_CHECKED : MF_UNCHECKED));
}

static LRESULT CALLBACK WndSubProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_SYSCOMMAND && (wParam & 0xFFF0) == MENU_TOGGLE_GRAB)
    {
        ToggleGrab(hwnd);
        return 0;
    }

    if (msg == WM_HOTKEY && wParam == HOTKEY_ID_GRAB)
    {
        ToggleGrab(hwnd);
        return 0;
    }

    return CallWindowProcW(g_oldProc, hwnd, msg, wParam, lParam);
}

// ============================================================================
// Public interface
// ============================================================================

void Menu_InitSubclass(HWND hwnd, SDL_Window* pWin, BOOL* pGrabbed)
{
    g_pWin     = pWin;
    g_pGrabbed = pGrabbed;

    // Append system menu item
    HMENU hSys = GetSystemMenu(hwnd, FALSE);
    if (hSys)
    {
        // Check if menu already exist
		if (GetMenuState(hSys, MENU_TOGGLE_GRAB, MF_BYCOMMAND) == -1)
		{
			Log("Appending system menu item");
            AppendMenuW(hSys, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hSys, MF_STRING, MENU_TOGGLE_GRAB, L"SDL Keyboard Grab\tCtrl+Shift+G");
            CheckMenuItem(hSys, MENU_TOGGLE_GRAB, MF_BYCOMMAND | (*g_pGrabbed ? MF_CHECKED : MF_UNCHECKED));
        }
		else
		{
			Log("System menu item already exists");
		}
    }

    // Subclass
    g_oldProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)WndSubProc);
    Log("Subclassed (old: %p)", g_oldProc);

    // Global hotkey
    RegisterHotKey(hwnd, HOTKEY_ID_GRAB, MOD_CONTROL | MOD_SHIFT, 'G');
}

void Menu_CleanupSubclass(HWND hwnd)
{
    if (g_oldProc)
    {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)g_oldProc);
        Log("Unsubclassed %p", hwnd);
        g_oldProc = NULL;
    }

    UnregisterHotKey(hwnd, HOTKEY_ID_GRAB);

    g_pWin     = NULL;
    g_pGrabbed = NULL;
}
