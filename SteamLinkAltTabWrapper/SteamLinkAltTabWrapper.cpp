#include <windows.h>
#include <tlhelp32.h>
#include <string>

// ============================================================================
// Path helpers
// ============================================================================

static std::wstring GetExeDir()
{
    WCHAR buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring path(buf);
    return path.substr(0, path.find_last_of(L'\\') + 1);
}

static std::wstring GetDllPath()
{
    return GetExeDir() + L"SteamLinkAltTabCapturerDll.dll";
}

// ============================================================================
// Report error (MessageBox)
// ============================================================================

static void ReportError(const wchar_t* fmt, ...)
{
    wchar_t msg[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(msg, sizeof(msg) / sizeof(wchar_t), _TRUNCATE, fmt, args);
    va_end(args);
    MessageBoxW(NULL, msg, L"SteamLinkAltTabWrapper", MB_OK | MB_ICONERROR);
}

// ============================================================================
// Inject a DLL into a running process using LoadLibraryA.
// ============================================================================

static BOOL InjectDll(HANDLE hProcess, const std::wstring& dllPath)
{
    char dllPathA[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, dllPath.c_str(), -1, dllPathA, sizeof(dllPathA), NULL, NULL);

    SIZE_T pathLen = strlen(dllPathA) + 1;

    LPVOID remoteMem = VirtualAllocEx(hProcess, NULL, pathLen,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem)
        return FALSE;

    if (!WriteProcessMemory(hProcess, remoteMem, dllPathA, pathLen, NULL))
    {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return FALSE;
    }

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32) return FALSE;
    FARPROC pLoadLibrary = GetProcAddress(hKernel32, "LoadLibraryA");

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
                                         (LPTHREAD_START_ROUTINE)pLoadLibrary,
                                         remoteMem, 0, NULL);
    if (!hThread)
    {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return FALSE;
    }

    WaitForSingleObject(hThread, 10000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);

    return exitCode != 0;
}

// ============================================================================
// Entry point
// ============================================================================

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    std::wstring dllPath = GetDllPath();

    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        ReportError(L"DLL not found:\n%s", dllPath.c_str());
        return 1;
    }

    // Create process debugged — prevents IFEO recursion
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    if (!CreateProcessW(NULL, lpCmdLine, NULL, NULL, FALSE,
                        DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi))
    {
        ReportError(L"CreateProcess failed (error %lu).", GetLastError());
        return 1;
    }

    // Wait for the initial debug event so the process is initialized
    DEBUG_EVENT dbg = { 0 };
    WaitForDebugEvent(&dbg, 5000);

    // Detach debugger — process now runs freely
    DebugActiveProcessStop(pi.dwProcessId);
    DebugSetProcessKillOnExit(FALSE);

    // Inject
    if (!InjectDll(pi.hProcess, dllPath))
    {
        ReportError(L"DLL injection failed (%lu).", GetLastError());
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}
