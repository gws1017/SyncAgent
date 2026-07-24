#include "dashboard.h"
#include "equipment.h"
#include "lang.h"
#include "cloud_sync.h"
#include "imgui.h"
#ifdef _WIN32
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <dxgi.h>
#endif
#include <cstdio>
#include <algorithm>

#ifdef _WIN32
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

// ---- D3D11 state ----------------------------------------------------------
static ID3D11Device*           g_pd3dDevice      = nullptr;
static ID3D11DeviceContext*    g_pd3dContext      = nullptr;
static IDXGISwapChain*         g_pSwapChain       = nullptr;
static ID3D11RenderTargetView* g_mainRenderTarget = nullptr;

static HWND g_hwnd    = nullptr;
static bool g_visible = false;

static constexpr int W = 460, H = 480;
#endif

// wstring → UTF-8 변환 (ImGui는 UTF-8 사용)
// Windows: WideCharToMultiByte(UTF-16 LE)  /  Android: wchar_t=4바이트(UTF-32) 직접 인코딩
static void ToUtf8(const std::wstring& src, char* dst, int dstSize) {
#ifdef _WIN32
    WideCharToMultiByte(CP_UTF8, 0, src.c_str(), -1, dst, dstSize, nullptr, nullptr);
#else
    int j = 0;
    for (wchar_t wc : src) {
        unsigned cp = (unsigned)wc;
        if      (cp < 0x80   && j+1 < dstSize) { dst[j++] = (char)cp; }
        else if (cp < 0x800  && j+2 < dstSize) { dst[j++] = (char)(0xC0|(cp>>6));  dst[j++] = (char)(0x80|(cp&0x3F)); }
        else if (cp < 0x10000&& j+3 < dstSize) { dst[j++] = (char)(0xE0|(cp>>12)); dst[j++] = (char)(0x80|((cp>>6)&0x3F)); dst[j++] = (char)(0x80|(cp&0x3F)); }
        else if              (  j+4 < dstSize) { dst[j++] = (char)(0xF0|(cp>>18)); dst[j++] = (char)(0x80|((cp>>12)&0x3F)); dst[j++] = (char)(0x80|((cp>>6)&0x3F)); dst[j++] = (char)(0x80|(cp&0x3F)); }
    }
    if (j < dstSize) dst[j] = '\0';
#endif
}

#ifdef _WIN32
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
#endif // _WIN32

// ---- 영웅 로스터 화면 -------------------------------------------------------
// 클래스당 슬롯 1개. 처음이면 새로 시작, 이미 키운 적 있으면 이어서 진행.
// 언제든 이 화면으로 돌아와서 다른 영웅으로 전환할 수 있음 (진행 상황은 안 사라짐).
static void ScreenHeroSelect(GameState& state) {
    ImGui::Spacing();

    // ---- 최초 실행 온보딩 — 영웅을 한 번도 안 키워봤을 때만 표시.
    // "갑자기 자동 진행이라 당황스럽다"는 피드백 대응 (비공개 테스트 피드백 #1).
    bool everPlayedAny = false;
    for (int i = 0; i < GameState::ROSTER_SIZE; i++)
        if (state.heroes[i].everPlayed) { everPlayedAny = true; break; }
    if (!everPlayedAny) {
        ImGui::TextWrapped("%s", T(
            "이 게임은 방치형(자동 진행) RPG입니다. 아래에서 직업을 하나 고르면, "
            "그 순간부터 던전 전투/레벨업/골드 획득이 전부 자동으로 진행됩니다 — "
            "화면을 계속 보고 있지 않아도, 앱을 최소화해도 그대로 자랍니다.",
            "This is an idle RPG. Pick a class below and combat, leveling, and gold "
            "all progress automatically from then on — even while you're not looking, "
            "or after you minimize the app."));
        ImGui::Spacing();
    }

    ImGui::TextDisabled("%s", T("영웅을 선택하세요  (전환해도 진행 상황은 유지됩니다)",
                                 "Choose a hero  (switching keeps everyone's progress)"));
    if (state.legacyPrestigeCount > 0) {
        ImGui::TextDisabled(T("계승 보너스 +%.1f%% (누적 프레스티지 %d회)", "Legacy bonus +%.1f%% (%d total prestiges)"),
                             state.legacyBonusPct, state.legacyPrestigeCount);
    }
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < GameState::ROSTER_SIZE; i++) {
        const ClassDef& c = kClasses[i];
        const Hero& hero = state.heroes[i];
        ImGui::PushID(i);

        char btnLabel[16];
        snprintf(btnLabel, sizeof(btnLabel), "%s", c.Name());
        if (ImGui::Button(btnLabel, {80, 48})) {
            CreateOrSwitchHero(state, (ClassType)(i + 1));
            SaveGame(state);
        }
        ImGui::SameLine(100);

        ImGui::BeginGroup();
        if (hero.everPlayed) {
            ImGui::Text(T("Lv.%d  —  스테이지 %d", "Lv.%d  —  stage %d"), hero.level, hero.dungeon.stage);
            if (hero.prestigeCount > 0)
                ImGui::TextDisabled(T("프레스티지 %d회", "%d prestiges"), hero.prestigeCount);
        } else {
            ImGui::TextDisabled("%s", T("아직 키운 적 없음 — 처음부터 시작", "Not started yet — begin from scratch"));
        }
        ImGui::TextDisabled("%s", c.Flavor());
        ImGui::EndGroup();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PopID();
    }
}

// ---- 탭 UI 함수 ------------------------------------------------------------
static void FormatDuration(double sec, char* buf, int bufSize) {
    long long total = (long long)sec;
    long long h = total / 3600;
    long long m = (total % 3600) / 60;
    if (h > 0) snprintf(buf, bufSize, "%lldh %lldm", h, m);
    else       snprintf(buf, bufSize, "%lldm", m);
}

static void TabStatus(GameState& state) {
    Hero& hero = state.Active();
    ImGui::Spacing();

    ImGui::TextDisabled("TEXT RPG  v1.1.0");
    ImGui::Separator();
    ImGui::Spacing();

    char evt[128] = {};
    ToUtf8(hero.lastEvent, evt, sizeof(evt));
    ImGui::TextDisabled("%s", T("최근 이벤트", "Recent event"));
    ImGui::TextWrapped("%s", evt);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("%s", T("계승 보너스 (전 영웅 공용)", "Legacy bonus (shared by all heroes)"));
    ImGui::Text("%s", T("현재 보너스", "Current bonus")); ImGui::SameLine(140);
    ImGui::Text("+%.1f%%", state.legacyBonusPct);
    ImGui::Text("%s", T("누적 프레스티지", "Total prestiges")); ImGui::SameLine(140);
    ImGui::Text("%d", state.legacyPrestigeCount);
    if (ImGui::Button(T("다른 영웅으로 전환", "Switch hero"), {320, 0})) {
        SaveGame(state);
        state.activeHero = -1;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    char runBuf[32], dashBuf[32];
    FormatDuration(state.totalRunSec, runBuf, sizeof(runBuf));
    FormatDuration(state.dashboardOpenSec, dashBuf, sizeof(dashBuf));
    ImGui::TextDisabled("%s", T("위장 기록", "Stealth log"));
    ImGui::Text("%s", T("몰래 가동", "Hidden uptime"));    ImGui::SameLine(140);
    ImGui::Text(T("%s  (트레이에 숨어서 돈 시간)", "%s  (time spent hidden in tray)"), runBuf);
    ImGui::Text("%s", T("대시보드 노출", "Dashboard open"));   ImGui::SameLine(140);
    ImGui::Text(T("%s  (들킬 뻔한 시간)", "%s  (time you risked getting caught)"), dashBuf);
    ImGui::Text("%s", T("사망 횟수", "Deaths"));   ImGui::SameLine(140);
    ImGui::Text(T("%lld 회  (이 영웅 기준)", "%lld  (this hero)"), hero.deathCount);
}

// ---- 설정 탭 ----------------------------------------------------------------
static void TabOptions(GameState& state) {
    ImGui::Spacing();

    ImGui::TextDisabled("%s", T("언어 / Language", "Language / 언어"));
    int langIdx = (g_lang == Lang::KO) ? 0 : 1;
    const char* langItems[] = { "한국어", "English" };
    ImGui::SetNextItemWidth(160);
    if (ImGui::Combo("##lang", &langIdx, langItems, 2)) {
        g_lang = (langIdx == 0) ? Lang::KO : Lang::EN;
        state.language = langIdx;
        SaveGame(state);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- 백그라운드 실행 -----------------------------------------------------
    // 꺼두면 안드로이드는 포그라운드 서비스(상시 알림)를 내려서 앱을 최소화했을 때
    // 성장이 멈추고, PC는 대시보드가 닫혀 있는 동안 틱을 건너뛴다. 강제종료 없이도
    // 유저가 직접 껐다 켰다 할 수 있게 하기 위한 옵션.
    ImGui::TextDisabled("%s", T("백그라운드 실행", "Background running"));
    ImGui::TextWrapped("%s", T("꺼두면 앱을 보고 있지 않을 때(최소화/백그라운드) 성장이 멈춥니다."
                                " 안드로이드에서는 상시 알림도 함께 사라집니다.",
                                "When off, progress pauses while the app isn't in the foreground."
                                " On Android, the persistent notification also disappears."));
    bool bgEnabled = state.backgroundEnabled;
    if (ImGui::Checkbox(T("백그라운드에서도 계속 실행", "Keep running in background"), &bgEnabled)) {
        state.backgroundEnabled = bgEnabled;
        SaveGame(state);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- 프라이버시 모드 ----------------------------------------------------
    // 화면 내용(탭/스탯 등)은 그대로 두고, 창 제목/트레이 표시 이름만
    // "Text RPG" ↔ "sync agent"로 바꾼다. 기본은 정직한 이름이 뜨고, 이 토글을
    // 켰을 때만 sync agent로 바뀜 (스토어 리스팅과 실제 앱 정체성 일치를 위해).
    ImGui::TextDisabled("%s", T("프라이버시 모드", "Privacy mode"));
    ImGui::TextWrapped("%s", T("창 제목과 트레이 표시 이름을 sync agent로 바꿉니다 (화면 내용은 그대로).",
                                "Renames the window title and tray icon to sync agent (screen content stays the same)."));
    bool disguise = state.disguiseMode;
    if (ImGui::Checkbox(T("프라이버시 모드 켜기", "Enable privacy mode"), &disguise)) {
        state.disguiseMode = disguise;
        SaveGame(state);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- 클라우드 동기화 (PC ↔ 모바일) --------------------------------------
    // 계정 로그인 없이 "동기화 코드" 하나로 두 기기를 연결한다. 한쪽에서 코드를
    // 만들고 다른 쪽에서 그 코드를 입력하면 됨. 네트워크 요청은 동기(blocking)라
    // 버튼 누르는 순간 잠깐 멈칫할 수 있음 — 게임이 워낙 가벼워서 감내할 만한 수준.
    static char codeInput[16] = {};
    static std::string syncMsg;
    static bool codeInputInit = false;
    std::string savedCode = CloudGetSavedCode();
    if (!codeInputInit) {
        snprintf(codeInput, sizeof(codeInput), "%s", savedCode.c_str());
        codeInputInit = true;
    }

    ImGui::TextDisabled("%s", T("클라우드 동기화 (PC / 모바일)", "Cloud sync (PC / mobile)"));
    if (savedCode.empty()) {
        ImGui::TextWrapped("%s", T("동기화 코드가 없습니다. 새로 만들거나, 다른 기기에서 만든 코드를 입력하세요.",
                                    "No sync code yet. Generate one, or enter a code from another device."));
    } else {
        ImGui::Text("%s", T("현재 코드", "Current code")); ImGui::SameLine(140);
        ImGui::TextColored({0.4f, 0.9f, 0.5f, 1.0f}, "%s", savedCode.c_str());
    }

    if (ImGui::Button(T("새 코드 생성", "Generate code"), {150, 0})) {
        std::string c = CloudGenerateCode();
        snprintf(codeInput, sizeof(codeInput), "%s", c.c_str());
        syncMsg = T("새 코드가 생성됐습니다", "New code generated");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputText("##codein", codeInput, sizeof(codeInput), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    if (ImGui::Button(T("이 코드로 연결", "Link this code"), {130, 0})) {
        CloudSetCode(codeInput);
        syncMsg = T("코드가 연결됐습니다", "Code linked");
    }

    bool hasCode = codeInput[0] != '\0';
    if (!hasCode) ImGui::BeginDisabled();
    if (ImGui::Button(T("지금 업로드", "Upload now"), {150, 0})) {
        CloudSyncResult r = CloudUpload(codeInput, SerializeGameState(state));
        syncMsg = r.message;
    }
    ImGui::SameLine();
    if (ImGui::Button(T("지금 다운로드", "Download now"), {150, 0})) {
        std::string text;
        CloudSyncResult r = CloudDownload(codeInput, text);
        if (r.ok) {
            GameState fresh{};
            if (DeserializeGameState(text, fresh)) {
                state = fresh;
                g_lang = (state.language == 1) ? Lang::EN : Lang::KO;
                SaveGame(state);
                syncMsg = T("다운로드 완료 — 로컬 세이브에 반영됨", "Downloaded — applied to local save");
            } else {
                syncMsg = T("받은 데이터를 읽지 못했습니다", "Could not parse downloaded data");
            }
        } else {
            syncMsg = r.message;
        }
    }
    if (!hasCode) ImGui::EndDisabled();

    if (!syncMsg.empty())
        ImGui::TextDisabled("%s", syncMsg.c_str());
}

static void TabUpgrade(GameState& state) {
    Hero& hero = state.Active();
    ImGui::Spacing();

    ImGui::Text("%s", T("골드", "Gold"));      ImGui::SameLine(100); ImGui::Text("%lld G", hero.gold);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- 현재 배율 분해 표시 — GameTick과 완전히 같은 공식으로 계산해서
    // "지금 골드/XP/드랍률이 기존 대비 몇 %인지" 한눈에 보이게 함.
    // (비공개 테스트 피드백 #2: 증가율이 얼마나인지 표시가 없다는 요청 대응)
    {
        float legacyFrac = state.legacyBonusPct / 100.0f;

        float xpUp   = hero.upgrades[UP_XP].level   * hero.upgrades[UP_XP].multiplier;
        float goldUp = hero.upgrades[UP_GOLD].level * hero.upgrades[UP_GOLD].multiplier;
        float dropUp = hero.upgrades[UP_DROP].level * hero.upgrades[UP_DROP].multiplier;

        float xpMult   = 1.0f + xpUp   + legacyFrac;
        float goldMult = 1.0f + goldUp + legacyFrac;
        float dropMult = 1.0f + dropUp + legacyFrac;

        if (hero.playerClass == CLASS_MAGE)    xpMult   *= 1.5f;
        if (hero.playerClass == CLASS_WARRIOR) goldMult *= 1.2f;
        if (hero.playerClass == CLASS_ROGUE)   goldMult *= 1.3f;
        if (hero.playerClass == CLASS_ROGUE)   dropMult *= 2.0f;

        float xpEq   = GetEquippedBonus(hero.inventory, StatType::Xp);
        float goldEq = GetEquippedBonus(hero.inventory, StatType::Gold);
        float dropEq = GetEquippedBonus(hero.inventory, StatType::Drop);
        xpMult   += xpEq;
        goldMult += goldEq;
        dropMult += dropEq;

        ImGui::TextDisabled("%s", T("현재 배율 (기본 100% 대비)", "Current multipliers (vs. base 100%)"));
        auto Row = [&](const char* label, float mult, float upBonus, float eqBonus) {
            ImGui::Text("%s", label); ImGui::SameLine(100);
            ImGui::Text("%.0f%%", mult * 100.0f);
            ImGui::SameLine(180);
            ImGui::TextDisabled(T("(업글 +%.0f%%p, 계승 +%.0f%%p, 장비 +%.0f%%p)",
                                   "(upg +%.0f%%p, legacy +%.0f%%p, gear +%.0f%%p)"),
                                 upBonus * 100.0f, legacyFrac * 100.0f, eqBonus * 100.0f);
        };
        Row(T("골드", "Gold"),   goldMult, goldUp, goldEq);
        Row(T("경험치", "XP"),   xpMult,   xpUp,   xpEq);
        Row(T("드랍률", "Drop"), dropMult, dropUp, dropEq);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < UP_COUNT; i++) {
        Upgrade& u = hero.upgrades[i];
        ImGui::PushID(i);

        // 이름 + 레벨
        char header[64];
        snprintf(header, sizeof(header), "%s  [%d / %d]", u.Name(), u.level, u.maxLevel);
        ImGui::Text("%s", header);

        // 설명
        ImGui::SameLine(260);
        ImGui::TextDisabled("%s", u.Desc());

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

        bool canBuy = !maxed && hero.gold >= cost;
        if (!canBuy) ImGui::BeginDisabled();
        if (ImGui::Button(btnLabel, {90, 0}))
            PurchaseUpgrade(hero, i);
        if (!canBuy) ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled(T("프레스티지  (이 영웅 %d회 진행)", "Prestige  (%d done by this hero)"), hero.prestigeCount);
    ImGui::TextWrapped("%s", T("스테이지 40 이상에서 실행 가능. 지금까지 투자한 업그레이드/특성/장비량에 비례해서"
                                " 계승 보너스(전 영웅 공용)가 오르고, 이 영웅은 리셋되어 다시 키울 수 있습니다.",
                                "Available from stage 40+. Adds to the shared legacy bonus based on how much"
                                " you've invested (upgrades/talents/gear), then resets this hero to grow again."));
    bool canPrestige = hero.dungeon.stage >= PRESTIGE_STAGE_REQ;
    if (!canPrestige) ImGui::BeginDisabled();
    if (ImGui::Button(T("프레스티지 실행", "Prestige Now"), {320, 0})) {
        DoPrestige(state);
        SaveGame(state);
    }
    if (!canPrestige) ImGui::EndDisabled();
    if (!canPrestige)
        ImGui::TextDisabled(T("스테이지 %d 이상 도달 시 해금", "Unlocks at stage %d"), PRESTIGE_STAGE_REQ);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    static bool confirmReset = false;
    ImGui::TextDisabled("%s", T("전체 초기화", "Full Reset"));
    ImGui::TextWrapped("%s", T("모든 영웅과 계승 보너스를 통째로 삭제합니다 (위장 시간 기록/언어 설정만 유지).",
                                "Wipes every hero and the legacy bonus entirely (keeps stealth log/language)."));
    ImGui::Checkbox(T("정말로 초기화할게요 (되돌릴 수 없음)", "Yes, really reset (cannot be undone)"), &confirmReset);
    if (!confirmReset) ImGui::BeginDisabled();
    if (ImGui::Button(T("전체 초기화 실행", "Full Reset Now"), {320, 0})) {
        ResetAll(state);
        SaveGame(state);
        confirmReset = false;
    }
    if (!confirmReset) ImGui::EndDisabled();
}

// slotStart..slotStart+count-1 구간의 특성 목록을 그려주는 공용 헬퍼 (1차/2차 공용)
static void DrawTalentRows(Hero& hero, int slotStart, int count, int& pool) {
    for (int i = slotStart; i < slotStart + count; i++) {
        Talent& t = hero.talents[i];
        ImGui::PushID(i);

        char header[64];
        snprintf(header, sizeof(header), "%s  [%d / %d]", t.Name(), t.level, t.maxLevel);
        ImGui::Text("%s", header);

        ImGui::SameLine(260);
        ImGui::TextDisabled("%s", t.Desc());

        ImGui::SetNextItemWidth(260);
        ImGui::ProgressBar((float)t.level / t.maxLevel, {260, 6}, "");

        ImGui::SameLine();
        bool maxed = (t.level >= t.maxLevel);
        bool canInvest = !maxed && pool > 0;
        if (!canInvest) ImGui::BeginDisabled();
        if (ImGui::Button(maxed ? "MAX" : T("+1 투자", "+1 Invest"), {90, 0}))
            InvestTalent(hero, i);
        if (!canInvest) ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::PopID();
    }
}

static void TabTalent(GameState& state) {
    Hero& hero = state.Active();
    ImGui::Spacing();
    int spent1 = hero.talents[TAL_0].level + hero.talents[TAL_1].level + hero.talents[TAL_2].level;
    ImGui::TextDisabled(T("1차 특성  —  레벨업마다 1포인트, 평생 최대 %d포인트 (3개 다 풀업은 불가능)",
                           "Tier 1 talents  —  1 point/level-up, %d lifetime cap (can't max all 3)"),
                         MAX_TALENT_POINTS);
    ImGui::Text("%s", T("보유 포인트", "Points available")); ImGui::SameLine(140); ImGui::Text("%d", hero.talentPoints);
    ImGui::Text("%s", T("누적 (보유+투자)", "Total earned"));   ImGui::SameLine(140); ImGui::Text("%d / %d", hero.talentPoints + spent1, MAX_TALENT_POINTS);
    ImGui::Separator();
    ImGui::Spacing();

    DrawTalentRows(hero, TAL_0, TIER1_TAL_COUNT, hero.talentPoints);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (hero.level < TIER2_LEVEL_REQ) {
        ImGui::TextDisabled(T("2차 특성  —  레벨 %d부터 해금 (현재 Lv.%d)", "Tier 2 talents  —  unlocks at Lv.%d (currently Lv.%d)"),
                             TIER2_LEVEL_REQ, hero.level);
    } else {
        int spent2 = hero.talents[TAL_3].level + hero.talents[TAL_4].level + hero.talents[TAL_5].level;
        ImGui::TextDisabled(T("2차 특성  —  레벨업마다 1포인트, 평생 최대 %d포인트 (1차와 별도 풀)",
                               "Tier 2 talents  —  1 point/level-up, %d lifetime cap (separate pool from tier 1)"),
                             MAX_TALENT_POINTS_T2);
        ImGui::Text("%s", T("보유 포인트", "Points available")); ImGui::SameLine(140); ImGui::Text("%d", hero.talentPoints2);
        ImGui::Text("%s", T("누적 (보유+투자)", "Total earned"));   ImGui::SameLine(140); ImGui::Text("%d / %d", hero.talentPoints2 + spent2, MAX_TALENT_POINTS_T2);
        ImGui::Separator();
        ImGui::Spacing();

        DrawTalentRows(hero, TAL_3, TIER2_TAL_COUNT, hero.talentPoints2);
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
    Inventory& inv = state.Active().inventory;
    ImGui::Spacing();

    // ---- 장착 슬롯 ----------------------------------------------------------
    ImGui::TextDisabled(T("장착 중  (%d / %d)", "Equipped  (%d / %d)"), (int)inv.equipped.size(), Inventory::MAX_EQUIP);
    ImGui::Separator();

    if (inv.equipped.empty()) {
        ImGui::TextDisabled("%s", T("  장착된 아이템 없음", "  No items equipped"));
    }
    for (int i = 0; i < (int)inv.equipped.size(); i++) {
        const Item& it = inv.equipped[i];
        ImGui::PushID(100 + i);
        ImGui::TextColored(GradeColor(it.grade), "[%s]", GradeName(it.grade));
        ImGui::SameLine();
        ImGui::Text("%s +%.0f%%", StatName(it.stat), it.bonus * 100.0f);
        ImGui::SameLine(300);
        if (ImGui::SmallButton(T("해제", "Unequip"))) Unequip(inv, i);
        ImGui::PopID();
    }

    ImGui::Spacing();

    // ---- 보관함 -------------------------------------------------------------
    ImGui::TextDisabled(T("보관함  (%d / %d)", "Bag  (%d / %d)"), (int)inv.items.size(), Inventory::MAX_ITEMS);
    ImGui::Separator();

    if (inv.items.empty()) {
        ImGui::TextDisabled("%s", T("  아이템 없음", "  No items"));
    }

    // 등급별로 같은 등급 아이템 수 카운트 (합성 버튼 표시용)
    int gradeCnt[4] = {};
    for (const Item& it : inv.items) gradeCnt[(int)it.grade]++;

    Hero& activeHero = state.Active();
    for (int i = 0; i < (int)inv.items.size(); i++) {
        Item& it = inv.items[i];
        ImGui::PushID(i);
        ImGui::TextColored(GradeColor(it.grade), "[%s]", GradeName(it.grade));
        ImGui::SameLine();
        ImGui::Text("%s +%.0f%%", StatName(it.stat), it.bonus * 100.0f);

        // 버튼 3개(장착/리롤/삭제)를 정보 텍스트랑 같은 줄에 억지로 욱여넣으면
        // 창 폭(460)을 넘어가서 잘림 — 버튼들은 다음 줄로 내려서 항상 폭 안에 들어오게 함.
        bool canEquip = (int)inv.equipped.size() < Inventory::MAX_EQUIP;
        if (!canEquip) ImGui::BeginDisabled();
        if (ImGui::SmallButton(T("장착", "Equip"))) TryEquip(inv, i);
        if (!canEquip) ImGui::EndDisabled();

        // ---- 리롤 (스탯 종류만 재추첨, 등급/보너스%는 유지) ----------------------
        ImGui::SameLine();
        long long rerollCost = RerollCost(it.grade);
        bool canReroll = activeHero.gold >= rerollCost;
        if (!canReroll) ImGui::BeginDisabled();
        char rerollLabel[32];
        snprintf(rerollLabel, sizeof(rerollLabel), T("리롤 (%lldG)", "Reroll (%lldG)"), rerollCost);
        if (ImGui::SmallButton(rerollLabel)) {
            activeHero.gold -= rerollCost;
            RerollItem(it);
        }
        if (!canReroll) ImGui::EndDisabled();

        // ---- 삭제 (되돌릴 수 없음, 칸 낭비되는 잡템 정리용) ----------------------
        ImGui::SameLine();
        if (ImGui::SmallButton(T("삭제", "Delete"))) {
            DeleteItem(inv, i);
            ImGui::PopID();
            continue; // 벡터가 한 칸 당겨졌으니 인덱스 재사용 없이 다음 프레임에 다시 그림
        }
        ImGui::PopID();
    }

    ImGui::Spacing();

    // ---- 합성 버튼 ----------------------------------------------------------
    ImGui::TextDisabled("%s", T("합성 (같은 등급 3개 → 상위 등급)", "Craft (3 of same grade -> 1 higher grade)"));
    ImGui::Separator();

    struct CraftRule { Grade from; const char* labelKo; const char* labelEn; };
    const CraftRule rules[] = {
        { Grade::Common, "일반 x3 → 희귀 (100%%)",             "Common x3 -> Rare (100%%)" },
        { Grade::Rare,   "희귀 x3 → 영웅 (70%%) / 실패시 희귀 1개", "Rare x3 -> Epic (70%%) / fail returns 1 Rare" },
        { Grade::Epic,   "영웅 x3 → 전설 (40%%) / 실패시 영웅 1개", "Epic x3 -> Legendary (40%%) / fail returns 1 Epic" },
    };

    for (auto& rule : rules) {
        int cnt = gradeCnt[(int)rule.from];
        bool canCraft = cnt >= 3;
        char label[96];
        snprintf(label, sizeof(label), T("%s  [보유: %d]", "%s  [have: %d]"), T(rule.labelKo, rule.labelEn), cnt);
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
    Hero& hero = state.Active();
    Dungeon& d = hero.dungeon;
    // state.legacyBonusPct는 "37.5"처럼 퍼센트 숫자 그대로 저장됨 — 배율 계산엔
    // 분수(0.375)로 써야 해서 여기서 나눔 (game.cpp GameTick과 동일한 이유/수정).
    float legacyFrac = state.legacyBonusPct / 100.0f;
    ImGui::Spacing();

    // ---- 캐릭터 상태 (적 체력과 바로 비교되도록 같은 너비로 위에 배치) ----------
    char clsBuf[64];
    const char* clsName = (hero.playerClass != CLASS_NONE)
                          ? kClasses[(int)hero.playerClass - 1].Name() : "?";
    snprintf(clsBuf, sizeof(clsBuf), "Lv.%d  [%s]", hero.level, clsName);
    ImGui::Text("%s", T("영웅", "Hero"));      ImGui::SameLine(100); ImGui::Text("%s", clsBuf);

    ImGui::Spacing();
    ImGui::Text("XP");        ImGui::SameLine(100);
    char xpOverlay[32];
    snprintf(xpOverlay, sizeof(xpOverlay), "%lld / %lld", hero.xp, hero.xpForNext());
    ImGui::ProgressBar(hero.xpProgress(), {320, 0}, xpOverlay);

    ImGui::Spacing();
    ImGui::Text("%s", T("내 체력", "My HP"));    ImGui::SameLine(100);
    // HP 자체는 정수(long long)로 유지하되, 아직 반영 안 된 체력흡수 이월분
    // (lifestealCarry)을 소숫점으로 얹어서 보여줌 — "흡혈이 계속 쌓이고 있다"는
    // 게 눈에 보이게 (정수만 보이면 몇 틱 동안 그대로인 것처럼 느껴짐).
    float displayHp = (float)hero.playerHp + hero.lifestealCarry;
    char playerHpOverlay[32];
    snprintf(playerHpOverlay, sizeof(playerHpOverlay), "%.1f / %lld", displayHp, hero.playerMaxHp);
    float playerHpFrac = (hero.playerMaxHp > 0) ? displayHp / (float)hero.playerMaxHp : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.30f, 0.80f, 0.35f, 1.0f));
    ImGui::ProgressBar(playerHpFrac, {320, 0}, playerHpOverlay);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    char stageBuf[32];
    snprintf(stageBuf, sizeof(stageBuf),
             d.bossStage ? T("스테이지 %d  [BOSS]", "Stage %d  [BOSS]") : T("스테이지 %d", "Stage %d"), d.stage);
    ImGui::Text("%s", stageBuf);

    ImGui::Spacing();
    ImGui::Text("%s", T("적 체력", "Enemy HP"));    ImGui::SameLine(100);

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

    TalentBonuses tal = ComputeTalentBonuses(hero);
    float atkMult = 1.0f + GetEquippedBonus(hero.inventory, StatType::Attack)
                         + legacyFrac
                         + hero.upgrades[UP_ATK].level * hero.upgrades[UP_ATK].multiplier
                         + tal.atkBonus;
    // 공격속도는 데미지 배율이 아니라 "한 틱에 평균 몇 번 때리는지"를 나타냄 (game.cpp와 동일 모델)
    float atkSpeedBonus = GetEquippedBonus(hero.inventory, StatType::AtkSpeed) + tal.atkSpeedBonus;
    float expectedAttacks = 1.0f + atkSpeedBonus;
    long long baseAtk = (long long)(PlayerBaseAtk(d.stage) * atkMult);
    long long enemyDef = EnemyDefForStage(d.stage);
    long long enemyAtk = EnemyAtkForStage(d.stage);
    long long playerDef = (long long)(PlayerBaseDef(d.stage) * (1.0f + GetEquippedBonus(hero.inventory, StatType::Defense) + tal.defenseBonus + legacyFrac));
    float lifestealPct = GetEquippedBonus(hero.inventory, StatType::Lifesteal) + tal.lifestealBonus;
    long long dmgToPlayer = MitigateDamage(enemyAtk, playerDef);
    dmgToPlayer = (long long)(dmgToPlayer * (1.0f - (std::min)(0.9f, tal.evasionBonus)));

    ImGui::Text("%s", T("적 방어력", "Enemy DEF")); ImGui::SameLine(100); ImGui::Text("%lld", enemyDef);
    ImGui::Text("%s", T("적 공격력", "Enemy ATK")); ImGui::SameLine(100); ImGui::Text("%lld", enemyAtk);
    ImGui::Text("%s", T("내 방어력", "My DEF")); ImGui::SameLine(100); ImGui::Text("%lld", playerDef);
    if (atkSpeedBonus > 0.0f) {
        ImGui::Text("%s", T("공격속도", "Atk Speed")); ImGui::SameLine(100);
        ImGui::Text(T("평균 %.2f회 / 틱", "avg %.2f hits / tick"), expectedAttacks);
    }
    if (lifestealPct > 0.0f) {
        ImGui::Text("%s", T("체력흡수", "Lifesteal")); ImGui::SameLine(100); ImGui::Text("%.0f%%", lifestealPct * 100.0f);

        // 회복량이 그냥 고정 텍스트로 박혀있으면 다른 숫자들 사이에 묻혀서 눈에 안 띈다는
        // 피드백 대응 — 애니메이션 없는 UI라 "지금 막 일어난 일"이라는 느낌을 주기 위해
        // 틱이 바뀔 때마다 잠깐 밝게 떴다가 서서히 옅어지게 함. hero.lastHealAmount(정수,
        // 실제 HP 반영분)가 아니라 hero.lastHealFrac(소숫점, 이번 틱 계산값)을 써서 —
        // 작은 흡혈은 정수로 반영 안 돼도 "매 틱 작동은 하고 있다"는 게 보이게 함.
        static double s_lastTickRunSec  = -1.0;
        static double s_healPopupAtTime = -1000.0;
        static float  s_healPopupFrac   = 0.0f;
        if (state.totalRunSec != s_lastTickRunSec) {
            s_lastTickRunSec = state.totalRunSec;
            if (hero.lastHealFrac > 0.0f) {
                s_healPopupAtTime = ImGui::GetTime();
                s_healPopupFrac   = hero.lastHealFrac;
            }
        }
        double elapsed = ImGui::GetTime() - s_healPopupAtTime;
        constexpr double kPopupDuration = 2.0;
        if (elapsed >= 0.0 && elapsed < kPopupDuration) {
            float alpha = 1.0f - (float)(elapsed / kPopupDuration);
            ImGui::SameLine();
            ImGui::TextColored({0.4f, 0.9f, 0.5f, alpha}, T("  ↳ 방금 +%.2f 회복!", "  -> just healed +%.2f!"), s_healPopupFrac);
        }
    }
    if (dmgToPlayer > 0) {
        ImGui::TextColored({1.0f, 0.4f, 0.3f, 1.0f},
            T("받는 피해 %lld / 틱 — 체력 0이 되면 전투가 리셋됩니다.", "Taking %lld dmg / tick — fight resets if HP hits 0."),
            dmgToPlayer);
    } else {
        ImGui::TextDisabled(T("받는 피해 %lld / 틱 (방어력으로 대부분 상쇄)", "Taking %lld dmg / tick (mostly absorbed by defense)"), dmgToPlayer);
    }

    ImGui::Spacing();
    long long rawHit = 0, critHit = 0;
    switch (hero.playerClass) {
    case CLASS_WARRIOR:
        rawHit = (long long)(baseAtk * 1.5f) * (d.bossStage ? 2 : 1);
        if (d.bossStage) rawHit = (long long)(rawHit * (1.0f + tal.bossDmgBonus));
        ImGui::Text("%s", T("공격력", "Attack"));  ImGui::SameLine(100);
        ImGui::Text(T("%lld / 타", "%lld / hit"), rawHit);
        break;
    case CLASS_MAGE: {
        float critChance = (std::min)(100.0f, 20.0f + tal.critChanceBonus);
        rawHit  = (long long)(baseAtk * 0.7f);
        critHit = (long long)(baseAtk * (4.0f + tal.critDmgBonus));
        ImGui::Text("%s", T("공격력", "Attack"));  ImGui::SameLine(100);
        ImGui::Text(T("%lld 일반  /  %lld 폭발 (%.0f%%)  (1타 기준)", "%lld normal  /  %lld burst (%.0f%%)  (per hit)"),
                    rawHit, critHit, critChance);
        break;
    }
    case CLASS_ROGUE:
        rawHit = baseAtk * 2;
        ImGui::Text("%s", T("공격력", "Attack"));  ImGui::SameLine(100);
        ImGui::Text(T("%lld x2 / 타", "%lld x2 / hit"), baseAtk);
        break;
    default:
        rawHit = baseAtk;
        ImGui::Text("%s", T("공격력", "Attack"));  ImGui::SameLine(100);
        ImGui::Text(T("%lld / 타", "%lld / hit"), baseAtk);
        break;
    }

    // 실데미지 미리보기 — 1타 기준 값에 평균 공격속도 횟수를 곱해서 틱당 기대치로 환산
    long long perTickRaw = (long long)((float)(std::max)(rawHit, critHit) * expectedAttacks);
    long long effDmg = (std::max)(0LL, perTickRaw - enemyDef);
    ImGui::Spacing();
    if (effDmg <= 0) {
        ImGui::TextColored({1.0f, 0.4f, 0.3f, 1.0f}, "%s",
            T("실데미지 0 — 적 방어력을 못 넘어 진행 불가. 업그레이드/장비로 공격력을 올리세요.",
              "0 effective damage — can't break enemy DEF. Boost attack via upgrades/gear."));
    } else {
        ImGui::TextDisabled(T("실데미지(방어 적용 후, 틱당 기대치)  %lld / 틱", "Effective dmg (after DEF, per tick)  %lld / tick"), effDmg);
    }
}

// ---- 공개 API: 플랫폼 공통 ------------------------------------------------

static float g_topInsetDp  = 0.0f;
static float g_sideMarginDp = 0.0f;

void DashboardSetTopInset(float insetDp) {
    g_topInsetDp = insetDp;
}

void DashboardSetSideMargin(float marginDp) {
    g_sideMarginDp = marginDp;
}

// 순수 ImGui UI — NewFrame 이후, Render 이전에 호출.
// DisplaySize를 그대로 사용하므로 PC 고정 창 / 모바일 전체화면 둘 다 동작한다.
// 안드로이드에서는 카메라 펀치홀/노치를 피해서 g_topInsetDp만큼 아래에서 시작하고,
// g_sideMarginDp만큼 좌우 여백을 둔다 (main_android.cpp가 논리 캔버스를 그만큼
// 넓게 잡아둬서, 기존 460dp 폭 레이아웃은 안 줄어들고 그대로 가운데 배치됨).
void DashboardDrawUI(GameState& state) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({g_sideMarginDp, g_topInsetDp});
    ImGui::SetNextWindowSize({io.DisplaySize.x - g_sideMarginDp * 2.0f, io.DisplaySize.y - g_topInsetDp});
    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoSavedSettings);

    auto TabScrollable = [](const char* id, void (*fn)(GameState&), GameState& s) {
        ImGui::BeginChild(id, {0, 0}, false);
        fn(s);
        ImGui::EndChild();
    };

    if (state.activeHero < 0) {
        ScreenHeroSelect(state);
    } else if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem(T("현황", "Status")))    { TabScrollable("##s1", TabStatus,    state); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(T("업그레이드", "Upgrade"))) { TabScrollable("##s2", TabUpgrade,   state); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(T("특성", "Talents")))   { TabScrollable("##s3", TabTalent,    state); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(T("던전", "Dungeon")))   { TabScrollable("##s4", TabDungeon,   state); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(T("장비", "Gear")))      { TabScrollable("##s5", TabEquipment, state); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(T("설정", "Options")))   { TabScrollable("##s6", TabOptions,   state); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ---- 공개 API: Windows 전용 -----------------------------------------------
#ifdef _WIN32

static constexpr UINT IDI_APPICON      = 101; // 기본(정직) — 판타지 젬 아이콘
static constexpr UINT IDI_APPICON_SYNC = 102; // 프라이버시 모드 — sync 아이콘

static HINSTANCE g_hInst = nullptr;

bool DashboardInit(HINSTANCE hInst) {
    g_hInst = hInst;
    const wchar_t* cls = L"SyncAgentDash";
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = DashWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.lpszClassName = cls;
    RegisterClassW(&wc);

    g_hwnd = CreateWindowW(
        cls, L"Text RPG — Dashboard", // 기본은 정직한 게임 제목. 위장 모드 켜면 DashboardFrame에서 바꿈.
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

    // 창 제목/작업표시줄 아이콘은 위장 모드 상태를 그대로 반영 — 기본은 정직하게
    // "Text RPG" + 젬 아이콘, 위장 모드 켜면 "sync agent" + sync 아이콘으로 바뀜.
    // 창 아이콘은 wc.hIcon(생성 시 고정값)과 별개로 WM_SETICON으로 런타임에 덮어써야
    // 작업표시줄에도 반영됨 (트레이 아이콘과 같은 이유로 처음엔 안 바뀌었었음).
    static bool titleInit = false;
    static bool lastDisguise = false;
    if (!titleInit || state.disguiseMode != lastDisguise) {
        SetWindowTextW(g_hwnd, state.disguiseMode ? L"sync agent — dashboard" : L"Text RPG — Dashboard");
        HICON icon = LoadIconW(g_hInst, MAKEINTRESOURCEW(state.disguiseMode ? IDI_APPICON_SYNC : IDI_APPICON));
        if (icon) {
            SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
            SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        }
        lastDisguise = state.disguiseMode;
        titleInit = true;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    DashboardDrawUI(state);

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

#endif // _WIN32
