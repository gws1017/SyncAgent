#include "game.h"
#include "equipment.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cmath>
#include <random>

static std::mt19937 g_rng{ std::random_device{}() };

// ---- 클래스 정의 ------------------------------------------------------------
const ClassDef kClasses[3] = {
    {
        "전사",
        "단단하고 안정적인 근접 전투",
        "공격력 x1.5",
        "보스 데미지 x2.0",
        "골드 획득 x1.2",
    },
    {
        "마법사",
        "강력하지만 불안정한 원거리 마법",
        "기본 공격 x0.7",
        "20% 확률로 x4.0 폭발 데미지",
        "XP 획득 x1.5",
    },
    {
        "도적",
        "빠르고 날쌘 근접 약탈",
        "매 틱 2회 공격",
        "아이템 드랍률 x2.0",
        "골드 획득 x1.3",
    },
};

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

    // 업그레이드 + 클래스 배율 계산
    float xpMult   = 1.0f + state.upgrades[UP_XP].level   * state.upgrades[UP_XP].multiplier;
    float goldMult = 1.0f + state.upgrades[UP_GOLD].level  * state.upgrades[UP_GOLD].multiplier;
    float dropMult = 1.0f + state.upgrades[UP_DROP].level  * state.upgrades[UP_DROP].multiplier;

    if (state.playerClass == CLASS_MAGE)    xpMult   *= 1.5f;
    if (state.playerClass == CLASS_WARRIOR) goldMult *= 1.2f;
    if (state.playerClass == CLASS_ROGUE)   goldMult *= 1.3f;
    if (state.playerClass == CLASS_ROGUE)   dropMult *= 2.0f;

    // 장착 아이템 스탯 반영
    xpMult   += GetEquippedBonus(state.inventory, StatType::Xp);
    goldMult += GetEquippedBonus(state.inventory, StatType::Gold);
    dropMult += GetEquippedBonus(state.inventory, StatType::Drop);

    long long xpGain   = (long long)((5 + state.level * 2) * xpMult);
    long long goldGain = (long long)((3 + state.level)     * goldMult);
    state.xp   += xpGain;
    state.gold += goldGain;

    std::wstring notify;

    // 던전 전투 — 클래스별 공격 방식
    float atkMult = 1.0f + GetEquippedBonus(state.inventory, StatType::Attack);
    long long baseAtk = (long long)(state.level * 10 * atkMult);
    long long totalDmg = 0;
    switch (state.playerClass) {
    case CLASS_WARRIOR:
        totalDmg = (long long)(baseAtk * 1.5f);
        if (state.dungeon.bossStage) totalDmg *= 2;  // 보스 특화
        break;
    case CLASS_MAGE: {
        std::uniform_int_distribution<int> critRoll(1, 100);
        totalDmg = (critRoll(g_rng) <= 20)
                   ? (long long)(baseAtk * 4.0f)   // 폭발 데미지
                   : (long long)(baseAtk * 0.7f);  // 일반
        break;
    }
    case CLASS_ROGUE:
        totalDmg = baseAtk * 2;  // 2회 공격
        break;
    default:
        totalDmg = baseAtk;
        break;
    }
    state.dungeon.enemyHp -= totalDmg;
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

    // 레벨업 (대시보드 현황에만 표시, 알림 없음)
    while (state.xp >= state.xpForNext()) {
        state.xp -= state.xpForNext();
        state.level++;
        wchar_t buf[64];
        swprintf_s(buf, L"[sync] 레벨 %d 달성", state.level);
        state.lastEvent = buf;
    }

    // 아이템 드랍
    int dropChance = (int)(6.0f * dropMult);
    std::uniform_int_distribution<int> roll(1, 100);
    if (roll(g_rng) <= dropChance) {
        state.items++;
        Item dropped = MakeItem(Grade::Common);
        if ((int)state.inventory.items.size() < Inventory::MAX_ITEMS) {
            state.inventory.items.push_back(dropped);
        }
        wchar_t buf[64];
        swprintf_s(buf, L"[sync] %s 아이템 획득 (%s +%.0f%%)",
                   GradeNameW(dropped.grade),
                   StatNameW(dropped.stat),
                   dropped.bonus * 100.0f);
        state.lastEvent = buf;
        if (notify.empty() && dropped.grade >= Grade::Rare) notify = buf;
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
    fprintf(f, "\n%d\n%d\n", state.dungeon.stage, (int)state.playerClass);
    std::string inv = SerializeInventory(state.inventory);
    fprintf(f, "%s\n", inv.c_str());
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
        int cls = 0;
        fscanf_s(f, "%d%d", &state.dungeon.stage, &cls);
        state.playerClass = (ClassType)cls;
        char invBuf[4096] = {};
        if (fscanf_s(f, " %4095[^\n]", invBuf, (unsigned)sizeof(invBuf)) == 1)
            DeserializeInventory(invBuf, state.inventory);
    } else {
        state = GameState{};
        InitUpgrades(state);
    }
    fclose(f);
}
