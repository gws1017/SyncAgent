// idlegame v0 — tray-resident idle game.
//
// Lives in the system tray, disguised as a background "sync" utility.
// A hero passively gains XP and gold on a timer; level-ups and rare item
// drops surface as Windows balloon notifications styled like log lines.
// No game window yet — that arrives in v1 (Dear ImGui dashboard).

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>      // SHGetFolderPath
#include <cstdio>
#include <cstdint>
#include <string>
#include <random>

// ---- Window message / command ids ----------------------------------------
static constexpr UINT WM_TRAY = WM_APP + 1;   // tray icon callback
static constexpr UINT TIMER_TICK = 1;          // game tick
static constexpr UINT TICK_MS = 3000;          // 3s per tick
static constexpr UINT IDM_STATUS = 1001;       // context menu: show status
static constexpr UINT IDM_QUIT = 1002;         // context menu: quit

// ---- Game state -----------------------------------------------------------
struct GameState {
    int      level = 1;
    long long xp = 0;       // current xp toward next level
    long long gold = 0;
    long long items = 0;    // rare items found
};

static GameState g_state;
static NOTIFYICONDATAW g_nid = {};
static HWND g_hwnd = nullptr;
static std::mt19937 g_rng{ std::random_device{}() };

// XP needed to reach the next level. Grows with level.
static long long XpForNext(int level) {
    return 50LL + (long long)level * 25LL;
}

// ---- Persistence: %APPDATA%\idlegame\save.txt -----------------------------
static std::wstring SavePath() {
    wchar_t appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::wstring dir = std::wstring(appdata) + L"\\idlegame";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\save.txt";
    }
    return L"idlegame_save.txt";
}

static void SaveGame() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, SavePath().c_str(), L"w") == 0 && f) {
        fprintf(f, "%d %lld %lld %lld\n",
                g_state.level, g_state.xp, g_state.gold, g_state.items);
        fclose(f);
    }
}

static void LoadGame() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, SavePath().c_str(), L"r") == 0 && f) {
        if (fscanf_s(f, "%d %lld %lld %lld",
                     &g_state.level, &g_state.xp, &g_state.gold, &g_state.items) != 4) {
            g_state = GameState{};
        }
        fclose(f);
    }
}

// ---- Tray helpers ---------------------------------------------------------
// Show a balloon notification. Title/body are styled like a log line so a
// passing glance reads as a background sync tool rather than a game.
static void Notify(const std::wstring& title, const std::wstring& body) {
    g_nid.uFlags = NIF_INFO;
    g_nid.dwInfoFlags = NIIF_NONE;  // no flashy icon -> looks routine
    wcsncpy_s(g_nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(g_nid.szInfo, body.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void UpdateTooltip() {
    wchar_t tip[128];
    swprintf_s(tip, L"sync agent  •  Lv.%d  •  %lld G",
               g_state.level, g_state.gold);
    g_nid.uFlags = NIF_TIP;
    wcsncpy_s(g_nid.szTip, tip, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// ---- Core idle loop -------------------------------------------------------
static void Tick() {
    // Passive gains scale gently with level.
    long long xpGain   = 5 + g_state.level * 2;
    long long goldGain = 3 + g_state.level;

    g_state.xp   += xpGain;
    g_state.gold += goldGain;

    // Level up (possibly multiple times in one tick).
    while (g_state.xp >= XpForNext(g_state.level)) {
        g_state.xp -= XpForNext(g_state.level);
        g_state.level++;
        wchar_t body[128];
        swprintf_s(body, L"[sync] checkpoint reached — level %d", g_state.level);
        Notify(L"Background sync", body);
    }

    // Rare item drop ~6% per tick.
    std::uniform_int_distribution<int> roll(1, 100);
    if (roll(g_rng) <= 6) {
        g_state.items++;
        std::uniform_int_distribution<int> bonusDist(20, 60);
        long long bonus = bonusDist(g_rng);
        g_state.gold += bonus;
        wchar_t body[128];
        swprintf_s(body, L"[sync] artifact #%lld recovered (+%lld G)",
                   g_state.items, bonus);
        Notify(L"Background sync", body);
    }

    UpdateTooltip();
    SaveGame();
}

// ---- Context menu ---------------------------------------------------------
static void ShowStatus() {
    wchar_t msg[256];
    swprintf_s(msg,
        L"Level: %d\nXP: %lld / %lld\nGold: %lld\nArtifacts: %lld",
        g_state.level, g_state.xp, XpForNext(g_state.level),
        g_state.gold, g_state.items);
    MessageBoxW(nullptr, msg, L"Status", MB_OK | MB_ICONINFORMATION);
}

static void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_STATUS, L"상태 보기");   // 상태 보기
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_QUIT, L"종료");                  // 종료
    // Required so the menu dismisses correctly when clicking elsewhere.
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

// ---- Window procedure -----------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (wp == TIMER_TICK) Tick();
        return 0;

    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
            ShowContextMenu(hwnd);
        } else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            ShowStatus();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_STATUS: ShowStatus(); return 0;
        case IDM_QUIT:   DestroyWindow(hwnd); return 0;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_TICK);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        SaveGame();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- Entry point ----------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    LoadGame();

    const wchar_t* kClass = L"IdleGameTrayWnd";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);

    // Message-only-style hidden window (never shown).
    g_hwnd = CreateWindowW(kClass, L"idlegame", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    // Register the tray icon.
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"sync agent");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    UpdateTooltip();

    SetTimer(g_hwnd, TIMER_TICK, TICK_MS, nullptr);
    Notify(L"Background sync", L"[sync] agent started");

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
