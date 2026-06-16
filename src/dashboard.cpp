#include "dashboard.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

// ---- D3D11 state ----------------------------------------------------------
static ID3D11Device*           g_pd3dDevice      = nullptr;
static ID3D11DeviceContext*    g_pd3dContext      = nullptr;
static IDXGISwapChain*         g_pSwapChain       = nullptr;
static ID3D11RenderTargetView* g_mainRenderTarget = nullptr;

static HWND g_hwnd    = nullptr;
static bool g_visible = false;

static constexpr int W = 460, H = 400;

// wstring → UTF-8 변환 헬퍼 (ImGui는 UTF-8 사용)
static void ToUtf8(const std::wstring& src, char* dst, int dstSize) {
    WideCharToMultiByte(CP_UTF8, 0, src.c_str(), -1, dst, dstSize, nullptr, nullptr);
}

// ---- D3D11 helpers --------------------------------------------------------
static bool CreateDeviceAndSwapChain() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount       = 2;
    sd.BufferDesc.Width  = W;
    sd.BufferDesc.Height = H;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow      = g_hwnd;
    sd.SampleDesc.Count  = 1;
    sd.Windowed          = TRUE;
    sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dContext);
    return SUCCEEDED(hr);
}

static void CreateRenderTarget() {
    ID3D11Texture2D* buf = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&buf));
    g_pd3dDevice->CreateRenderTargetView(buf, nullptr, &g_mainRenderTarget);
    buf->Release();
}

static void CleanupRenderTarget() {
    if (g_mainRenderTarget) { g_mainRenderTarget->Release(); g_mainRenderTarget = nullptr; }
}

// ---- Window procedure -----------------------------------------------------
static LRESULT CALLBACK DashWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pSwapChain && wp != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        g_visible = false;
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- 탭 UI 함수 ------------------------------------------------------------
static void TabStatus(GameState& state) {
    ImGui::Spacing();

    ImGui::TextDisabled("IDLE AGENT  v0.1");
    ImGui::Separator();
    ImGui::Spacing();

    char buf[64];
    snprintf(buf, sizeof(buf), "Lv.%d", state.level);
    ImGui::Text("영웅");      ImGui::SameLine(100); ImGui::Text("%s", buf);

    ImGui::Spacing();
    ImGui::Text("XP");        ImGui::SameLine(100);
    char xpOverlay[32];
    snprintf(xpOverlay, sizeof(xpOverlay), "%lld / %lld", state.xp, state.xpForNext());
    ImGui::SetNextItemWidth(270);
    ImGui::ProgressBar(state.xpProgress(), {-1, 0}, xpOverlay);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("골드");      ImGui::SameLine(100); ImGui::Text("%lld G", state.gold);
    ImGui::Text("유물");      ImGui::SameLine(100); ImGui::Text("%lld 개", state.items);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    char evt[128] = {};
    ToUtf8(state.lastEvent, evt, sizeof(evt));
    ImGui::TextDisabled("최근 이벤트");
    ImGui::TextWrapped("%s", evt);
}

static void TabUpgrade(GameState& state) {
    ImGui::Spacing();

    for (int i = 0; i < UP_COUNT; i++) {
        Upgrade& u = state.upgrades[i];
        ImGui::PushID(i);

        // 이름 + 레벨
        char header[64];
        snprintf(header, sizeof(header), "%s  [%d / %d]", u.name, u.level, u.maxLevel);
        ImGui::Text("%s", header);

        // 설명
        ImGui::SameLine(260);
        ImGui::TextDisabled("%s", u.desc);

        // 레벨 게이지
        ImGui::SetNextItemWidth(260);
        ImGui::ProgressBar((float)u.level / u.maxLevel, {260, 6}, "");

        // 구매 버튼
        ImGui::SameLine();
        bool maxed = (u.level >= u.maxLevel);
        long long cost = GetUpgradeCost(u);
        char btnLabel[32];
        if (maxed) snprintf(btnLabel, sizeof(btnLabel), "MAX");
        else       snprintf(btnLabel, sizeof(btnLabel), "%lld G", cost);

        bool canBuy = !maxed && state.gold >= cost;
        if (!canBuy) ImGui::BeginDisabled();
        if (ImGui::Button(btnLabel, {90, 0}))
            PurchaseUpgrade(state, i);
        if (!canBuy) ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::PopID();
    }
}

static void TabDungeon(GameState& state) {
    Dungeon& d = state.dungeon;
    ImGui::Spacing();

    char stageBuf[32];
    snprintf(stageBuf, sizeof(stageBuf),
             d.bossStage ? "스테이지 %d  [BOSS]" : "스테이지 %d", d.stage);
    ImGui::Text("%s", stageBuf);

    ImGui::Spacing();
    ImGui::Text("적 HP");
    ImGui::SameLine(80);

    float hpPct = (d.enemyMaxHp > 0)
                  ? (float)d.enemyHp / (float)d.enemyMaxHp
                  : 0.0f;

    // 보스는 빨간색, 일반은 기본 파란색
    if (d.bossStage)
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));

    char hpOverlay[32];
    snprintf(hpOverlay, sizeof(hpOverlay), "%lld / %lld", d.enemyHp, d.enemyMaxHp);
    ImGui::SetNextItemWidth(290);
    ImGui::ProgressBar(hpPct, {-1, 0}, hpOverlay);

    if (d.bossStage) ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    long long atk = (long long)state.level * 10;
    ImGui::Text("공격력");  ImGui::SameLine(100); ImGui::Text("%lld / 틱", atk);
    ImGui::Text("스테이지"); ImGui::SameLine(100); ImGui::Text("%d", d.stage);
}

// ---- 공개 API --------------------------------------------------------------
bool DashboardInit(HINSTANCE hInst) {
    const wchar_t* cls = L"IdleGameDash";
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = DashWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = cls;
    RegisterClassW(&wc);

    g_hwnd = CreateWindowW(
        cls, L"sync agent — dashboard",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return false;

    if (!CreateDeviceAndSwapChain()) return false;
    CreateRenderTarget();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    // 한국어 폰트 로드 (맑은 고딕, 윈도우 10/11 기본 내장)
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\malgun.ttf", 15.0f, &cfg,
        io.Fonts->GetGlyphRangesKorean());

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 2.0f;
    s.FrameRounding  = 2.0f;
    s.TabRounding    = 2.0f;
    s.WindowBorderSize = 0.0f;
    s.Colors[ImGuiCol_WindowBg]       = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    s.Colors[ImGuiCol_PlotHistogram]  = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    s.Colors[ImGuiCol_Tab]            = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    s.Colors[ImGuiCol_TabSelected]    = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);
    return true;
}

void DashboardDestroy() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupRenderTarget();
    if (g_pSwapChain)  { g_pSwapChain->Release();  g_pSwapChain  = nullptr; }
    if (g_pd3dContext) { g_pd3dContext->Release();  g_pd3dContext = nullptr; }
    if (g_pd3dDevice)  { g_pd3dDevice->Release();   g_pd3dDevice  = nullptr; }
    if (g_hwnd)        { DestroyWindow(g_hwnd);      g_hwnd        = nullptr; }
}

void DashboardToggle() {
    if (!g_hwnd) return;
    g_visible = !g_visible;
    ShowWindow(g_hwnd, g_visible ? SW_SHOW : SW_HIDE);
    if (g_visible) SetForegroundWindow(g_hwnd);
}

bool DashboardIsVisible() { return g_visible; }

void DashboardFrame(GameState& state) {
    if (!g_visible) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({(float)W, (float)H});
    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoSavedSettings);

    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("현황"))    { TabStatus(state);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("업그레이드")) { TabUpgrade(state); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("던전"))    { TabDungeon(state);  ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::End();
    ImGui::Render();

    const float clear[4] = { 0.10f, 0.10f, 0.10f, 1.0f };
    g_pd3dContext->OMSetRenderTargets(1, &g_mainRenderTarget, nullptr);
    g_pd3dContext->ClearRenderTargetView(g_mainRenderTarget, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(1, 0);
}

bool DashboardHandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp) != 0;
}
