#include "game.h"
#include "equipment.h"
#include "platform.h"
#include <cstdio>
#include <cmath>
#include <random>
#include <algorithm>

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
    state.upgrades[UP_ATK]  = { "전투 본능", "공격력 +15% / 레벨",      0, 10, 100, 0.15f };
}

// ---- 특성 -------------------------------------------------------------------
// 레벨업마다 1포인트 지급. 3가지 다 최대까지 찍으려면 30포인트가 필요해서
// (느려진 XP 곡선 기준) 항상 부족하게 만들어 투자 선택을 강제함.
// 클래스 컨셉에 맞춰 슬롯별 효과가 다름 (전사=탱커, 마법사=폭딜+자가회복, 도적=회피+다회타).
const Talent kTalentDefs[3][TAL_COUNT] = {
    { // 전사 — 단단한 근접 탱커
        { "철벽 방어 (패시브)",   "방어력 +10% / 포인트", 0, 10 },
        { "전장의 분노 (패시브)", "공격력 +8% / 포인트",  0, 10 },
        { "처단자 (패시브)",      "보스 데미지 +15% / 포인트", 0, 10 },
    },
    { // 마법사 — 불안정한 폭딜, 자가 회복으로 보완
        { "마나 증폭 (패시브)",   "폭발 데미지 +15% / 포인트",   0, 10 },
        { "주문 가속 (패시브)",   "폭발 확률 +2%p / 포인트",     0, 10 },
        { "마력 순환 (패시브)",   "체력흡수 +3% / 포인트",       0, 10 },
    },
    { // 도적 — 빠르고 잘 피하는 다회타
        { "연속 공격 (패시브)",   "공격속도 +10% / 포인트",      0, 10 },
        { "기습 (확률)",          "추가 공격 확률 +4%p / 포인트", 0, 10 },
        { "은신 회피 (패시브)",   "받는 피해 -5% / 포인트",       0, 10 },
    },
};

void InitTalentsForClass(GameState& state) {
    if (state.playerClass == CLASS_NONE) return;
    int idx = (int)state.playerClass - 1;
    for (int i = 0; i < TAL_COUNT; i++) {
        int savedLevel = state.talents[i].level;
        state.talents[i] = kTalentDefs[idx][i];
        state.talents[i].level = savedLevel; // 레벨 보존 (저장 불러오기/순서 무관하게 안전)
    }
}

TalentBonuses ComputeTalentBonuses(const GameState& state) {
    TalentBonuses b;
    switch (state.playerClass) {
    case CLASS_WARRIOR:
        b.defenseBonus = state.talents[TAL_0].level * 0.10f; // 철벽 방어
        b.atkBonus     = state.talents[TAL_1].level * 0.08f; // 전장의 분노
        b.bossDmgBonus = state.talents[TAL_2].level * 0.15f; // 처단자
        break;
    case CLASS_MAGE:
        b.critDmgBonus    = state.talents[TAL_0].level * 0.15f; // 마나 증폭
        b.critChanceBonus = state.talents[TAL_1].level * 2.0f;  // 주문 가속
        b.lifestealBonus  = state.talents[TAL_2].level * 0.03f; // 마력 순환
        break;
    case CLASS_ROGUE:
        b.atkSpeedBonus  = state.talents[TAL_0].level * 0.10f; // 연속 공격
        b.extraAtkChance = state.talents[TAL_1].level * 4.0f;  // 기습
        b.evasionBonus   = state.talents[TAL_2].level * 0.05f; // 은신 회피
        break;
    default:
        break;
    }
    return b;
}

// ---- 던전 스테이지 초기화 ---------------------------------------------------
// 스테이지가 오를수록 지수적으로 어려워짐 — 레벨업만으로는 못 따라가고
// 업그레이드/장비 투자가 있어야 계속 진행 가능 (TBH 스타일 진행 장벽)
static long long EnemyHpForStage(int stage, bool boss) {
    long long hp = (long long)(30.0 * std::pow(1.18, stage - 1));
    return boss ? hp * 5 : hp;
}

// 적 방어력 — 공격력이 이걸 못 넘으면 데미지가 0이 되어 진짜로 진행이 멈춤
// (HP가 유한하면 데미지>0인 한 언젠가 깨지므로, 진짜 교착상태는 방어력으로만 만들 수 있음)
long long EnemyDefForStage(int stage) {
    return (long long)(5.0 + stage * 1.8);
}

// 적 반격 — 플레이어도 맞는다. 방어력/체력흡수 투자가 없으면 못 버티고 전투가 리셋됨.
long long EnemyAtkForStage(int stage) {
    return (long long)(4.0 + stage * 1.5);
}

// XP 곡선 — 지수 증가. 레벨이 오를수록 한 단계 올리는 데 필요한 경험치가 급격히 커져서
// (예: 55레벨대면 요구치가 억 단위) 레벨업 자체가 점점 큰 이벤트가 됨.
long long XpForLevel(int level) {
    return (long long)(50.0 * std::pow(1.30, level));
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

bool InvestTalent(GameState& state, int id) {
    if (id < 0 || id >= TAL_COUNT) return false;
    Talent& t = state.talents[id];
    if (t.level >= t.maxLevel) return false;
    if (state.talentPoints <= 0) return false;
    state.talentPoints--;
    t.level++;
    return true;
}

long long PrestigeRequirement(int prestigeCount) {
    return PRESTIGE_STAGE_REQ + (long long)prestigeCount * 10;
}

void DoPrestige(GameState& state) {
    // 유지: 클래스, 장착 장비, 프레스티지 횟수
    ClassType          cls      = state.playerClass;
    std::vector<Item>  equipped = state.inventory.equipped;
    int                pc       = state.prestigeCount + 1;

    state = GameState{};
    InitUpgrades(state);

    state.playerClass          = cls;
    InitTalentsForClass(state);
    state.inventory.equipped   = equipped;
    state.prestigeCount        = pc;
    state.lastEvent            = L"[sync] 프레스티지 완료";
}

std::wstring GameTick(GameState& state) {
    if (state.dungeon.enemyMaxHp == 0)
        InitDungeonStage(state.dungeon);

    // 업그레이드 + 클래스 배율 계산
    float xpMult   = 1.0f + state.upgrades[UP_XP].level   * state.upgrades[UP_XP].multiplier;
    float goldMult = 1.0f + state.upgrades[UP_GOLD].level  * state.upgrades[UP_GOLD].multiplier;
    float dropMult = 1.0f + state.upgrades[UP_DROP].level  * state.upgrades[UP_DROP].multiplier;

    // 프레스티지 영구 보너스
    float prestigeBonus = state.prestigeCount * 0.15f;
    xpMult   += prestigeBonus;
    goldMult += prestigeBonus;
    dropMult += prestigeBonus;

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

    // 플레이어 체력 — 프레스티지마다 최대치 소폭 증가
    long long maxHp = 200 + (long long)state.prestigeCount * 20;
    state.playerMaxHp = maxHp;
    if (state.playerHp <= 0 || state.playerHp > maxHp) state.playerHp = maxHp;

    // 던전 전투 — 클래스별 공격 방식
    // 공격력은 레벨과 무관하게 고정 기반값 + 투자(업그레이드/장비/프레스티지/특성)로만 상승.
    // 레벨업만으로는 절대 못 따라가게 만들어, 장비/업그레이드/특성 투자를 강제하는 진행 장벽 역할.
    constexpr long long BASE_ATK = 10;
    TalentBonuses tal = ComputeTalentBonuses(state);
    float atkMult = 1.0f + GetEquippedBonus(state.inventory, StatType::Attack)
                         + state.prestigeCount * 0.10f
                         + state.upgrades[UP_ATK].level * state.upgrades[UP_ATK].multiplier
                         + tal.atkBonus;
    float atkSpeedMult = 1.0f + GetEquippedBonus(state.inventory, StatType::AtkSpeed)
                               + tal.atkSpeedBonus;
    long long baseAtk = (long long)(BASE_ATK * atkMult);
    long long totalDmg = 0;
    switch (state.playerClass) {
    case CLASS_WARRIOR:
        totalDmg = (long long)(baseAtk * 1.5f);
        if (state.dungeon.bossStage) {
            totalDmg *= 2;  // 보스 특화
            totalDmg = (long long)(totalDmg * (1.0f + tal.bossDmgBonus)); // 처단자
        }
        break;
    case CLASS_MAGE: {
        float critChance = std::min(100.0f, 20.0f + tal.critChanceBonus); // 주문 가속
        float critMult    = 4.0f + tal.critDmgBonus;                      // 마나 증폭
        std::uniform_real_distribution<float> critRoll(0.0f, 100.0f);
        totalDmg = (critRoll(g_rng) <= critChance)
                   ? (long long)(baseAtk * critMult)  // 폭발 데미지
                   : (long long)(baseAtk * 0.7f);      // 일반
        break;
    }
    case CLASS_ROGUE:
        totalDmg = baseAtk * 2;  // 2회 공격
        break;
    default:
        totalDmg = baseAtk;
        break;
    }
    totalDmg = (long long)(totalDmg * atkSpeedMult);

    // 기습(도적) — 포인트당 추가 공격 확률
    if (tal.extraAtkChance > 0.0f) {
        std::uniform_real_distribution<float> procRoll(0.0f, 100.0f);
        if (procRoll(g_rng) <= tal.extraAtkChance) totalDmg *= 2;
    }

    long long enemyDef = EnemyDefForStage(state.dungeon.stage);
    totalDmg = std::max(0LL, totalDmg - enemyDef);
    state.dungeon.enemyHp -= totalDmg;

    // 체력흡수 — 실제로 들어간 데미지 기준으로 회복 (장비 + 마법사 마력 순환)
    float lifestealPct = GetEquippedBonus(state.inventory, StatType::Lifesteal) + tal.lifestealBonus;
    if (totalDmg > 0 && lifestealPct > 0.0f) {
        long long heal = (long long)(totalDmg * lifestealPct);
        state.playerHp = std::min(maxHp, state.playerHp + heal);
    }

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

    // 적 반격 — 방어력/체력흡수 투자가 부족하면 죽어서 전투가 리셋됨 (스테이지는 유지)
    long long playerDef = (long long)(10.0 * (1.0f + GetEquippedBonus(state.inventory, StatType::Defense) + tal.defenseBonus));
    long long enemyAtk   = EnemyAtkForStage(state.dungeon.stage);
    long long dmgToPlayer = std::max(0LL, enemyAtk - playerDef);
    dmgToPlayer = (long long)(dmgToPlayer * (1.0f - std::min(0.9f, tal.evasionBonus))); // 은신 회피
    state.playerHp -= dmgToPlayer;
    if (state.playerHp <= 0) {
        state.playerHp = state.playerMaxHp;
        state.dungeon.enemyHp = state.dungeon.enemyMaxHp;
        state.lastEvent = L"[sync] 패배 — 전투 초기화. 방어력/체력흡수를 보강하세요.";
        // 막혀서 매 틱 죽는 상황에선 알림이 도배되므로 토스트는 띄우지 않음
    }

    // 레벨업 (대시보드 현황에만 표시, 알림 없음) — 레벨업마다 특성 포인트 1개 지급
    while (state.xp >= state.xpForNext()) {
        state.xp -= state.xpForNext();
        state.level++;
        state.talentPoints++;
        wchar_t buf[64];
        swprintf_s(buf, L"[sync] 레벨 %d 달성 (특성 포인트 +1)", state.level);
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
// 경로 탐색은 platform.h 뒤로 위임 (OS별 구현은 platform_win.cpp 등)

void SaveGame(const GameState& state) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, GetSaveFilePath().c_str(), L"w") != 0 || !f) return;
    fprintf(f, "%d %lld %lld %lld\n", state.level, state.xp, state.gold, state.items);
    for (int i = 0; i < UP_COUNT; i++) fprintf(f, "%d ", state.upgrades[i].level);
    fprintf(f, "\n%d\n%d\n%d\n", state.dungeon.stage, (int)state.playerClass, state.prestigeCount);
    std::string inv = SerializeInventory(state.inventory);
    fprintf(f, "%s\n", inv.c_str());
    fprintf(f, "%d ", state.talentPoints);
    for (int i = 0; i < TAL_COUNT; i++) fprintf(f, "%d ", state.talents[i].level);
    fprintf(f, "\n");
    fclose(f);
}

void LoadGame(GameState& state) {
    InitUpgrades(state);
    FILE* f = nullptr;
    if (_wfopen_s(&f, GetSaveFilePath().c_str(), L"r") != 0 || !f) return;
    if (fscanf_s(f, "%d %lld %lld %lld",
                 &state.level, &state.xp, &state.gold, &state.items) == 4) {
        for (int i = 0; i < UP_COUNT; i++)
            fscanf_s(f, "%d", &state.upgrades[i].level);
        int cls = 0;
        fscanf_s(f, "%d%d%d", &state.dungeon.stage, &cls, &state.prestigeCount);
        state.playerClass = (ClassType)cls;
        InitTalentsForClass(state);
        char invBuf[4096] = {};
        if (fscanf_s(f, " %4095[^\n]", invBuf, (unsigned)sizeof(invBuf)) == 1)
            DeserializeInventory(invBuf, state.inventory);
        if (fscanf_s(f, "%d", &state.talentPoints) == 1) {
            for (int i = 0; i < TAL_COUNT; i++)
                fscanf_s(f, "%d", &state.talents[i].level);
        }
    } else {
        state = GameState{};
        InitUpgrades(state);
    }
    fclose(f);
}
