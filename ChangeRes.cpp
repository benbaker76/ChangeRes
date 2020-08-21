// ChangeRes.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "ChangeRes.h"
#include <windows.h>
#include <shellapi.h>
#include <io.h> 
#include <fcntl.h> 
#include <stdio.h>
#include <vector>
#include <algorithm>

using namespace std;

#define MSG_POPUP				WM_USER + 100

#define MNU_EXIT				1
#define MNU_AUTOSTART			2
#define MNU_SAVE_SETTINGS		3

#define MNU_RESOLUTIONS			4

#define RESOLUTION_RATIO_COUNT	8

struct ResolutionRatio
{
	INT ratioWidth;
	INT ratioHeight;
};

// 5:4, 4:3, 3:2, 8:5, 5:3, 16:9, 17:9
ResolutionRatio g_resolutionRatio[RESOLUTION_RATIO_COUNT] = { { 5, 4 }, { 4, 3 }, { 3, 2 }, { 8, 5 }, { 5, 3 }, { 16, 9 }, { 17, 9 }, { -1, -1 } };

HANDLE g_hMutex = NULL;
HINSTANCE g_hInstance = NULL;
const TCHAR g_szAppName[] = L"Resolution Changer";

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

class Settings
{
public:
	//static unsigned MinHorzRes;
	//static bool DisplayNonStdRatio;
	static bool AutoStart;

	static void reset()
	{
		//MinHorzRes = DEFAULT_MIN_HORZ_RES;
		//DisplayNonStdRatio = false;
		AutoStart = false;
	}

	static void loadFromRegistry()
	{
		HKEY hKey;
		unsigned long type, size;

		reset();

		if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\ChangeRes", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			DWORD buffer;
			size = 4;

			/* if (RegQueryValueEx(hKey, L"DisplayLoRes", 0, &type, (LPBYTE)&buffer, &size) == ERROR_SUCCESS)
			{
				if (type == REG_DWORD && size == 4)
					MinHorzRes = buffer ? 0 : DEFAULT_MIN_HORZ_RES;
			} */

			size = 4;

			/* if (RegQueryValueEx(hKey, L"DisplayNonStdRatio", 0, &type, (LPBYTE)&buffer, &size) == ERROR_SUCCESS)
			{
				if (type == REG_DWORD && size == 4)
					DisplayNonStdRatio = (buffer != 0);
			} */

			RegCloseKey(hKey);
		}

		if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			TCHAR buffer[MAX_PATH];
			size = sizeof(buffer);

			if (RegQueryValueEx(hKey, L"ChangeRes", 0, &type, (LPBYTE)&buffer, &size) == ERROR_SUCCESS)
			{
				if (type == REG_SZ)
					AutoStart = true;
			}

			RegCloseKey(hKey);
		}
	}

	// all=true: minden beállítást ment
	//    false: csak az autosave-et
	static bool saveToRegistry(bool all)
	{
		HKEY hKey;
		DWORD disp;
		bool ok = true;

		// menti a program beállításait a HKEY_CURRENT_USER/Software/ChangeRes ágra
		if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\ChangeRes", 0, NULL, 0, KEY_WRITE, NULL, &hKey, &disp) == ERROR_SUCCESS)
		{
			DWORD buffer;

			if (all)
			{
				/* buffer = (DWORD)DisplayNonStdRatio;

				if (RegSetValueEx(hKey, L"DisplayNonStdRatio", 0, REG_DWORD, (LPBYTE)&buffer, 4) != ERROR_SUCCESS)
					ok = false;
			
				buffer = (unsigned long)(MinHorzRes == 0);

				if (RegSetValueEx(hKey, L"DisplayLoRes", 0, REG_DWORD, (LPBYTE)&buffer, 4) != ERROR_SUCCESS)
					ok = false; */
			}
		}

		// fizikailag ki/bekapcsolja az automatikus indítást
		if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, &disp) == ERROR_SUCCESS)
		{
			if (AutoStart)
			{
				TCHAR fileName[MAX_PATH + 1];
				TCHAR buffer[MAX_PATH + 1];
				
				DWORD len = GetModuleFileName(0, fileName, MAX_PATH);
				fileName[len] = 0;

				swprintf(buffer, L"\"%ls\"", fileName);

				if (RegSetValueEx(hKey, L"ChangeRes", 0, REG_SZ, (LPBYTE)buffer, (wcslen(buffer) + 1) * sizeof(TCHAR)) != ERROR_SUCCESS)
				{
					ok = false;
				}
			}
			else
			{
				RegDeleteValue(hKey, L"ChangeRes");
			}
			
			RegCloseKey(hKey);
		}
		else
		{
			ok = false;
		}

		return ok;
	}
};

//unsigned Settings::MinHorzRes;
//bool Settings::DisplayNonStdRatio;
bool Settings::AutoStart;

class DisplayMode
{
	friend struct DisplayMode_Less;
	unsigned width, height, frequency, bitsPerPixel;

	int isValidRange()
	{
		return width >= 320 && width <= 2048 && height >= 200 && height <= 2048 && bitsPerPixel == 32 && frequency <= 200 ? SUCCESS : RANGE_ERROR;
	}

public:
	static const int
		SUCCESS = 0,
		PARSE_ERROR = 1,
		RANGE_ERROR = 2;

	DisplayMode()
	{ }

	DisplayMode(DEVMODE devMode)
	{
		width = devMode.dmPelsWidth;
		height = devMode.dmPelsHeight;
		frequency = devMode.dmDisplayFrequency;
		bitsPerPixel = devMode.dmBitsPerPel;
	}

	void setSafe()
	{
		width = 640;
		height = 480;
		bitsPerPixel = 32;
		frequency = 60;
	}

	int parseString(TCHAR *s)
	{
		char d1, d2;
		int n = swscanf(s, L"%dx%d%c%d%c%d", &width, &height, &d1, &bitsPerPixel, &d2, &frequency);
		
		if (n == 6 && d1 == 'x' && d2 == '@')
			return isValidRange();
		
		if (n == 4 && d1 == 'x')
		{
			frequency = 0;

			return isValidRange();
		}
		
		if (n == 4 && d1 == '@')
		{
			frequency = bitsPerPixel;
			bitsPerPixel = 0;

			return isValidRange();
		}

		if (n == 2)
		{
			bitsPerPixel = 0;
			frequency = 0;

			return isValidRange();
		}

		return PARSE_ERROR;
	}

	DisplayMode& operator= (const DisplayMode &displayMode)
	{
		width = displayMode.width;
		height = displayMode.height;
		frequency = displayMode.frequency;
		bitsPerPixel = displayMode.bitsPerPixel;
		return *this;
	}

	bool operator< (const DisplayMode &displayMode) const
	{
		return width == displayMode.width && height == displayMode.height && bitsPerPixel == displayMode.bitsPerPixel && frequency < displayMode.frequency;
	}

	bool operator== (const DisplayMode &displayMode) const
	{
		return width == displayMode.width && height == displayMode.height && bitsPerPixel == displayMode.bitsPerPixel;
	}

	unsigned getBPP()
	{ return bitsPerPixel; }

	void toString(bool bpp, TCHAR *s)
	{
		if (bpp)
			swprintf(s, L"%dx%dx%d %d Hz", width, height, bitsPerPixel, frequency);
		else
			swprintf(s, L"%dx%d %d Hz", width, height, frequency);
	}

	void findMaxFreq()
	{
		DEVMODE devMode;
		int i = 0;
		frequency = 0;

		while (EnumDisplaySettings(NULL, i++, &devMode))
		{
			DisplayMode displayMode(devMode);

			if (*this < displayMode)
				frequency = devMode.dmDisplayFrequency;
		}
	}

	long apply()
	{
		if (!bitsPerPixel)
		{
			DisplayMode currentDisplayMode;
			currentDisplayMode.getCurrentMode();
			bitsPerPixel = currentDisplayMode.bitsPerPixel;
		}

		if (!frequency)
			findMaxFreq();

		DEVMODE devMode;

		ZeroMemory(&devMode, sizeof(DEVMODE));

		devMode.dmSize = sizeof(DEVMODE);
		devMode.dmPelsWidth = width;
		devMode.dmPelsHeight = height;
		devMode.dmBitsPerPel = bitsPerPixel;
		devMode.dmDisplayFrequency = frequency;
		devMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;
		
		long result = ChangeDisplaySettings(&devMode, CDS_UPDATEREGISTRY);
		
		if (result == DISP_CHANGE_NOTUPDATED)
			result = ChangeDisplaySettings(&devMode, 0);

		return result;
	}

	void getCurrentMode()
	{
		HDC hdcScreen = CreateDC(L"DISPLAY", NULL, NULL, NULL);

		width = GetDeviceCaps(hdcScreen, HORZRES);
		height = GetDeviceCaps(hdcScreen, VERTRES);
		frequency = GetDeviceCaps(hdcScreen, VREFRESH);
		bitsPerPixel = GetDeviceCaps(hdcScreen, BITSPIXEL);

		DeleteDC(hdcScreen);
	}
};

struct DisplayMode_Less
{
	bool operator() (const DisplayMode &displayMode1, const DisplayMode &displayMode2)
	{
		return displayMode1.width == displayMode2.width && displayMode1.height == displayMode2.height && displayMode1.bitsPerPixel == displayMode2.bitsPerPixel && displayMode1.frequency < displayMode2.frequency;
	}
};

class DisplayModeList
{
private:

	vector<DisplayMode> m_displayModeList;
	int m_widthRatio;
	int m_heightRatio;

	void clear()
	{
		m_displayModeList.clear();
	}

	bool contains(DEVMODE devMode)
	{
		DisplayMode displayMode(devMode);
		vector<DisplayMode>::iterator i;

		for (i = m_displayModeList.begin(); i != m_displayModeList.end(); i++)
		{
			if (*i == displayMode)
				return true;
		}

		return false;
	}

	void add(DEVMODE devMode)
	{
		DisplayMode displayMode(devMode);
		vector<DisplayMode>::iterator i;

		for (i = m_displayModeList.begin(); i != m_displayModeList.end(); i++)
		{
			if (*i < displayMode)
			{
				*i = displayMode;
				return;
			}
		}
		
		m_displayModeList.push_back(displayMode);
	}

	void sort()
	{
		std::sort(m_displayModeList.begin(), m_displayModeList.end(), DisplayMode_Less());
	}

public:

	DisplayModeList(int widthRatio, int heightRatio)
	{
		m_widthRatio = widthRatio;
		m_heightRatio = heightRatio;
	}

	int indexOf(DisplayMode displayMode) const
	{
		vector<DisplayMode>::const_iterator i;
		for (i = m_displayModeList.begin(); i != m_displayModeList.end() && !(*i == displayMode); i++) ;
		
		if (i == m_displayModeList.end())
			return -1;
		else
			return (int)(i - m_displayModeList.begin());
	}

	int count() const
	{
		return (int)m_displayModeList.size();
	}

	void toString(int index, bool bpp, TCHAR *s)
	{
		m_displayModeList[index].toString(bpp, s);
	}

	int gcd(int a, int b)
	{
		return (b == 0) ? a : gcd (b, a % b);
	}

	void getModes()
	{
		clear();

		DisplayMode currentDisplayMode;

		currentDisplayMode.getCurrentMode();
		unsigned BPP = currentDisplayMode.getBPP();

		DEVMODE devMode;
		int i = 0;

		while (EnumDisplaySettings(NULL, i++, &devMode))
		{
			if (devMode.dmBitsPerPel != BPP)
				continue;

			if (contains(devMode))
				continue;

			float ratio = gcd(devMode.dmPelsWidth, devMode.dmPelsHeight);
			int widthRatio = devMode.dmPelsWidth / ratio;
			int heightRatio = devMode.dmPelsHeight / ratio;

			//if (devMode.dmBitsPerPel == BPP && devMode.dmPelsWidth >= Settings::MinHorzRes && (devMode.dmPelsWidth * 3 == devMode.dmPelsHeight * 4 || Settings::DisplayNonStdRatio))
			//if (devMode.dmBitsPerPel == BPP && devMode.dmPelsWidth >= Settings::MinHorzRes)

			if (m_widthRatio == -1 && m_heightRatio == -1)
			{
				bool resFound = false;

				for (int i = 0; i < RESOLUTION_RATIO_COUNT; i++)
				{
					if (widthRatio == g_resolutionRatio[i].ratioWidth && heightRatio == g_resolutionRatio[i].ratioHeight)
						resFound = true;
				}

				if (!resFound)
					add(devMode);

				continue;
			}

			if (widthRatio != m_widthRatio || heightRatio != m_heightRatio)
				continue;

			//TCHAR buf[256];
			//swprintf(buf, L"%d:%d %dx%dx%d@%d\n", widthRatio, heightRatio, devMode.dmPelsWidth, devMode.dmPelsHeight, devMode.dmBitsPerPel, devMode.dmDisplayFrequency);
			//OutputDebugString(buf);

			add(devMode);
		}

		sort();
	}

	void apply(int index)
	{
		m_displayModeList[index].apply();
	}
};

DisplayModeList *g_displayModeList[RESOLUTION_RATIO_COUNT];

class CmdLine
{
public:
	static int analyzeArgs(DisplayMode &displayMode)
	{
		LPWSTR *argv;
		int argc;
		TCHAR errormsg[300];

		argv = CommandLineToArgvW(GetCommandLine(), &argc);

		int start = 0;  // 0: menu  1: exit  2: change mode then exit

		if (!start && argc > 2)
		{
			MessageBox(NULL, L"Too many parameters", L"Error", MB_OK);
			start = 1;  // exit after showing the dialog box
		}

		if (argc == 2)
		{
			initConsole();

			if (!start && (!wcscmp(argv[1], L"-?") || !wcscmp(argv[1], L"/?")))
			{
				writeConsole(L"ChangeRes v1.0 - by pallosp / headkaze\n");
				writeConsole(L"ChangeRes -safe\n");
				writeConsole(L"      Sets the Display Mode to 640x480x32@60\n");
				writeConsole(L"ChangeRes <width>x<height>[x<bpp>][@<freq>]\n");
				writeConsole(L"      Sets a Custom Display Mode\n");
				writeConsole(L"ChangeRes -list\n");
				writeConsole(L"      Lists the Display Modes\n");

				start = 1;  // exit after showing the dialog box
			}

			if (!start && !wcsicmp(argv[1], L"-list"))
			{
				for (int i = 0; i < RESOLUTION_RATIO_COUNT; i++)
					outputResolutions(g_displayModeList[i], g_resolutionRatio[i].ratioWidth, g_resolutionRatio[i].ratioHeight);

				start = 1;
			}

			if (!start && !wcsicmp(argv[1], L"-safe"))
			{
				displayMode.setSafe();
				start = 2;
			}

			if (!start)
			{
				switch (displayMode.parseString(argv[1]))
				{
					case DisplayMode::PARSE_ERROR:
						writeConsole(L"Invalid Parameter: %ls\n", argv[1]);
						start = 1;
						break;
					case DisplayMode::RANGE_ERROR:
						writeConsole(L"Range Error in Parameter: %ls\n", argv[1]);
						start = 1;
						break;
					default:  // success
						start = 2;
						break;
				}
			}
		
			freeConsole();
		}

		LocalFree(argv);

		return start;
	}

	static void outputResolutions(DisplayModeList *displayModeList, int widthRatio, int heightRatio)
	{
		displayModeList->getModes();

		if (displayModeList->count() == 0)
			return;

		TCHAR resName[32];

		if (widthRatio == -1 && heightRatio == -1)
			writeConsole(L"Other\n");
		else
			writeConsole(L"%d:%d\n", widthRatio, heightRatio);

		for (int i = 0; i < displayModeList->count(); i++)
		{
			displayModeList->toString(i, false, resName);
			writeConsole(L"%ls\n", resName);
		}
	}

private:

	static HANDLE m_stdOutHandle;

	static void initConsole()
	{
		if (AttachConsole(ATTACH_PARENT_PROCESS))
		{
			m_stdOutHandle = GetStdHandle(STD_OUTPUT_HANDLE);

			writeConsole(L"\n");
		}
	}

	static void writeConsole(TCHAR *text, ...)
	{
		if (m_stdOutHandle)
		{
			TCHAR buf[1024];
			va_list argptr;
			va_start(argptr, text);
			wvsprintf(buf, text, argptr);
			va_end(argptr);
			WriteConsole(m_stdOutHandle, buf, wcslen(buf), NULL, NULL);
		}
	}

	static void freeConsole()
	{
		if (m_stdOutHandle)
		{
			::SendMessage(GetConsoleWindow(), WM_CHAR, VK_RETURN, 0);

			FreeConsole();
		}
	}
};

HANDLE CmdLine::m_stdOutHandle = NULL;

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	MSG msg;
	WNDCLASSEX wndclass;
	DisplayMode displayMode;
	int retVal = 0;

	for (int i = 0; i < RESOLUTION_RATIO_COUNT; i++)
		g_displayModeList[i] = new DisplayModeList(g_resolutionRatio[i].ratioWidth, g_resolutionRatio[i].ratioHeight);

	int start = CmdLine::analyzeArgs(displayMode);

	if (start == 0)
	{
		// fut-e már az alkalmazás egy példányban?
		g_hMutex = CreateMutex(NULL, true, L"ChangeRes");
		
		if (GetLastError() == ERROR_ALREADY_EXISTS)
			goto exit;

		Settings::loadFromRegistry();

		g_hInstance = hInstance;

		wndclass.cbSize        = sizeof(WNDCLASSEX);
		wndclass.style         = CS_HREDRAW | CS_VREDRAW;
		wndclass.lpfnWndProc   = WndProc;
		wndclass.cbClsExtra    = 0;
		wndclass.cbWndExtra    = 0;
		wndclass.hInstance     = hInstance;
		wndclass.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CHANGERES));
		wndclass.hCursor       = LoadCursor(hInstance, IDC_ARROW);
		wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
		wndclass.lpszMenuName  = NULL;
		wndclass.lpszClassName = g_szAppName;
		wndclass.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CHANGERES));

		RegisterClassEx(&wndclass);

		CreateWindow(g_szAppName, NULL, WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

		while (GetMessage(&msg, NULL, 0, 0))
			DispatchMessage(&msg);
		
		retVal = (int)msg.wParam;
	}
	else if (start == 2)
	{
		long result = displayMode.apply();
		
		if (result != DISP_CHANGE_SUCCESSFUL && result != DISP_CHANGE_RESTART)
			MessageBox(NULL, L"Display Mode Change Failed", L"Error", MB_OK);
	}

exit:

	for (int i = 0; i < RESOLUTION_RATIO_COUNT; i++)
		delete g_displayModeList[i];

	return retVal;
}

void ApplyResolution(DisplayModeList *displayModeList, WPARAM wParam, UINT uIDCheckItem)
{
	if (LOWORD(wParam) >= uIDCheckItem && LOWORD(wParam) < uIDCheckItem + displayModeList->count())
		displayModeList->apply(LOWORD(wParam) - uIDCheckItem);
}

void BuildMenu(HMENU popupMenu, DisplayModeList *displayModeList, int widthRatio, int heightRatio, UINT uIDSubMenu)
{
	int i = 0;
	TCHAR ratioName[32];
	TCHAR resName[32];

	if (displayModeList->count() == 0)
		return;

	HMENU displayModeMenu = CreateMenu();

	for (i = 0; i < displayModeList->count(); i++)
	{
		displayModeList->toString(i, false, resName);
		AppendMenu(displayModeMenu, 0, uIDSubMenu + i, resName);
	}

	if (widthRatio == -1 && heightRatio == -1)
		swprintf(ratioName, L"Other");
	else
		swprintf(ratioName, L"%d:%d", widthRatio, heightRatio);

	AppendMenu(popupMenu, MF_POPUP, (UINT_PTR)displayModeMenu, ratioName);
	
	DisplayMode currentDisplayMode;
	currentDisplayMode.getCurrentMode();
	
	if ((i = displayModeList->indexOf(currentDisplayMode)) >= 0)
		CheckMenuItem(displayModeMenu, uIDSubMenu + i, MF_CHECKED);
}

HMENU BuildMenu()
{
	int i;
	HMENU popupMenu, settingsMenu;
	popupMenu = CreatePopupMenu();
	settingsMenu = CreateMenu();

	// Settings menü belseje
	//AppendMenu(settingsMenu, 0, MNU_LORES, L"Display low resolutions");
	
	//if (Settings::MinHorzRes < DEFAULT_MIN_HORZ_RES)
	//	CheckMenuItem(settingsMenu, MNU_LORES, MF_CHECKED);
	
	//AppendMenu(settingsMenu, 0, MNU_NONSTD_RATIO, L"Display not 4/3 resolutions");
	
	//if (Settings::DisplayNonStdRatio)
	//	CheckMenuItem(settingsMenu, MNU_NONSTD_RATIO, MF_CHECKED);
	
	AppendMenu(settingsMenu, 0, MNU_AUTOSTART, L"Autostart");
	
	if (Settings::AutoStart)
		CheckMenuItem(settingsMenu, MNU_AUTOSTART, MF_CHECKED);
	
	AppendMenu(settingsMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(settingsMenu, 0, MNU_SAVE_SETTINGS, L"Save settings");

	// Fõmenü
	AppendMenu(popupMenu, MF_POPUP, (UINT_PTR)settingsMenu, L"Settings");
	AppendMenu(popupMenu, MF_SEPARATOR, 0, NULL);

	UINT uIDMenu = MNU_RESOLUTIONS;

	for (int i = 0; i < RESOLUTION_RATIO_COUNT; i++)
	{
		BuildMenu(popupMenu, g_displayModeList[i], g_resolutionRatio[i].ratioWidth, g_resolutionRatio[i].ratioHeight, uIDMenu);

		uIDMenu += g_displayModeList[i]->count();
	}

	AppendMenu(popupMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(popupMenu, 0, MNU_EXIT, L"Exit");

	return popupMenu;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	POINT cursorPoint;
	HMENU popupMenu;
	static NOTIFYICONDATA notifyIconData;

	switch (iMsg)
	{
	case WM_CREATE:
		for (int i = 0; i < RESOLUTION_RATIO_COUNT; i++)
			g_displayModeList[i]->getModes();

		notifyIconData.cbSize = sizeof(NOTIFYICONDATA);

		wcscpy(notifyIconData.szTip, g_szAppName);
		
		//ExtractIconEx(L"ChangeRes.ico", 0, NULL, &notifyIconData.hIcon, 1);
		
		notifyIconData.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_CHANGERES));
		notifyIconData.hWnd = hwnd;
		notifyIconData.uID = IDI_CHANGERES;
		notifyIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
		notifyIconData.uCallbackMessage = MSG_POPUP;
		
		Shell_NotifyIcon(NIM_ADD, &notifyIconData);
		
		DestroyIcon(notifyIconData.hIcon);

		return 0;

	case MSG_POPUP:
		if (lParam == WM_LBUTTONDOWN || lParam == WM_RBUTTONDOWN)
		{
			GetCursorPos(&cursorPoint);
			popupMenu = BuildMenu();
			// tûnjön el a menü, ha mellé kattintunk
			SetForegroundWindow(hwnd);
			TrackPopupMenu(popupMenu, 0, cursorPoint.x, cursorPoint.y, 0, hwnd, 0);
			PostMessage(hwnd, WM_NULL, 0, 0);
			DestroyMenu(popupMenu);
		}

		return 0;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case MNU_EXIT:
			SendMessage(hwnd, WM_CLOSE, 0, 0);
			break;
		//case MNU_LORES:
		//	Settings::MinHorzRes = Settings::MinHorzRes ? 0 : DEFAULT_MIN_HORZ_RES;
		//	g_displayModeList.getModes();
		//	break;
		//case MNU_NONSTD_RATIO:
		//	Settings::DisplayNonStdRatio = !Settings::DisplayNonStdRatio;
		//	g_displayModeList.getModes();
		//	break;
		case MNU_AUTOSTART:
			Settings::AutoStart = !Settings::AutoStart;
			Settings::saveToRegistry(false);
			break;
		case MNU_SAVE_SETTINGS:
			Settings::saveToRegistry(true);
			break;
		default:
			UINT uIDMenu = MNU_RESOLUTIONS;

			for (int i = 0; i < RESOLUTION_RATIO_COUNT; i++)
			{
				ApplyResolution(g_displayModeList[i], wParam, uIDMenu);

				uIDMenu += g_displayModeList[i]->count();
			}

			break;
		}

		return 0;

	case WM_DESTROY:
		if (g_hMutex)
		{
			ReleaseMutex(g_hMutex);
			g_hMutex = NULL;
		}

		Shell_NotifyIcon(NIM_DELETE, &notifyIconData);
		PostQuitMessage(0);
		
		return 0;
	}

	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}