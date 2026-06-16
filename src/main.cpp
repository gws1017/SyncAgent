#include <windows.h>
#include "game.h"
#include "tray.h"
#include "dashboard.h"

static constexpr UINT WM_TRAY    = WM_APP + 1;
static constexpr UINT TIMER_TICK = 1;
static constexpr UINT TICK_MS    = 3000;
static constexpr UINT IDM_STATUS = 1001;
static constexpr UINT IDM_QUIT   = 1002;
static constexpr UINT IDI_APPICON = 101;

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

    const wchar_t* cls = L"SyncAgentMsg";
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = MsgWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = cls;
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    RegisterClassW(&wc);

    HWND msgHwnd = CreateWindowW(cls, nullptr, 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!msgHwnd) return 1;

    TrayInit(msgHwnd, WM_TRAY, hInst);
    UpdateTrayTooltip();
    TrayNotify(L"Background sync", L"[sync] agent started");

    if (!DashboardInit(hInst)) return 1;

    SetTimer(msgHwnd, TIMER_TICK, TICK_MS, nullptr);

    // 고해상도 타이머로 ~60fps 프레임 캡
    LARGE_INTEGER freq, lastFrame;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastFrame);
    const long long frameInterval = freq.QuadPart / 60;

    MSG m;
    bool running = true;
    while (running) {
        if (DashboardIsVisible()) {
            // 대시보드가 열려있을 때: PeekMessage로 메시지 소진 후 렌더
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            if (!running) break;

            // 60fps 프레임 캡 — 아직 시간이 안 됐으면 잠깐 양보
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            long long elapsed = now.QuadPart - lastFrame.QuadPart;
            if (elapsed < frameInterval) {
                DWORD sleepMs = (DWORD)((frameInterval - elapsed) * 1000 / freq.QuadPart);
                if (sleepMs > 1) Sleep(sleepMs - 1);
                continue;
            }
            lastFrame = now;
            DashboardFrame(g_state);
        } else {
            // 대시보드가 닫혀있을 때: GetMessage로 블로킹 (CPU 0%)
            if (!GetMessageW(&m, nullptr, 0, 0)) break;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return 0;
}
