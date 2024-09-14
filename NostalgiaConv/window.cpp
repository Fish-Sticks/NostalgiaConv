#include <Windows.h>
#include <iostream>
#include "window.hpp"

// Thanks ChatGPT, win32 API is literally the most horrible thing I've ever seen.
// This is the only part I used AI on, I don't use AI to code ever but I'm not wasting my time with win32 API.

#define ID_SELECTALL 1
#define ID_COPY      2
#define ID_PASTE     3

#define ID_EXECUTE   4
#define ID_CLEAR     5
#define ID_CONSOLE   6

static HACCEL hAccelTable;

ACCEL accelTable[] = {
    {FCONTROL | FVIRTKEY, 'A', ID_SELECTALL},
    {FCONTROL | FVIRTKEY, 'C', ID_COPY},
    {FCONTROL | FVIRTKEY, 'V', ID_PASTE}
};

void CallConsoleFunction() {
    static bool showConsole = false;
    showConsole = !showConsole;
    ShowWindow(GetConsoleWindow(), showConsole ? SW_SHOW : SW_HIDE);
}

static executefn_t ExecuteFunc;
static HWND robloxWindow;
LRESULT CALLBACK NostalgiaWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hTextBox;
    static HWND hExecuteButton;
    static HWND hClearButton;
    static HWND hConsoleButton;
    static HBRUSH hPinkBrush;

    switch (msg) {
    case WM_CREATE: {
        hTextBox = CreateWindowEx(
            WS_EX_CLIENTEDGE, "EDIT", "-- Enter script here",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
            10, 10, 760, 540,
            hwnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL
        );

        if (hTextBox == NULL) {
            MessageBox(hwnd, "Failed to create text box!", "Error", MB_ICONERROR);
            return -1;
        }

        hPinkBrush = CreateSolidBrush(RGB(61, 112, 255));
        if (hPinkBrush == NULL) {
            MessageBox(hwnd, "Failed to create pink brush!", "Error", MB_ICONERROR);
            return -1;
        }

        hExecuteButton = CreateWindow(
            "BUTTON", "EXECUTE",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            10, 560, 100, 30,
            hwnd, (HMENU)ID_EXECUTE, ((LPCREATESTRUCT)lParam)->hInstance, NULL
        );

        hClearButton = CreateWindow(
            "BUTTON", "CLEAR",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            120, 560, 100, 30,
            hwnd, (HMENU)ID_CLEAR, ((LPCREATESTRUCT)lParam)->hInstance, NULL
        );

        hConsoleButton = CreateWindow(
            "BUTTON", "CONSOLE",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            230, 560, 100, 30,
            hwnd, (HMENU)ID_CONSOLE, ((LPCREATESTRUCT)lParam)->hInstance, NULL
        );

        if (hExecuteButton == NULL || hClearButton == NULL || hConsoleButton == NULL) {
            MessageBox(hwnd, "Failed to create buttons!", "Error", MB_ICONERROR);
            return -1;
        }

        hAccelTable = CreateAcceleratorTable(accelTable, sizeof(accelTable) / sizeof(ACCEL));
        if (hAccelTable == NULL) {
            MessageBox(hwnd, "Failed to create accelerator table!", "Error", MB_ICONERROR);
            return -1;
        }

        SendMessageA(hTextBox, EM_SETLIMITTEXT, (WPARAM)1000000, 0);

        return 0;
    }
    case WM_SYSCOMMAND: {
        if (wParam == SC_MINIMIZE) {
            // When minimized, focus on the Roblox window to prevent beeping
            if (robloxWindow) {
                SetForegroundWindow(robloxWindow);
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        SetWindowPos(hTextBox, NULL, 10, 10, width - 20, height - 60, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hExecuteButton, NULL, 10, height - 40, 100, 30, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hClearButton, NULL, 120, height - 40, 100, 30, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hConsoleButton, NULL, 230, height - 40, 100, 30, SWP_NOZORDER | SWP_NOACTIVATE);
        break;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* pMinMax = (MINMAXINFO*)lParam;
        pMinMax->ptMinTrackSize.x = 400;
        pMinMax->ptMinTrackSize.y = 300;
        break;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND hEdit = (HWND)lParam;
        SetBkColor(hdc, RGB(61, 112, 255));
        SetTextColor(hdc, RGB(0, 0, 0));
        return (LRESULT)hPinkBrush;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HBRUSH hBrush = CreateSolidBrush(RGB(255, 0, 102));
        FillRect(hdc, &ps.rcPaint, hBrush);
        DeleteObject(hBrush);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_SELECTALL:
            SendMessage(hTextBox, EM_SETSEL, 0, -1);
            break;
        case ID_COPY:
            SendMessage(hTextBox, WM_COPY, 0, 0);
            break;
        case ID_PASTE:
            SendMessage(hTextBox, WM_PASTE, 0, 0);
            break;
        case ID_EXECUTE: {
            int length = GetWindowTextLength(hTextBox);
            if (length > 0) {
                char* textBuffer = new char[length + 1];
                GetWindowText(hTextBox, textBuffer, length + 1);
                ExecuteFunc(textBuffer);
                delete[] textBuffer;
            }
            break;
        }
        case ID_CLEAR:
            SetWindowText(hTextBox, "");
            break;
        case ID_CONSOLE:
            CallConsoleFunction();
            break;
        }
        break;
    }
    case WM_DESTROY: {
        if (hPinkBrush) {
            DeleteObject(hPinkBrush);
        }
        PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return 0;
}

void SetupWindow(executefn_t executefn) {
    robloxWindow = FindWindowW(NULL, L"Roblox");
    ExecuteFunc = executefn;

    WNDCLASSA wndClass = { };
    wndClass.lpszClassName = "nostalgiaconvwnd";
    wndClass.lpfnWndProc = NostalgiaWindowProc;
    wndClass.hInstance = GetModuleHandle(NULL);
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);

    ATOM classAtom = RegisterClassA(&wndClass);
    if (!classAtom) {
        MessageBox(NULL, "Failed to register class atom!", "Error", MB_ICONERROR);
        return;
    }

    HWND window = CreateWindowExA(
        WS_EX_APPWINDOW, "nostalgiaconvwnd", "NostalgiaConv",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        400, 400, 800, 600, robloxWindow, NULL, GetModuleHandle(NULL), NULL
    );

    if (window == NULL) {
        MessageBox(NULL, "Failed to create window!", "Error", MB_ICONERROR);
        return;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(window, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

BOOL WINAPI CtrlHandler(DWORD dwCtrlType) {
    return TRUE;
}

void MakeConsole(const std::string& title) {
    DWORD oldProt = 0;
    VirtualProtect(&FreeConsole, 1, PAGE_EXECUTE_READWRITE, &oldProt);
    *reinterpret_cast<std::uint8_t*>(&FreeConsole) = 0xC3;
    VirtualProtect(&FreeConsole, 1, oldProt, &oldProt);

    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONIN$", "r", stdin);

    SetConsoleTitleA(title.c_str());

    SetConsoleCtrlHandler(&CtrlHandler, TRUE);
}