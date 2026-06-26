#include "tray.h"
#include "lang.h"
#include <shellapi.h>

static NOTIFYICONDATAW g_nid = {};

static constexpr UINT IDI_APPICON = 101;

void TrayInit(HWND hwnd, UINT callbackMsg, HINSTANCE hInst) {
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = callbackMsg;
    g_nid.hIcon            = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"sync agent");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void TrayDestroy() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

void TraySetTooltip(const std::wstring& tip) {
    g_nid.uFlags = NIF_TIP;
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void TrayNotify(const std::wstring& title, const std::wstring& body) {
    g_nid.uFlags     = NIF_INFO;
    g_nid.dwInfoFlags = NIIF_NONE;
    wcsncpy_s(g_nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(g_nid.szInfo,      body.c_str(),  _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void TrayShowContextMenu(HWND hwnd, UINT cmdStatus, UINT cmdQuit) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING,    cmdStatus, TW(L"상태 대시보드", L"Status Dashboard"));
    AppendMenuW(menu, MF_SEPARATOR, 0,         nullptr);
    AppendMenuW(menu, MF_STRING,    cmdQuit,   TW(L"종료", L"Exit"));
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}
