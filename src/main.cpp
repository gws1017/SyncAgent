#include <windows.h>
#include "game.h"
#include "tray.h"
#include "dashboard.h"
#include "platform.h"
#include "cloud_sync.h"
#include <random>

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

// 세션 소유권 체크용 랜덤 ID — 이 프로세스가 살아있는 동안만 유효(재시작마다 새로 생성).
// 같은 계정(코드)으로 다른 기기에서 로그인하면 클라우드의 activeSession 값이
// 바뀌어서, 여기 담긴 값과 달라지는 순간 이 세션이 밀려난 걸 알 수 있다.
static std::string g_mySessionId;

static std::string GenerateSessionId() {
    std::mt19937_64 rng(std::random_device{}());
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)rng());
    return buf;
}

// 로그인 직후 또는(이미 연동된 상태로) 앱을 새로 켰을 때 호출 — 내 세션 ID로
// 클라우드 소유권을 덮어써서 "내가 최신"이라고 주장한다(다른 기기가 켜져 있었다면 밀어냄).
static void ClaimCloudSessionIfLinked() {
    if (!g_state.googleLinked) return;
    std::string code = CloudGetSavedCode();
    if (code.empty()) return;
    g_mySessionId = GenerateSessionId();
    CloudClaimSession(code, g_mySessionId);
}

// Google 계정 연동(google_auth_win.cpp) 완료 결과를 매 틱마다 논블로킹으로 확인.
// 안드로이드의 PollGoogleLinkResult()와 동일한 로직 — dashboard.cpp의 수동
// "지금 다운로드" 버튼과도 같은 방식(기존 세이브 있으면 교체, 없으면 업로드).
static void PollGoogleLinkResultIfAny() {
    std::string code;
    if (!PlatformPollGoogleLinkResult(code)) return;
    if (code.empty() || code == "__FAILED__") return;

    CloudSetCode(code);
    std::string downloaded;
    CloudSyncResult dl = CloudDownload(code, downloaded);
    GameState fresh{};
    if (dl.ok && DeserializeGameState(downloaded, fresh)) {
        g_state = fresh; // 이미 이 계정으로 저장된 세이브가 있으면 그대로 교체
    } else {
        CloudUpload(code, SerializeGameState(g_state)); // 새 계정이면 지금 세이브를 올림
    }
    g_state.googleLinked = true;
    GrantGoogleLinkReward(g_state);
    SaveGame(g_state);
    ClaimCloudSessionIfLinked(); // 방금 연동했으니 세션 소유권도 바로 주장
}

// Google 계정 연동 중이면 이 주기마다 클라우드에 조용히 자동 업로드(안드로이드와 동일 주기).
// 업로드 전에 세션 소유권부터 확인 — 다른 기기가 나중에 로그인해서 내 세션을
// 가져갔으면(같은 계정 동시 사용 시 세이브가 갈라지는 것 방지) 알림 띄우고 종료.
static constexpr double CLOUD_AUTO_UPLOAD_SEC = 120.0;
static double g_lastCloudUploadRunSec = 0.0;
static void AutoUploadIfLinked(HWND hwnd) {
    if (!g_state.googleLinked) return;
    if (g_state.totalRunSec - g_lastCloudUploadRunSec < CLOUD_AUTO_UPLOAD_SEC) return;
    g_lastCloudUploadRunSec = g_state.totalRunSec;
    std::string code = CloudGetSavedCode();
    if (code.empty()) return;

    if (!g_mySessionId.empty()) {
        std::string remoteSession;
        CloudSyncResult chk = CloudGetActiveSession(code, remoteSession);
        if (chk.ok && !remoteSession.empty() && remoteSession != g_mySessionId) {
            MessageBoxW(hwnd,
                L"다른 기기에서 같은 계정으로 로그인되어 이 세션은 종료됩니다.",
                L"Text RPG", MB_OK | MB_ICONWARNING);
            DestroyWindow(hwnd); // 기존 종료 경로(WM_DESTROY)를 그대로 타서 정상적으로 정리하고 끝냄
            return;
        }
    }
    CloudUpload(code, SerializeGameState(g_state));
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
            PollGoogleLinkResultIfAny();
            AutoUploadIfLinked(hwnd);
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
    ClaimCloudSessionIfLinked(); // 이미 연동돼 있었으면 이번 실행이 새 활성 세션이라고 주장(다른 기기 밀어냄)

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
