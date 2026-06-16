#include "game.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cmath>
#include <random>

static std::mt19937 g_rng{ std::random_device{}() };

// ---- 업그레이드 초기화 -------------------------------------------------------
static void InitUpgrades(GameState& state) {
    state.upgrades[UP_XP]   = { "수련 강도", "XP 획득량 +20% / 레벨",  0, 10,  50, 0.20f };
    state.upgrades[UP_GOLD] = { "채집 효율", "골드 획득량 +20% / 레벨", 0, 10,  80, 0.20f };
    state.upgrades[UP_DROP] = { "탐색 본능", "드랍률 +10% / 레벨",      0, 10, 120, 0.10f };
}

// ---- 던전 스테이지 초기화 ---------------------------------------------------
static long long EnemyHpForStage(int stage, bool boss) {
    long long hp = 30LL + stage * 20LL;
    return boss ? hp * 5 : hp;
}

static void InitDungeonStage(Dungeon& d) {
    d.bossStage  = (d.stage % 5 == 0);
    d.enemyMaxHp = EnemyHpForStage(d.stage, d.bossStage);
    d.enemyHp    = d.enemyMaxHp;
}

// ---- 공개 API ---------------------------------------------------------------
long long GetUpgradeCost(const Upgrade& u) {
    return (long long)(u.baseCost * std::pow(2.0, u.level));
}

bool PurchaseUpgrade(GameState& state, int id) {
    if (id < 0 || id >= UP_COUNT) return false;
    Upgrade& u = state.upgrades[id];
    if (u.level >= u.maxLevel) return false;
    long long cost = GetUpgradeCost(u);
    if (state.gold < cost) return false;
    state.gold -= cost;
    u.level++;
    return true;
}

std::wstring GameTick(GameState& state) {
    if (state.dungeon.enemyMaxHp == 0)
        InitDungeonStage(state.dungeon);

    // 업그레이드 배율 계산
    float xpMult   = 1.0f + state.upgrades[UP_XP].level   * state.upgrades[UP_XP].multiplier;
    float goldMult = 1.0f + state.upgrades[UP_GOLD].level  * state.upgrades[UP_GOLD].multiplier;

    long long xpGain   = (long long)((5 + state.level * 2) * xpMult);
    long long goldGain = (long long)((3 + state.level)     * goldMult);
    state.xp   += xpGain;
    state.gold += goldGain;

    std::wstring notify;

    // 던전 전투
    long long atk = (long long)state.level * 10;
    state.dungeon.enemyHp -= atk;
    if (state.dungeon.enemyHp <= 0) {
        long long reward   = state.dungeon.stage * 10LL * (state.dungeon.bossStage ? 3 : 1);
        long long xpReward = state.dungeon.stage * 5LL;
        state.gold += reward;
        state.xp   += xpReward;

        wchar_t buf[128];
        if (state.dungeon.bossStage)
            swprintf_s(buf, L"[sync] 보스 처치! 스테이지 %d 클리어 (+%lld G)", state.dungeon.stage, reward);
        else
            swprintf_s(buf, L"[sync] 스테이지 %d 클리어 (+%lld G)", state.dungeon.stage, reward);
        state.lastEvent = buf;
        if (state.dungeon.bossStage) notify = buf; // 보스만 알림, 일반 클리어는 조용히

        state.dungeon.stage++;
        InitDungeonStage(state.dungeon);
    }

    // 레벨업
    while (state.xp >= state.xpForNext()) {
        state.xp -= state.xpForNext();
        state.level++;
        wchar_t buf[64];
        swprintf_s(buf, L"[sync] 레벨 %d 달성", state.level);
        state.lastEvent = buf;
        if (notify.empty()) notify = buf;
    }

    // 아이템 드랍
    float dropMult = 1.0f + state.upgrades[UP_DROP].level * state.upgrades[UP_DROP].multiplier;
    int   dropChance = (int)(6.0f * dropMult);
    std::uniform_int_distribution<int> roll(1, 100);
    if (roll(g_rng) <= dropChance) {
        state.items++;
        std::uniform_int_distribution<int> bonus(20, 60);
        long long b = bonus(g_rng);
        state.gold += b;
        wchar_t buf[64];
        swprintf_s(buf, L"[sync] 유물 #%lld 회수 (+%lld G)", state.items, b);
        state.lastEvent = buf;
        if (notify.empty()) notify = buf;
    }

    return notify;
}

// ---- 저장 / 불러오기 ---------------------------------------------------------
static std::wstring SavePath() {
    wchar_t appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::wstring dir = std::wstring(appdata) + L"\\idlegame";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\save.txt";
    }
    return L"save.txt";
}

void SaveGame(const GameState& state) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, SavePath().c_str(), L"w") != 0 || !f) return;
    fprintf(f, "%d %lld %lld %lld\n", state.level, state.xp, state.gold, state.items);
    for (int i = 0; i < UP_COUNT; i++) fprintf(f, "%d ", state.upgrades[i].level);
    fprintf(f, "\n%d\n", state.dungeon.stage);
    fclose(f);
}

void LoadGame(GameState& state) {
    InitUpgrades(state);
    FILE* f = nullptr;
    if (_wfopen_s(&f, SavePath().c_str(), L"r") != 0 || !f) return;
    if (fscanf_s(f, "%d %lld %lld %lld",
                 &state.level, &state.xp, &state.gold, &state.items) == 4) {
        for (int i = 0; i < UP_COUNT; i++)
            fscanf_s(f, "%d", &state.upgrades[i].level);
        fscanf_s(f, "%d", &state.dungeon.stage);
    } else {
        state = GameState{};
        InitUpgrades(state);
    }
    fclose(f);
}
