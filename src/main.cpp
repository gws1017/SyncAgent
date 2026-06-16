#include <windows.h>
#include "game.h"
#include "tray.h"
#include "dashboard.h"

static constexpr UINT WM_TRAY    = WM_APP + 1;
static constexpr UINT TIMER_TICK = 1;
static constexpr UINT TICK_MS    = 3000;
static constexpr UINT IDM_STATUS = 1001;
static constexpr UINT IDM_QUIT   = 1002;

static GameState g_state;

static void UpdateTrayTooltip() {
    wchar_t tip[128];
    swprintf_s(tip, L"sync agent  •  Lv.%d  •  %lld G",
               g_state.level, g_state.gold);
    TraySetTooltip(tip);
}

static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (wp == TIMER_TICK) {
            std::wstring notify = GameTick(g_state);
            if (!notify.empty())
                TrayNotify(L"Background sync", notify);
            UpdateTrayTooltip();
            SaveGame(g_state);
        }
        return 0;

    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            TrayShowContextMenu(hwnd, IDM_STATUS, IDM_QUIT);
            break;
        case WM_LBUTTONDBLCLK:
            DashboardToggle();
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_STATUS: DashboardToggle(); return 0;
        case IDM_QUIT:   DestroyWindow(hwnd); return 0;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_TICK);
        TrayDestroy();
        DashboardDestroy();
        SaveGame(g_state);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    LoadGame(g_state);

    const wchar_t* cls = L"IdleGameMsg";
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = MsgWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = cls;
    RegisterClassW(&wc);

    HWND msgHwnd = CreateWindowW(cls, nullptr, 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!msgHwnd) return 1;

    TrayInit(msgHwnd, WM_TRAY);
    UpdateTrayTooltip();
    TrayNotify(L"Background sync", L"[sync] agent started");

    if (!DashboardInit(hInst)) return 1;

    SetTimer(msgHwnd, TIMER_TICK, TICK_MS, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        DashboardFrame(g_state);
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
