#include <windows.h>
#include <iostream>
#include <string>
#include <tlhelp32.h>

// Structure to pass target process context to EnumWindows callback
struct TargetWindowSearch {
	DWORD targetProcessId;
	HWND hFoundWnd;
};

// Callback to locate top-level window owned by SteamLink.exe matching class "SDL_app"
BOOL CALLBACK EnumSteamLinkWindowsCallback(HWND hWnd, LPARAM lParam) {
	TargetWindowSearch* searchData = (TargetWindowSearch*)lParam;

	DWORD windowProcessId = 0;
	GetWindowThreadProcessId(hWnd, &windowProcessId);

	if (windowProcessId == searchData->targetProcessId) {
		char className[256] = { 0 };
		GetClassNameA(hWnd, className, sizeof(className));

		if (strcmp(className, "SDL_app") == 0) {
			searchData->hFoundWnd = hWnd;
			return FALSE; // Stop enumerating, found match
		}
	}
	return TRUE; // Continue enumerating
}

DWORD GetProcessIdByName(const std::wstring& processName) {
	DWORD pid = 0;

	// 1. Create a snapshot of all active processes in the system
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	PROCESSENTRY32W pe;
	pe.dwSize = sizeof(PROCESSENTRY32W);

	// 2. Retrieve information about the first process
	if (Process32FirstW(hSnapshot, &pe)) {
		// 3. Loop through all processes in the snapshot
		do {
			// Compare the process name (case-insensitive mapping might be preferred)
			if (processName == pe.szExeFile) {
				pid = pe.th32ProcessID;
				break;
			}
		} while (Process32NextW(hSnapshot, &pe));
	}

	// 4. Clean up the snapshot handle
	CloseHandle(hSnapshot);

	return pid;
}

// Rewritten FindSteamLinkWindow function
HWND FindSteamLinkWindow() {
	DWORD steamLinkPid = GetProcessIdByName(L"SteamLink.exe");
	if (steamLinkPid == 0) {
		std::cout << "[!] SteamLink.exe process not found!" << std::endl;
		return NULL;
	}

	std::cout << "[+] Found SteamLink.exe process ID: " << steamLinkPid << std::endl;

	TargetWindowSearch searchData = { steamLinkPid, NULL };
	EnumWindows(EnumSteamLinkWindowsCallback, (LPARAM)&searchData);

	if (!searchData.hFoundWnd) {
		std::cout << "[!] Could not find 'SDL_app' window inside SteamLink.exe PID "
			<< steamLinkPid << std::endl;
	}

	return searchData.hFoundWnd;
}

// Helper to construct Win32 keyboard lParam bitmask
LPARAM BuildLParam(WORD scanCode, bool isExtended, bool isAltDown, bool isKeyUp, bool isRepeat) {
	LPARAM lParam = 1; // Repeat count = 1
	lParam |= ((scanCode & 0xFF) << 16);
	if (isExtended) lParam |= (1 << 24);
	if (isAltDown)   lParam |= (1 << 29);
	if (isRepeat || isKeyUp) lParam |= (1 << 30);
	if (isKeyUp)     lParam |= (1 << 31);
	return lParam;
}

void TestSendKey(HWND hWnd, UINT msg, WPARAM vkCode, WORD scanCode, bool isExtended, bool isAltDown, bool isKeyUp) {
	LPARAM lParam = BuildLParam(scanCode, isExtended, isAltDown, isKeyUp, false);

	std::cout << "\n[-->] Posting Message to HWND 0x" << std::hex << hWnd << std::dec << ":" << std::endl;
	std::cout << "      Msg: 0x" << std::hex << msg
		<< " | VK: 0x" << vkCode
		<< " | ScanCode: 0x" << scanCode
		<< " | lParam: 0x" << lParam << std::dec << std::endl;

	BOOL res = PostMessageA(hWnd, msg, vkCode, lParam);
	std::cout << "      Result: " << (res ? "SUCCESS" : "FAILED") << std::endl;
}

void TestAltTab(HWND hWnd) {
	std::cout << "\n=== Testing Alt+Tab Sequence to SDL_app ===" << std::endl;
	WORD altScan = (WORD)MapVirtualKeyA(VK_MENU, MAPVK_VK_TO_VSC);
	WORD tabScan = (WORD)MapVirtualKeyA(VK_ESCAPE, MAPVK_VK_TO_VSC);

	// 1. Alt Down (WM_SYSKEYDOWN)
	TestSendKey(hWnd, WM_SYSKEYDOWN, VK_MENU, altScan, false, true, false);
	Sleep(50);

	// 2. Tab Down (WM_SYSKEYDOWN)
	TestSendKey(hWnd, WM_SYSKEYDOWN, VK_ESCAPE, tabScan, false, true, false);
	Sleep(50);

	// 3. Tab Up (WM_SYSKEYUP)
	TestSendKey(hWnd, WM_SYSKEYUP, VK_ESCAPE, tabScan, false, true, true);
	Sleep(50);

	// 4. Alt Up (WM_SYSKEYUP)
	TestSendKey(hWnd, WM_SYSKEYUP, VK_MENU, altScan, false, false, true);
}

void TestWinKeySequence(HWND hWnd) {
	std::cout << "\n=== Testing Win Key Tap Sequence to SDL_app ===" << std::endl;
	WORD winScan = 0x5B;

	// 1. Win Key Down (WM_KEYDOWN)
	TestSendKey(hWnd, WM_KEYDOWN, VK_LWIN, winScan, true, false, false);

	std::cout << "--> Holding Win Key Down. Press ENTER to send KEYUP..." << std::endl;
	std::cin.get();

	// 2. Win Key Up (WM_KEYUP)
	TestSendKey(hWnd, WM_KEYUP, VK_LWIN, winScan, true, false, true);
}

int main() {
	SetConsoleTitleA("Steam Link SDL_app Input Tester");

	std::cout << "Searching for Steam Link (Class: 'SDL_app')..." << std::endl;
	HWND hWnd = FindSteamLinkWindow();

	if (!hWnd) {
		std::cout << "[ERROR] Could not locate Steam Link window! Make sure SteamLink.exe is running." << std::endl;
		std::cout << "Press ENTER to exit...";
		std::cin.get();
		return 1;
	}

	char windowTitle[256] = { 0 };
	GetWindowTextA(hWnd, windowTitle, sizeof(windowTitle));
	std::cout << "[SUCCESS] Target Window Found!" << std::endl;
	std::cout << "          HWND: 0x" << std::hex << hWnd << std::dec << std::endl;
	std::cout << "          Title: '" << windowTitle << "'" << std::endl;

	while (true) {
		std::cout << "\n--------------------------------------------------" << std::endl;
		std::cout << "Select Test Scenario:" << std::endl;
		std::cout << "  1. Send Win Key Tap (KeyDown -> Wait for Enter -> KeyUp)" << std::endl;
		std::cout << "  2. Send Alt+ESC Pulse Sequence" << std::endl;
		std::cout << "  3. Send Custom Virtual Key (Manual Entry)" << std::endl;
		std::cout << "  4. Bring Steam Link to Foreground" << std::endl;
		std::cout << "  0. Exit" << std::endl;
		std::cout << "Choice: ";

		int choice = -1;
		std::cin >> choice;
		std::cin.ignore(); // Clear newline

		if (choice == 0) break;

		switch (choice) {
		case 1:
			TestWinKeySequence(hWnd);
			break;
		case 2:
			TestAltTab(hWnd);
			break;
		case 3: {
			DWORD vk = 0, scan = 0, isSys = 0, isUp = 0;
			std::cout << "Enter VK Code in Hex (e.g., 5B for Win, 09 for Tab): 0x";
			std::cin >> std::hex >> vk;
			std::cout << "Enter ScanCode in Hex (e.g., 5B for Win, 0F for Tab): 0x";
			std::cin >> std::hex >> scan;
			std::cout << "Use System Msg (1 for WM_SYSKEY, 0 for WM_KEY): ";
			std::cin >> isSys;
			std::cout << "Is KeyUp (1 for KEYUP, 0 for KEYDOWN): ";
			std::cin >> isUp;
			std::cin.ignore();

			UINT msg = isSys ? (isUp ? WM_SYSKEYUP : WM_SYSKEYDOWN) : (isUp ? WM_KEYUP : WM_KEYDOWN);
			TestSendKey(hWnd, msg, vk, (WORD)scan, true, isSys != 0, isUp != 0);
			break;
		}
		case 4:
			SetForegroundWindow(hWnd);
			std::cout << "Brought Steam Link to foreground." << std::endl;
			break;
		default:
			std::cout << "Invalid choice." << std::endl;
			break;
		}
	}

	return 0;
}