#include <windows.h>
#include "game.h"
#include "tray.h"
#include "dashboard.h"

static constexpr UINT WM_TRAY    = WM_APP + 1;
static constexpr UINT TIMER_TICK = 1;
static constexpr UINT TICK_MS    = 5000;
static constexpr UINT IDM_STATUS = 1001;
static constexpr UINT IDM_QUIT   = 1002;
static constexpr UINT IDI_APPICON = 101;

static GameState g_state;

// 트레이 툴팁도 위장 모드 상태를 그대로 반영 — 기본은 정직하게 "Text RPG",
// 위장 모드를 켰을 때만 "sync agent"로 바뀐다.
static void UpdateTrayTooltip() {
    const wchar_t* brand = g_state.disguiseMode ? L"sync agent" : L"Text RPG";
    wchar_t tip[128];
    if (g_state.activeHero >= 0) {
        const Hero& h = g_state.Active();
        swprintf_s(tip, L"%s  •  Lv.%d  •  %lld G", brand, h.level, h.gold);
    } else {
        swprintf_s(tip, L"%s", brand);
    }
    TraySetTooltip(tip);
}

// 프라이버시 모드가 바뀌었을 때만 트레이 아이콘을 교체 (매 프레임 LoadIconW를
// 다시 부르지 않도록 변경 여부를 추적).
static bool g_trayIconInit = false;
static bool g_lastPrivacyMode = false;
static void SyncTrayIconIfChanged() {
    if (!g_trayIconInit || g_state.disguiseMode != g_lastPrivacyMode) {
        TraySetIcon(g_state.disguiseMode);
        g_lastPrivacyMode = g_state.disguiseMode;
        g_trayIconInit = true;
    }
}

static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (wp == TIMER_TICK) {
            // 백그라운드 실행이 꺼져 있으면 대시보드가 안 보일 때 성장을 멈춘다
            // (강제종료 없이 유저가 직접 껐다 켰다 할 수 있게 하는 옵션).
            if (g_state.backgroundEnabled || DashboardIsVisible()) {
                g_state.totalRunSec += TICK_MS / 1000.0; // 위장 가동 시간 누적
                std::wstring notify = GameTick(g_state);
                if (!notify.empty())
                    TrayNotify(L"Background sync", notify);
                SaveGame(g_state);
            }
            UpdateTrayTooltip();
            SyncTrayIconIfChanged();
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
    // 중복 실행 방지 — 이미 떠 있는 인스턴스가 있으면 그냥 조용히 종료.
    // (안 그러면 옛 인스턴스와 새 인스턴스가 같은 세이브 파일을 5초마다 번갈아
    // 덮어써서 진행 상황이 무작위로 섞이거나 사라지는 사고가 난다.)
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"SyncAgentSingleInstanceMutex");
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    // PC는 스토어 심사 정책이 없어서(사이드로드 배포) 안드로이드처럼 "기본은 정직하게"
    // 강제할 필요가 없음 — 신규 설치 시엔 곧바로 프라이버시 모드로 시작. 세이브 파일이
    // 있으면(기존 유저) LoadGame이 저장된 값으로 덮어써서 유저가 직접 끈 상태는 유지됨.
    g_state.disguiseMode = true;
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
    SyncTrayIconIfChanged(); // 저장된 프라이버시 모드 상태를 시작하자마자 반영
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
            g_state.dashboardOpenSec += (double)frameInterval / (double)freq.QuadPart; // 노출 시간 누적
            lastFrame = now;
            DashboardFrame(g_state);
            SyncTrayIconIfChanged(); // 대시보드에서 방금 토글했으면 즉시 반영
        } else {
            // 대시보드가 닫혀있을 때: GetMessage로 블로킹 (CPU 0%)
            if (!GetMessageW(&m, nullptr, 0, 0)) break;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return 0;
}
