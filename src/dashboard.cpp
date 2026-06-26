#include "dashboard.h"
#include "equipment.h"
#include "lang.h"
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

static constexpr int W = 460, H = 480;

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
    ImGui::TextDisabled("%s", T("직업을 선택하세요  (프레스티지 전까지는 변경 불가)",
                                 "Choose a class  (locked until you prestige)"));
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < 3; i++) {
        const ClassDef& c = kClasses[i];
        ImGui::PushID(i);

        // 직업 이름 버튼
        if (ImGui::Button(c.Name(), {80, 48})) {
            state.playerClass = (ClassType)(i + 1);
            InitTalentsForClass(state);
            SaveGame(state);
        }
        ImGui::SameLine(100);

        // 설명 블록
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", c.Flavor());
        ImGui::BulletText("%s", c.Stat0());
        ImGui::BulletText("%s", c.Stat1());
        ImGui::BulletText("%s", c.Stat2());
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
    ImGui::Spacing();

    ImGui::TextDisabled("IDLE AGENT  v0.1");
    ImGui::Separator();
    ImGui::Spacing();

    char evt[128] = {};
    ToUtf8(state.lastEvent, evt, sizeof(evt));
    ImGui::TextDisabled("%s", T("최근 이벤트", "Recent event"));
    ImGui::TextWrapped("%s", evt);

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
    ImGui::Text(T("%lld 회  (이번 캐릭터 기준)", "%lld  (this character)"), state.deathCount);

    ImGui::Spacing();
    ImGui::Separator();
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
}

static void TabUpgrade(GameState& state) {
    ImGui::Spacing();

    ImGui::Text("%s", T("골드", "Gold"));      ImGui::SameLine(100); ImGui::Text("%lld G", state.gold);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < UP_COUNT; i++) {
        Upgrade& u = state.upgrades[i];
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

        bool canBuy = !maxed && state.gold >= cost;
        if (!canBuy) ImGui::BeginDisabled();
        if (ImGui::Button(btnLabel, {90, 0}))
            PurchaseUpgrade(state, i);
        if (!canBuy) ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled(T("프레스티지  (%d회 진행)", "Prestige  (%d done)"), state.prestigeCount);
    ImGui::TextWrapped(T("회당 보너스: XP/골드/드랍률 +%.0f%%,  공격력 +%.0f%%,  최대체력 +%lld"
                          "   (지금까지 %d회 × 위 수치 = 현재 적용 중인 총 보너스)",
                          "Per run: +%.0f%% XP/gold/drop rate, +%.0f%% attack, +%lld max HP"
                          "   (x%d runs so far = current total bonus)"),
                        PRESTIGE_ECON_BONUS * 100.0f, PRESTIGE_ATK_BONUS * 100.0f, PRESTIGE_HP_BONUS,
                        state.prestigeCount);
    ImGui::TextDisabled("%s", T("실행하면 레벨/골드/장비(보관함)/스테이지가 초기화되고 직업을 다시 고릅니다.",
                                 "Resets level/gold/gear(bag)/stage and lets you pick a class again."));
    long long req = PrestigeRequirement(state.prestigeCount);
    bool canPrestige = state.dungeon.stage >= req;
    if (!canPrestige) ImGui::BeginDisabled();
    if (ImGui::Button(T("프레스티지 실행", "Prestige Now"), {320, 0})) {
        DoPrestige(state);
        SaveGame(state);
    }
    if (!canPrestige) ImGui::EndDisabled();
    if (!canPrestige)
        ImGui::TextDisabled(T("스테이지 %lld 이상 도달 시 해금", "Unlocks at stage %lld"), req);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    static bool confirmReset = false;
    ImGui::TextDisabled("%s", T("세이브 초기화", "Reset Save"));
    ImGui::Checkbox(T("정말로 초기화할게요 (되돌릴 수 없음)", "Yes, really reset (cannot be undone)"), &confirmReset);
    if (!confirmReset) ImGui::BeginDisabled();
    if (ImGui::Button(T("세이브 초기화 실행", "Reset Save Now"), {320, 0})) {
        ResetGame(state);
        SaveGame(state);
        confirmReset = false;
    }
    if (!confirmReset) ImGui::EndDisabled();
}

// slotStart..slotStart+count-1 구간의 특성 목록을 그려주는 공용 헬퍼 (1차/2차 공용)
static void DrawTalentRows(GameState& state, int slotStart, int count, int& pool) {
    for (int i = slotStart; i < slotStart + count; i++) {
        Talent& t = state.talents[i];
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
            InvestTalent(state, i);
        if (!canInvest) ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::PopID();
    }
}

static void TabTalent(GameState& state) {
    ImGui::Spacing();
    int spent1 = state.talents[TAL_0].level + state.talents[TAL_1].level + state.talents[TAL_2].level;
    ImGui::TextDisabled(T("1차 특성  —  레벨업마다 1포인트, 평생 최대 %d포인트 (3개 다 풀업은 불가능)",
                           "Tier 1 talents  —  1 point/level-up, %d lifetime cap (can't max all 3)"),
                         MAX_TALENT_POINTS);
    ImGui::Text("%s", T("보유 포인트", "Points available")); ImGui::SameLine(140); ImGui::Text("%d", state.talentPoints);
    ImGui::Text("%s", T("누적 (보유+투자)", "Total earned"));   ImGui::SameLine(140); ImGui::Text("%d / %d", state.talentPoints + spent1, MAX_TALENT_POINTS);
    ImGui::Separator();
    ImGui::Spacing();

    DrawTalentRows(state, TAL_0, TIER1_TAL_COUNT, state.talentPoints);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (state.level < TIER2_LEVEL_REQ) {
        ImGui::TextDisabled(T("2차 특성  —  레벨 %d부터 해금 (현재 Lv.%d)", "Tier 2 talents  —  unlocks at Lv.%d (currently Lv.%d)"),
                             TIER2_LEVEL_REQ, state.level);
    } else {
        int spent2 = state.talents[TAL_3].level + state.talents[TAL_4].level + state.talents[TAL_5].level;
        ImGui::TextDisabled(T("2차 특성  —  레벨업마다 1포인트, 평생 최대 %d포인트 (1차와 별도 풀)",
                               "Tier 2 talents  —  1 point/level-up, %d lifetime cap (separate pool from tier 1)"),
                             MAX_TALENT_POINTS_T2);
        ImGui::Text("%s", T("보유 포인트", "Points available")); ImGui::SameLine(140); ImGui::Text("%d", state.talentPoints2);
        ImGui::Text("%s", T("누적 (보유+투자)", "Total earned"));   ImGui::SameLine(140); ImGui::Text("%d / %d", state.talentPoints2 + spent2, MAX_TALENT_POINTS_T2);
        ImGui::Separator();
        ImGui::Spacing();

        DrawTalentRows(state, TAL_3, TIER2_TAL_COUNT, state.talentPoints2);
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

    for (int i = 0; i < (int)inv.items.size(); i++) {
        const Item& it = inv.items[i];
        ImGui::PushID(i);
        ImGui::TextColored(GradeColor(it.grade), "[%s]", GradeName(it.grade));
        ImGui::SameLine();
        ImGui::Text("%s +%.0f%%", StatName(it.stat), it.bonus * 100.0f);
        ImGui::SameLine(300);
        bool canEquip = (int)inv.equipped.size() < Inventory::MAX_EQUIP;
        if (!canEquip) ImGui::BeginDisabled();
        if (ImGui::SmallButton(T("장착", "Equip"))) TryEquip(inv, i);
        if (!canEquip) ImGui::EndDisabled();
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
    Dungeon& d = state.dungeon;
    ImGui::Spacing();

    // ---- 캐릭터 상태 (적 체력과 바로 비교되도록 같은 너비로 위에 배치) ----------
    char clsBuf[64];
    const char* clsName = (state.playerClass != CLASS_NONE)
                          ? kClasses[(int)state.playerClass - 1].Name() : "?";
    snprintf(clsBuf, sizeof(clsBuf), "Lv.%d  [%s]", state.level, clsName);
    ImGui::Text("%s", T("영웅", "Hero"));      ImGui::SameLine(100); ImGui::Text("%s", clsBuf);

    ImGui::Spacing();
    ImGui::Text("XP");        ImGui::SameLine(100);
    char xpOverlay[32];
    snprintf(xpOverlay, sizeof(xpOverlay), "%lld / %lld", state.xp, state.xpForNext());
    ImGui::ProgressBar(state.xpProgress(), {320, 0}, xpOverlay);

    ImGui::Spacing();
    ImGui::Text("%s", T("내 체력", "My HP"));    ImGui::SameLine(100);
    char playerHpOverlay[32];
    snprintf(playerHpOverlay, sizeof(playerHpOverlay), "%lld / %lld", state.playerHp, state.playerMaxHp);
    float playerHpFrac = (state.playerMaxHp > 0) ? (float)state.playerHp / (float)state.playerMaxHp : 0.0f;
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

    TalentBonuses tal = ComputeTalentBonuses(state);
    float atkMult = 1.0f + GetEquippedBonus(state.inventory, StatType::Attack)
                         + state.prestigeCount * PRESTIGE_ATK_BONUS
                         + state.upgrades[UP_ATK].level * state.upgrades[UP_ATK].multiplier
                         + tal.atkBonus;
    // 공격속도는 데미지 배율이 아니라 "한 틱에 평균 몇 번 때리는지"를 나타냄 (game.cpp와 동일 모델)
    float atkSpeedBonus = GetEquippedBonus(state.inventory, StatType::AtkSpeed) + tal.atkSpeedBonus;
    float expectedAttacks = 1.0f + atkSpeedBonus;
    long long baseAtk = (long long)(PlayerBaseAtk(d.stage) * atkMult);
    long long enemyDef = EnemyDefForStage(d.stage);
    long long enemyAtk = EnemyAtkForStage(d.stage);
    long long playerDef = (long long)(PlayerBaseDef(d.stage) * (1.0f + GetEquippedBonus(state.inventory, StatType::Defense) + tal.defenseBonus));
    float lifestealPct = GetEquippedBonus(state.inventory, StatType::Lifesteal) + tal.lifestealBonus;
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
        if (state.lastHealAmount > 0)
            ImGui::TextColored({0.4f, 0.9f, 0.5f, 1.0f}, T("  ↳ 방금 +%lld 회복", "  -> just healed +%lld"), state.lastHealAmount);
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
    switch (state.playerClass) {
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

    auto TabScrollable = [](const char* id, void (*fn)(GameState&), GameState& s) {
        ImGui::BeginChild(id, {0, 0}, false);
        fn(s);
        ImGui::EndChild();
    };

    if (state.playerClass == CLASS_NONE) {
        ScreenClassSelect(state);
    } else if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem(T("현황", "Status")))    { TabScrollable("##s1", TabStatus,    state); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(T("업그레이드", "Upgrade"))) { TabScrollable("##s2", TabUpgrade,   state); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(T("특성", "Talents")))   { TabScrollable("##s3", TabTalent,    state); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(T("던전", "Dungeon")))   { TabScrollable("##s4", TabDungeon,   state); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(T("장비", "Gear")))      { TabScrollable("##s5", TabEquipment, state); ImGui::EndTabItem(); }
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
