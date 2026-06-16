#include "dashboard.h"
#include "equipment.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <algorithm>

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

// ---- 클래스 선택 화면 -------------------------------------------------------
static void ScreenClassSelect(GameState& state) {
    ImGui::Spacing();
    ImGui::TextDisabled("직업을 선택하세요  (한 번 고르면 변경 불가)");
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < 3; i++) {
        const ClassDef& c = kClasses[i];
        ImGui::PushID(i);

        // 직업 이름 버튼
        if (ImGui::Button(c.name, {80, 48})) {
            state.playerClass = (ClassType)(i + 1);
            InitTalentsForClass(state);
            SaveGame(state);
        }
        ImGui::SameLine(100);

        // 설명 블록
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", c.flavor);
        ImGui::BulletText("%s", c.stat0);
        ImGui::BulletText("%s", c.stat1);
        ImGui::BulletText("%s", c.stat2);
        ImGui::EndGroup();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PopID();
    }
}

// ---- 탭 UI 함수 ------------------------------------------------------------
static void TabStatus(GameState& state) {
    ImGui::Spacing();

    ImGui::TextDisabled("IDLE AGENT  v0.1");
    ImGui::Separator();
    ImGui::Spacing();

    char buf[64];
    const char* clsName = (state.playerClass != CLASS_NONE)
                          ? kClasses[(int)state.playerClass - 1].name : "?";
    snprintf(buf, sizeof(buf), "Lv.%d  [%s]", state.level, clsName);
    ImGui::Text("영웅");      ImGui::SameLine(100); ImGui::Text("%s", buf);

    ImGui::Spacing();
    ImGui::Text("XP");        ImGui::SameLine(100);
    char xpOverlay[32];
    snprintf(xpOverlay, sizeof(xpOverlay), "%lld / %lld", state.xp, state.xpForNext());
    ImGui::ProgressBar(state.xpProgress(), {320, 0}, xpOverlay);

    ImGui::Spacing();
    ImGui::Text("체력");      ImGui::SameLine(100);
    char hpOverlay[32];
    snprintf(hpOverlay, sizeof(hpOverlay), "%lld / %lld", state.playerHp, state.playerMaxHp);
    float hpFrac = (state.playerMaxHp > 0) ? (float)state.playerHp / (float)state.playerMaxHp : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.30f, 0.80f, 0.35f, 1.0f));
    ImGui::ProgressBar(hpFrac, {320, 0}, hpOverlay);
    ImGui::PopStyleColor();

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

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("프레스티지  (%d회 / 보너스 +%.0f%%)",
                         state.prestigeCount, state.prestigeCount * 15.0f);
    long long req = PrestigeRequirement(state.prestigeCount);
    bool canPrestige = state.dungeon.stage >= req;
    if (!canPrestige) ImGui::BeginDisabled();
    if (ImGui::Button("프레스티지 실행 (레벨/골드/스테이지 초기화)", {320, 0})) {
        DoPrestige(state);
        SaveGame(state);
    }
    if (!canPrestige) ImGui::EndDisabled();
    if (!canPrestige)
        ImGui::TextDisabled("스테이지 %lld 이상 도달 시 해금", req);
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

static void TabTalent(GameState& state) {
    ImGui::Spacing();
    int spent = state.talents[TAL_0].level + state.talents[TAL_1].level + state.talents[TAL_2].level;
    ImGui::TextDisabled("레벨업마다 1포인트 지급. 평생 최대 %d포인트 — 3개 다 풀업(30)은 불가능, 골라서 투자.",
                         MAX_TALENT_POINTS);
    ImGui::Text("보유 포인트"); ImGui::SameLine(120); ImGui::Text("%d", state.talentPoints);
    ImGui::Text("누적 (보유+투자)"); ImGui::SameLine(120); ImGui::Text("%d / %d", state.talentPoints + spent, MAX_TALENT_POINTS);
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < TAL_COUNT; i++) {
        Talent& t = state.talents[i];
        ImGui::PushID(i);

        char header[64];
        snprintf(header, sizeof(header), "%s  [%d / %d]", t.name, t.level, t.maxLevel);
        ImGui::Text("%s", header);

        ImGui::SameLine(260);
        ImGui::TextDisabled("%s", t.desc);

        ImGui::SetNextItemWidth(260);
        ImGui::ProgressBar((float)t.level / t.maxLevel, {260, 6}, "");

        ImGui::SameLine();
        bool maxed = (t.level >= t.maxLevel);
        bool canInvest = !maxed && state.talentPoints > 0;
        if (!canInvest) ImGui::BeginDisabled();
        if (ImGui::Button(maxed ? "MAX" : "+1 투자", {90, 0}))
            InvestTalent(state, i);
        if (!canInvest) ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::PopID();
    }
}

static ImVec4 GradeColor(Grade g) {
    switch (g) {
    case Grade::Common:    return { 1.0f, 1.0f, 1.0f, 1.0f };
    case Grade::Rare:      return { 0.3f, 0.6f, 1.0f, 1.0f };
    case Grade::Epic:      return { 0.7f, 0.3f, 1.0f, 1.0f };
    case Grade::Legendary: return { 1.0f, 0.8f, 0.1f, 1.0f };
    }
    return { 1,1,1,1 };
}

static void TabEquipment(GameState& state) {
    Inventory& inv = state.inventory;
    ImGui::Spacing();

    // ---- 장착 슬롯 ----------------------------------------------------------
    ImGui::TextDisabled("장착 중  (%d / %d)", (int)inv.equipped.size(), Inventory::MAX_EQUIP);
    ImGui::Separator();

    if (inv.equipped.empty()) {
        ImGui::TextDisabled("  장착된 아이템 없음");
    }
    for (int i = 0; i < (int)inv.equipped.size(); i++) {
        const Item& it = inv.equipped[i];
        ImGui::PushID(100 + i);
        ImGui::TextColored(GradeColor(it.grade), "[%s]", GradeName(it.grade));
        ImGui::SameLine();
        ImGui::Text("%s +%.0f%%", StatName(it.stat), it.bonus * 100.0f);
        ImGui::SameLine(300);
        if (ImGui::SmallButton("해제")) Unequip(inv, i);
        ImGui::PopID();
    }

    ImGui::Spacing();

    // ---- 보관함 -------------------------------------------------------------
    ImGui::TextDisabled("보관함  (%d / %d)", (int)inv.items.size(), Inventory::MAX_ITEMS);
    ImGui::Separator();

    if (inv.items.empty()) {
        ImGui::TextDisabled("  아이템 없음");
    }

    // 등급별로 같은 등급 아이템 수 카운트 (합성 버튼 표시용)
    int gradeCnt[4] = {};
    for (const Item& it : inv.items) gradeCnt[(int)it.grade]++;

    for (int i = 0; i < (int)inv.items.size(); i++) {
        const Item& it = inv.items[i];
        ImGui::PushID(i);
        ImGui::TextColored(GradeColor(it.grade), "[%s]", GradeName(it.grade));
        ImGui::SameLine();
        ImGui::Text("%s +%.0f%%", StatName(it.stat), it.bonus * 100.0f);
        ImGui::SameLine(300);
        bool canEquip = (int)inv.equipped.size() < Inventory::MAX_EQUIP;
        if (!canEquip) ImGui::BeginDisabled();
        if (ImGui::SmallButton("장착")) TryEquip(inv, i);
        if (!canEquip) ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::Spacing();

    // ---- 합성 버튼 ----------------------------------------------------------
    ImGui::TextDisabled("합성 (같은 등급 3개 → 상위 등급)");
    ImGui::Separator();

    struct CraftRule { Grade from; const char* label; int rate; };
    const CraftRule rules[] = {
        { Grade::Common, "일반 x3 → 희귀 (100%%)",              100 },
        { Grade::Rare,   "희귀 x3 → 영웅 (70%%) / 실패시 희귀 1개",  70 },
        { Grade::Epic,   "영웅 x3 → 전설 (40%%) / 실패시 영웅 1개",  40 },
    };

    for (auto& rule : rules) {
        int cnt = gradeCnt[(int)rule.from];
        bool canCraft = cnt >= 3;
        char label[64];
        snprintf(label, sizeof(label), "%s  [보유: %d]", rule.label, cnt);
        if (!canCraft) ImGui::BeginDisabled();
        if (ImGui::Button(label, {380, 0})) {
            // 재료 3개 제거
            int removed = 0;
            for (int i = (int)inv.items.size() - 1; i >= 0 && removed < 3; i--) {
                if (inv.items[i].grade == rule.from) {
                    inv.items.erase(inv.items.begin() + i);
                    removed++;
                }
            }
            Item result = CraftItem(rule.from);
            if ((int)inv.items.size() < Inventory::MAX_ITEMS)
                inv.items.push_back(result);
        }
        if (!canCraft) ImGui::EndDisabled();
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
    ImGui::ProgressBar(hpPct, {320, 0}, hpOverlay);

    if (d.bossStage) ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    TalentBonuses tal = ComputeTalentBonuses(state);
    float atkMult = 1.0f + GetEquippedBonus(state.inventory, StatType::Attack)
                         + state.prestigeCount * 0.10f
                         + state.upgrades[UP_ATK].level * state.upgrades[UP_ATK].multiplier
                         + tal.atkBonus;
    float atkSpeedMult = 1.0f + GetEquippedBonus(state.inventory, StatType::AtkSpeed)
                               + tal.atkSpeedBonus;
    long long baseAtk = (long long)(10 * atkMult * atkSpeedMult);
    long long enemyDef = EnemyDefForStage(d.stage);
    long long enemyAtk = EnemyAtkForStage(d.stage);
    long long playerDef = (long long)(10.0 * (1.0f + GetEquippedBonus(state.inventory, StatType::Defense) + tal.defenseBonus));
    float lifestealPct = GetEquippedBonus(state.inventory, StatType::Lifesteal) + tal.lifestealBonus;
    long long dmgToPlayer = (std::max)(0LL, enemyAtk - playerDef);
    dmgToPlayer = (long long)(dmgToPlayer * (1.0f - (std::min)(0.9f, tal.evasionBonus)));

    ImGui::Text("스테이지"); ImGui::SameLine(100); ImGui::Text("%d", d.stage);
    ImGui::Text("적 방어력"); ImGui::SameLine(100); ImGui::Text("%lld", enemyDef);
    ImGui::Text("적 공격력"); ImGui::SameLine(100); ImGui::Text("%lld", enemyAtk);
    ImGui::Text("내 방어력"); ImGui::SameLine(100); ImGui::Text("%lld", playerDef);
    if (lifestealPct > 0.0f) {
        ImGui::Text("체력흡수"); ImGui::SameLine(100); ImGui::Text("%.0f%%", lifestealPct * 100.0f);
    }
    if (dmgToPlayer > 0) {
        ImGui::TextColored({1.0f, 0.4f, 0.3f, 1.0f}, "받는 피해 %lld / 틱 — 체력 0이 되면 전투가 리셋됩니다.", dmgToPlayer);
    } else {
        ImGui::TextDisabled("받는 피해 0 / 틱");
    }

    ImGui::Spacing();
    long long rawHit = 0, critHit = 0;
    switch (state.playerClass) {
    case CLASS_WARRIOR:
        rawHit = (long long)(baseAtk * 1.5f) * (d.bossStage ? 2 : 1);
        if (d.bossStage) rawHit = (long long)(rawHit * (1.0f + tal.bossDmgBonus));
        ImGui::Text("공격력");  ImGui::SameLine(100);
        ImGui::Text("%lld / 틱", rawHit);
        break;
    case CLASS_MAGE: {
        float critChance = (std::min)(100.0f, 20.0f + tal.critChanceBonus);
        rawHit  = (long long)(baseAtk * 0.7f);
        critHit = (long long)(baseAtk * (4.0f + tal.critDmgBonus));
        ImGui::Text("공격력");  ImGui::SameLine(100);
        ImGui::Text("%lld 일반  /  %lld 폭발 (%.0f%%)", rawHit, critHit, critChance);
        break;
    }
    case CLASS_ROGUE:
        rawHit = baseAtk * 2;
        ImGui::Text("공격력");  ImGui::SameLine(100);
        ImGui::Text("%lld x2 / 틱", baseAtk);
        break;
    default:
        rawHit = baseAtk;
        ImGui::Text("공격력");  ImGui::SameLine(100);
        ImGui::Text("%lld / 틱", baseAtk);
        break;
    }

    long long effDmg = (std::max)(0LL, (std::max)(rawHit, critHit) - enemyDef);
    ImGui::Spacing();
    if (effDmg <= 0) {
        ImGui::TextColored({1.0f, 0.4f, 0.3f, 1.0f},
            "실데미지 0 — 적 방어력을 못 넘어 진행 불가. 업그레이드/장비로 공격력을 올리세요.");
    } else {
        ImGui::TextDisabled("실데미지(방어 적용 후)  %lld / 틱", effDmg);
    }
}

// ---- 공개 API --------------------------------------------------------------
bool DashboardInit(HINSTANCE hInst) {
    const wchar_t* cls = L"SyncAgentDash";
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
    // 오버샘플 1x1로 낮춰 텍스처 아틀라스 크기 절감
    ImFontConfig cfg;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;
    ImFont* font = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\malgun.ttf", 15.0f, &cfg,
        io.Fonts->GetGlyphRangesKorean());
    if (!font) io.Fonts->AddFontDefault();

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

    if (state.playerClass == CLASS_NONE) {
        ScreenClassSelect(state);
    } else if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("현황"))       { TabStatus(state);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("업그레이드"))  { TabUpgrade(state);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("특성"))       { TabTalent(state);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("던전"))       { TabDungeon(state);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("장비"))       { TabEquipment(state); ImGui::EndTabItem(); }
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
