#include "game.h"
#include "equipment.h"
#include "platform.h"
#include "lang.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <random>
#include <algorithm>
#include <map>
#include <sstream>
#include <string>

static std::mt19937 g_rng{ std::random_device{}() };

// ---- 클래스 정의 ------------------------------------------------------------
const ClassDef kClasses[3] = {
    {
        "전사", "Warrior",
        "단단하고 안정적인 근접 전투", "Sturdy, reliable melee combat",
        "공격력 x1.5", "Attack x1.5",
        "보스 데미지 x2.0", "Boss damage x2.0",
        "골드 획득 x1.2", "Gold gain x1.2",
    },
    {
        "마법사", "Mage",
        "강력하지만 불안정한 원거리 마법", "Powerful but unstable ranged magic",
        "기본 공격 x0.7", "Base attack x0.7",
        "20% 확률로 x4.0 폭발 데미지", "20% chance of x4.0 burst damage",
        "XP 획득 x1.5", "XP gain x1.5",
    },
    {
        "도적", "Rogue",
        "빠르고 날쌘 근접 약탈", "Fast, nimble melee plunder",
        "매 틱 2회 공격", "Attacks twice per tick",
        "아이템 드랍률 x2.0", "Item drop rate x2.0",
        "골드 획득 x1.3", "Gold gain x1.3",
    },
};

// ---- 업그레이드 초기화 -------------------------------------------------------
static void InitUpgrades(Hero& hero) {
    hero.upgrades[UP_XP]   = { "수련 강도", "Training",  "XP 획득량 +20% / 레벨",  "+20% XP gain / level",   0, 10,  50, 0.20f };
    hero.upgrades[UP_GOLD] = { "채집 효율", "Gathering", "골드 획득량 +20% / 레벨", "+20% gold gain / level", 0, 10,  80, 0.20f };
    hero.upgrades[UP_DROP] = { "탐색 본능", "Scavenging","드랍률 +10% / 레벨",      "+10% drop rate / level", 0, 10, 120, 0.10f };
    hero.upgrades[UP_ATK]  = { "전투 본능", "Combat",    "공격력 +15% / 레벨",      "+15% attack / level",    0, 10, 100, 0.15f };
}

// ---- 특성 -------------------------------------------------------------------
// 1차(슬롯 0~2): 레벨업마다 1포인트, 평생 15포인트 캡 (3개 다 풀업하는 30보다 적음).
// 2차(슬롯 3~5): 25레벨부터 별도 풀로 1포인트씩, 마찬가지로 평생 15포인트 캡.
// 클래스 컨셉에 맞춰 슬롯별 효과가 다름 (전사=탱커, 마법사=폭딜+자가회복, 도적=회피+다회타).
const Talent kTalentDefs[3][TAL_COUNT] = {
    { // 전사 — 단단한 근접 탱커
        { "철벽 방어 (패시브)",   "Iron Wall (Passive)",     "방어력 +10% / 포인트",      "+10% defense / point",      0, 10 },
        { "전장의 분노 (패시브)", "Battle Rage (Passive)",   "공격력 +8% / 포인트",       "+8% attack / point",        0, 10 },
        { "처단자 (패시브)",      "Executioner (Passive)",   "보스 데미지 +15% / 포인트", "+15% boss damage / point",  0, 10 },
        { "불굴의 의지 (패시브)", "Unbreakable (Passive)",   "최대체력 +5% / 포인트",     "+5% max HP / point",        0, 10 },
        { "광전사 (패시브)",      "Berserker (Passive)",     "공격력 +8% / 포인트",       "+8% attack / point",        0, 10 },
        { "수호자 (패시브)",      "Guardian (Passive)",      "방어력 +10% / 포인트",      "+10% defense / point",      0, 10 },
    },
    { // 마법사 — 불안정한 폭딜, 자가 회복으로 보완
        { "마나 증폭 (패시브)",   "Mana Amplify (Passive)",  "폭발 데미지 +15% / 포인트",   "+15% burst damage / point",   0, 10 },
        { "마력 폭주 (패시브)",   "Mana Surge (Passive)",    "폭발 확률 +2%p / 포인트",     "+2%p burst chance / point",   0, 10 },
        { "마나 흡혈 (패시브)",   "Mana Drain (Passive)",    "체력흡수 +3% / 포인트",       "+3% lifesteal / point",       0, 10 },
        { "마나 코어 (패시브)",   "Mana Core (Passive)",     "최대체력 +5% / 포인트",       "+5% max HP / point",          0, 10 },
        { "원소 폭발 (패시브)",   "Elemental Burst (Passive)","폭발 데미지 +15% / 포인트",  "+15% burst damage / point",   0, 10 },
        { "흡혼 (패시브)",        "Soul Drain (Passive)",    "체력흡수 +3% / 포인트",       "+3% lifesteal / point",       0, 10 },
    },
    { // 도적 — 빠르고 잘 피하는 다회타
        { "연속 공격 (패시브)",   "Rapid Strikes (Passive)", "공격속도 +10% / 포인트",       "+10% attack speed / point",      0, 10 },
        { "기습 (확률)",          "Ambush (Chance)",         "추가 공격 확률 +4%p / 포인트", "+4%p extra attack chance / point", 0, 10 },
        { "은신 회피 (패시브)",   "Stealth Evasion (Passive)","받는 피해 -5% / 포인트",      "-5% damage taken / point",       0, 10 },
        { "쾌속 (패시브)",        "Swiftness (Passive)",     "공격속도 +10% / 포인트",       "+10% attack speed / point",      0, 10 },
        { "치명적 기습 (확률)",   "Deadly Ambush (Chance)",  "추가 공격 확률 +4%p / 포인트", "+4%p extra attack chance / point", 0, 10 },
        { "야성 (패시브)",        "Wild Instinct (Passive)", "받는 피해 -5% / 포인트",       "-5% damage taken / point",       0, 10 },
    },
};

void InitTalentsForClass(Hero& hero) {
    int idx = (int)hero.playerClass - 1;
    if (idx < 0 || idx >= 3) return; // CLASS_NONE이거나 손상된 값이면 건너뜀 (배열 범위 밖 접근 방지)
    for (int i = 0; i < TAL_COUNT; i++) {
        int savedLevel = hero.talents[i].level;
        hero.talents[i] = kTalentDefs[idx][i];
        hero.talents[i].level = savedLevel; // 레벨 보존 (저장 불러오기/순서 무관하게 안전)
    }
}

TalentBonuses ComputeTalentBonuses(const Hero& hero) {
    TalentBonuses b;
    switch (hero.playerClass) {
    // 2차 특성(TAL_3~5)이 1차보다 계수가 낮게(20~40%) 설계되어 있어서 "2차가 항상
    // 손해"로 느껴진다는 피드백이 있었음 — 매칭되는 스탯 계수를 1차와 동일하게 맞추고,
    // 체력 보너스만 2차 고유 혜택으로 남김 (그래서 2차 투자가 1차보다 못하지 않게 됨).
    case CLASS_WARRIOR:
        b.defenseBonus = hero.talents[TAL_0].level * 0.10f; // 철벽 방어
        b.atkBonus     = hero.talents[TAL_1].level * 0.08f; // 전장의 분노
        b.bossDmgBonus = hero.talents[TAL_2].level * 0.15f; // 처단자
        b.hpBonus      = hero.talents[TAL_3].level * 0.05f; // 불굴의 의지
        b.atkBonus    += hero.talents[TAL_4].level * 0.08f; // 광전사
        b.defenseBonus+= hero.talents[TAL_5].level * 0.10f; // 수호자
        break;
    case CLASS_MAGE:
        b.critDmgBonus    = hero.talents[TAL_0].level * 0.15f; // 마나 증폭
        b.critChanceBonus = hero.talents[TAL_1].level * 2.0f;  // 마력 폭주
        b.lifestealBonus  = hero.talents[TAL_2].level * 0.03f; // 마나 흡혈
        b.hpBonus         = hero.talents[TAL_3].level * 0.05f; // 마나 코어
        b.critDmgBonus   += hero.talents[TAL_4].level * 0.15f; // 원소 폭발
        b.lifestealBonus += hero.talents[TAL_5].level * 0.03f; // 흡혼
        break;
    case CLASS_ROGUE:
        b.atkSpeedBonus  = hero.talents[TAL_0].level * 0.10f; // 연속 공격
        b.extraAtkChance = hero.talents[TAL_1].level * 4.0f;  // 기습
        b.evasionBonus   = hero.talents[TAL_2].level * 0.05f; // 은신 회피
        b.atkSpeedBonus += hero.talents[TAL_3].level * 0.10f; // 쾌속
        b.extraAtkChance+= hero.talents[TAL_4].level * 4.0f;  // 치명적 기습
        b.evasionBonus  += hero.talents[TAL_5].level * 0.05f; // 야성
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

// 플레이어 기본 스탯 — 몹보다 느린 속도로 같이 성장 (몹 공식과 짝을 이룸).
// 성장률이 몹보다 낮아서 투자 없이는 결국 따라잡히지만, 시작부터 0데미지로
// 막히지는 않음 — "느려지는 장벽" 쪽으로 완화.
long long PlayerBaseAtk(int stage) {
    return (long long)(10.0 + stage * 0.9); // 적 방어력(1.8/stage)의 절반 속도
}

long long PlayerBaseDef(int stage) {
    return (long long)(10.0 + stage * 0.75); // 적 공격력(1.5/stage)의 절반 속도
}

long long PlayerBaseMaxHp(int stage) {
    // 초반이 너무 쉽게 죽는다("개복치")는 피드백으로 기본값을 200→350으로 올림.
    // 스테이지당 성장률(8)은 그대로라 이후 곡선 자체는 안 바뀜, 초반 쿠션만 확보.
    return 350 + (long long)stage * 8;
}

// 방어력 적용 — 비율 기반 감소(diminishing returns). defValue가 커질수록 감소율이
// 100%에 가까워지지만 절대 0이 되지는 않아서, 투자를 아무리 많이 해도 몹 성장이
// 계속 어느 정도의 위협으로 남는다 (반대로 적게 투자해도 너무 약하게 느껴지지 않음).
long long MitigateDamage(long long incomingDmg, long long defValue) {
    if (incomingDmg <= 0) return 0;
    constexpr double DEF_K = 50.0; // 절반 감소가 되는 방어력 기준점
    double reduction = (double)defValue / ((double)defValue + DEF_K);
    return (long long)(incomingDmg * (1.0 - reduction));
}

// 스테이지가 깊어질수록 드랍 등급 분포가 좋아짐 — 20층부터 희귀가 섞이고,
// 40층(프레스티지 해금)부터는 영웅도 소량 나옴 (전설은 합성으로만 획득).
static Grade RollDropGrade(int stage) {
    std::uniform_int_distribution<int> roll(1, 100);
    int r = roll(g_rng);
    if (stage >= 40) {
        if (r <= 5)  return Grade::Epic;
        if (r <= 25) return Grade::Rare; // 5 + 20
        return Grade::Common;
    }
    if (stage >= 20) {
        if (r <= 20) return Grade::Rare;
        return Grade::Common;
    }
    return Grade::Common;
}

// XP 곡선 — 지수 증가. 레벨이 오를수록 한 단계 올리는 데 필요한 경험치가 급격히 커져서
// 레벨업 자체가 점점 큰 이벤트가 됨. 기존 지수 1.30은 특성2 다 찍기도 전에
// "떡벽"처럼 느껴진다는 피드백으로 1.25로 완화 (예: 50레벨 기준 요구치가 약 7배 줄어듦).
long long XpForLevel(int level) {
    return (long long)(50.0 * std::pow(1.25, level));
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

bool PurchaseUpgrade(Hero& hero, int id) {
    if (id < 0 || id >= UP_COUNT) return false;
    Upgrade& u = hero.upgrades[id];
    if (u.level >= u.maxLevel) return false;
    long long cost = GetUpgradeCost(u);
    if (hero.gold < cost) return false;
    hero.gold -= cost;
    u.level++;
    return true;
}

bool InvestTalent(Hero& hero, int id) {
    if (id < 0 || id >= TAL_COUNT) return false;
    bool isTier2 = id >= TIER1_TAL_COUNT;
    if (isTier2 && hero.level < TIER2_LEVEL_REQ) return false; // 2차는 25레벨부터
    int& pool = isTier2 ? hero.talentPoints2 : hero.talentPoints;
    Talent& t = hero.talents[id];
    if (t.level >= t.maxLevel) return false;
    if (pool <= 0) return false;
    pool--;
    t.level++;
    return true;
}

// 영웅 한 명의 투자량(업그레이드+특성+장비)을 계승 보너스 %로 환산.
// 초반엔 작은 보너스, 풀투자 영웅은 큰 보너스 — 여러 영웅에 걸쳐 누적되는 자원.
static float ComputeLegacyGain(const Hero& hero) {
    int investScore = 0;
    for (int i = 0; i < UP_COUNT; i++) investScore += hero.upgrades[i].level;
    for (int i = 0; i < TAL_COUNT; i++) investScore += hero.talents[i].level;
    for (const Item& it : hero.inventory.equipped) investScore += (int)(it.bonus * 100.0f);
    return investScore * 0.15f; // %p
}

bool CreateOrSwitchHero(GameState& state, ClassType cls) {
    int idx = (int)cls - 1;
    if (idx < 0 || idx >= GameState::ROSTER_SIZE) return false;
    Hero& hero = state.heroes[idx];
    if (!hero.everPlayed) {
        hero = Hero{};
        hero.playerClass = cls;
        hero.everPlayed  = true;
        InitUpgrades(hero);
        InitTalentsForClass(hero);
    }
    state.activeHero = idx;
    return true;
}

void DoPrestige(GameState& state) {
    if (state.activeHero < 0) return;
    Hero& hero = state.Active();

    float gain = ComputeLegacyGain(hero);
    state.legacyBonusPct += gain;
    state.legacyPrestigeCount++;

    // 유지: 클래스, 장착 장비, 이 영웅의 프레스티지 횟수. 나머지는 리셋해서 다시 키움.
    ClassType          cls      = hero.playerClass;
    std::vector<Item>  equipped = hero.inventory.equipped;
    int                pc       = hero.prestigeCount + 1;

    hero = Hero{};
    hero.playerClass = cls;
    hero.everPlayed  = true;
    hero.prestigeCount = pc;
    InitUpgrades(hero);
    InitTalentsForClass(hero);
    hero.inventory.equipped = equipped;

    wchar_t buf[128];
    swprintf(buf, 128, TW(L"[sync] 프레스티지 완료 — 계승 보너스 +%.1f%% (총 %.1f%%)",
                           L"[sync] Prestige complete — legacy bonus +%.1f%% (total %.1f%%)"),
              gain, state.legacyBonusPct);
    hero.lastEvent = buf;
}

void ResetAll(GameState& state) {
    double runSec  = state.totalRunSec;
    double dashSec = state.dashboardOpenSec;
    int    lang    = state.language;
    state = GameState{};
    state.totalRunSec      = runSec;  // 위장 시간 기록은 초기화에도 유지
    state.dashboardOpenSec = dashSec;
    state.language         = lang;
}

std::wstring GameTick(Hero& hero, float legacyBonusPct) {
    if (hero.dungeon.enemyMaxHp == 0)
        InitDungeonStage(hero.dungeon);

    // legacyBonusPct는 "37.5"처럼 퍼센트 숫자 그대로 저장됨(표시 코드도 이 가정).
    // 배율 계산에선 분수(0.375)로 써야 하는데 100으로 안 나누고 그대로 더하던 버그가
    // 있었음 — 예를 들어 37.5%가 "1.375배"가 아니라 "38.5배"로 들어가서 프레스티지
    // 한 번에 스탯이 수십 배씩 폭증했음. 여기서 한 번만 나눠서 이후엔 분수로 사용.
    const float legacyFrac = legacyBonusPct / 100.0f;

    // 업그레이드 + 클래스 배율 계산
    float xpMult   = 1.0f + hero.upgrades[UP_XP].level   * hero.upgrades[UP_XP].multiplier;
    float goldMult = 1.0f + hero.upgrades[UP_GOLD].level  * hero.upgrades[UP_GOLD].multiplier;
    float dropMult = 1.0f + hero.upgrades[UP_DROP].level  * hero.upgrades[UP_DROP].multiplier;

    // 계승 보너스 — 어떤 영웅이든 프레스티지할 때마다 쌓이는 계정 전체 공용 보너스
    xpMult   += legacyFrac;
    goldMult += legacyFrac;
    dropMult += legacyFrac;

    if (hero.playerClass == CLASS_MAGE)    xpMult   *= 1.5f;
    if (hero.playerClass == CLASS_WARRIOR) goldMult *= 1.2f;
    if (hero.playerClass == CLASS_ROGUE)   goldMult *= 1.3f;
    if (hero.playerClass == CLASS_ROGUE)   dropMult *= 2.0f;

    // 장착 아이템 스탯 반영
    xpMult   += GetEquippedBonus(hero.inventory, StatType::Xp);
    goldMult += GetEquippedBonus(hero.inventory, StatType::Gold);
    dropMult += GetEquippedBonus(hero.inventory, StatType::Drop);

    long long xpGain   = (long long)((5 + hero.level * 2) * xpMult);
    long long goldGain = (long long)((3 + hero.level)     * goldMult);
    hero.xp   += xpGain;
    hero.gold += goldGain;

    std::wstring notify;

    TalentBonuses tal = ComputeTalentBonuses(hero);

    // 플레이어 체력 — 스테이지에 따라 소폭 성장 + 계승 보너스 + 2차 특성 보너스
    long long maxHp = (long long)(PlayerBaseMaxHp(hero.dungeon.stage) * (1.0f + tal.hpBonus + legacyFrac));
    hero.playerMaxHp = maxHp;
    if (hero.playerHp <= 0 || hero.playerHp > maxHp) hero.playerHp = maxHp;

    // 던전 전투 — 클래스별 공격 방식
    // 공격력 기반값은 몹보다 느리게 스테이지를 따라 성장하고, 그 위에
    // 투자(업그레이드/장비/계승/특성)가 곱연산으로 얹힘 — 투자가 여전히 핵심.
    float atkMult = 1.0f + GetEquippedBonus(hero.inventory, StatType::Attack)
                         + legacyFrac
                         + hero.upgrades[UP_ATK].level * hero.upgrades[UP_ATK].multiplier
                         + tal.atkBonus;
    // 공격속도 — 데미지를 곱해 늘리는 게 아니라 "한 틱에 몇 번 때리는지"를 결정함.
    // 정수 부분만큼 추가 공격이 확정되고, 소수 부분은 그 확률로 한 번 더 때림.
    float atkSpeedBonus = GetEquippedBonus(hero.inventory, StatType::AtkSpeed) + tal.atkSpeedBonus;
    long long baseAtk = (long long)(PlayerBaseAtk(hero.dungeon.stage) * atkMult);
    long long totalDmg = 0;
    switch (hero.playerClass) {
    case CLASS_WARRIOR:
        totalDmg = (long long)(baseAtk * 1.5f);
        if (hero.dungeon.bossStage) {
            totalDmg *= 2;  // 보스 특화
            totalDmg = (long long)(totalDmg * (1.0f + tal.bossDmgBonus)); // 처단자
        }
        break;
    case CLASS_MAGE: {
        float critChance = std::min(100.0f, 20.0f + tal.critChanceBonus); // 마력 폭주
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
    int numAttacks = 1 + (int)atkSpeedBonus; // 정수 부분 = 확정 추가 공격
    float atkSpeedFrac = atkSpeedBonus - (float)(int)atkSpeedBonus;
    if (atkSpeedFrac > 0.0f) {
        std::uniform_real_distribution<float> spdRoll(0.0f, 1.0f);
        if (spdRoll(g_rng) <= atkSpeedFrac) numAttacks++;
    }
    totalDmg *= numAttacks;

    // 기습(도적) — 포인트당 추가 공격 확률
    if (tal.extraAtkChance > 0.0f) {
        std::uniform_real_distribution<float> procRoll(0.0f, 100.0f);
        if (procRoll(g_rng) <= tal.extraAtkChance) totalDmg *= 2;
    }

    long long enemyDef = EnemyDefForStage(hero.dungeon.stage);
    totalDmg = std::max(0LL, totalDmg - enemyDef);
    hero.dungeon.enemyHp -= totalDmg;

    // 체력흡수 — 실제로 들어간 데미지 기준으로 회복 (장비 + 마법사 마나 흡혈)
    float lifestealPct = GetEquippedBonus(hero.inventory, StatType::Lifesteal) + tal.lifestealBonus;
    hero.lastHealAmount = 0;
    if (totalDmg > 0 && lifestealPct > 0.0f) {
        long long heal = (long long)(totalDmg * lifestealPct);
        long long before = hero.playerHp;
        hero.playerHp = std::min(maxHp, hero.playerHp + heal);
        hero.lastHealAmount = hero.playerHp - before; // 화면에 "+N 회복" 표시용
    }

    if (hero.dungeon.enemyHp <= 0) {
        long long reward   = hero.dungeon.stage * 10LL * (hero.dungeon.bossStage ? 3 : 1);
        long long xpReward = hero.dungeon.stage * 5LL;
        hero.gold += reward;
        hero.xp   += xpReward;

        wchar_t buf[128];
        if (hero.dungeon.bossStage)
            swprintf(buf, 128, TW(L"[sync] 보스 처치! 스테이지 %d 클리어 (+%lld G)", L"[sync] Boss defeated! Stage %d cleared (+%lld G)"), hero.dungeon.stage, reward);
        else
            swprintf(buf, 128, TW(L"[sync] 스테이지 %d 클리어 (+%lld G)", L"[sync] Stage %d cleared (+%lld G)"), hero.dungeon.stage, reward);
        hero.lastEvent = buf;
        if (hero.dungeon.bossStage) notify = buf; // 보스만 알림, 일반 클리어는 조용히

        hero.dungeon.stage++;
        InitDungeonStage(hero.dungeon);
        hero.playerHp = hero.playerMaxHp; // 스테이지 클리어 시 체력 리필
    }

    // 적 반격 — 방어력/체력흡수 투자가 부족하면 죽어서 전투가 리셋됨 (스테이지는 유지)
    long long playerDef = (long long)(PlayerBaseDef(hero.dungeon.stage) * (1.0f + GetEquippedBonus(hero.inventory, StatType::Defense) + tal.defenseBonus + legacyFrac));
    long long enemyAtk   = EnemyAtkForStage(hero.dungeon.stage);
    long long dmgToPlayer = MitigateDamage(enemyAtk, playerDef);
    dmgToPlayer = (long long)(dmgToPlayer * (1.0f - std::min(0.9f, tal.evasionBonus))); // 은신 회피
    hero.playerHp -= dmgToPlayer;
    if (hero.playerHp <= 0) {
        hero.playerHp = hero.playerMaxHp;
        hero.dungeon.enemyHp = hero.dungeon.enemyMaxHp;
        hero.deathCount++;
        hero.lastEvent = TW(L"[sync] 패배 — 전투 초기화. 방어력/체력흡수를 보강하세요.",
                             L"[sync] Defeated — fight reset. Boost your defense/lifesteal.");
        // 막혀서 매 틱 죽는 상황에선 알림이 도배되므로 토스트는 띄우지 않음
    }

    // 레벨업 (대시보드 현황에만 표시, 알림 없음) — 레벨업마다 1차 특성 포인트 1개,
    // 25레벨부터는 2차 특성 포인트도 1개 추가 지급. 각각 평생 캡(15)까지만.
    while (hero.xp >= hero.xpForNext()) {
        hero.xp -= hero.xpForNext();
        hero.level++;

        int spent1 = hero.talents[TAL_0].level + hero.talents[TAL_1].level + hero.talents[TAL_2].level;
        bool gotTier1 = false, gotTier2 = false;
        if (hero.talentPoints + spent1 < MAX_TALENT_POINTS) {
            hero.talentPoints++;
            gotTier1 = true;
        }
        if (hero.level >= TIER2_LEVEL_REQ) {
            int spent2 = hero.talents[TAL_3].level + hero.talents[TAL_4].level + hero.talents[TAL_5].level;
            if (hero.talentPoints2 + spent2 < MAX_TALENT_POINTS_T2) {
                hero.talentPoints2++;
                gotTier2 = true;
            }
        }

        wchar_t buf[64];
        if (gotTier1 && gotTier2)
            swprintf(buf, 64, TW(L"[sync] 레벨 %d 달성 (특성 포인트 +1, 2차 +1)", L"[sync] Level %d reached (+1 talent pt, +1 tier-2)"), hero.level);
        else if (gotTier1)
            swprintf(buf, 64, TW(L"[sync] 레벨 %d 달성 (특성 포인트 +1)", L"[sync] Level %d reached (+1 talent point)"), hero.level);
        else if (gotTier2)
            swprintf(buf, 64, TW(L"[sync] 레벨 %d 달성 (2차 특성 포인트 +1)", L"[sync] Level %d reached (+1 tier-2 point)"), hero.level);
        else
            swprintf(buf, 64, TW(L"[sync] 레벨 %d 달성", L"[sync] Level %d reached"), hero.level);
        hero.lastEvent = buf;
    }

    // 아이템 드랍
    int dropChance = (int)(6.0f * dropMult);
    std::uniform_int_distribution<int> roll(1, 100);
    if (roll(g_rng) <= dropChance) {
        Item dropped = MakeItem(RollDropGrade(hero.dungeon.stage));
        wchar_t buf[80];
        if ((int)hero.inventory.items.size() < Inventory::MAX_ITEMS) {
            hero.inventory.items.push_back(dropped);
            // %ls를 씀 — 와이드 printf(swprintf)에서 %s는 플랫폼마다 동작이 갈림
            // (MSVC는 wchar_t*로 받아주지만 안드로이드 Bionic libc는 표준대로
            // char*를 기대해서 깨짐). %ls는 모든 플랫폼에서 wchar_t*로 확실히 처리됨.
            swprintf(buf, 80, TW(L"[sync] %ls 아이템 획득 (%ls +%.0f%%)", L"[sync] Got %ls item (%ls +%.0f%%)"),
                     GradeNameW(dropped.grade),
                     StatNameW(dropped.stat),
                     dropped.bonus * 100.0f);
            hero.lastEvent = buf;
            if (notify.empty() && dropped.grade >= Grade::Rare) notify = buf;
        } else {
            // 보관함이 가득 차서 드랍이 그대로 버려짐 — 대시보드(최근 이벤트)에는 남기지만
            // 푸시 알림은 안 보냄. 드랍 롤이 될 때마다(가방 꽉 찬 동안 계속) 반복 알림이
            // 뜨는 게 과하다는 피드백 대응 — 확인은 대시보드 열었을 때 하면 충분함.
            swprintf(buf, 80, TW(L"[sync] 보관함 가득 참 — %ls 아이템을 놓쳤습니다!", L"[sync] Bag full — missed a %ls item!"), GradeNameW(dropped.grade));
            hero.lastEvent = buf;
        }
    }

    return notify;
}

std::wstring GameTick(GameState& state) {
    if (state.activeHero < 0) return L"";
    return GameTick(state.Active(), state.legacyBonusPct);
}

// ---- 저장 / 불러오기 ---------------------------------------------------------
// 경로 탐색은 platform.h 뒤로 위임 (OS별 구현은 platform_win.cpp 등)
//
// key=value 한 줄씩 저장하는 포맷. 자릿수 기반(위치로 필드를 구분하는) 포맷은
// 필드를 추가/삭제할 때마다 옛 세이브를 읽으면 값이 한 칸씩 밀려서 깨지는
// 사고가 반복됐다(유물 제거, 2차 특성 추가 때 모두 발생). key=value는 옛
// 세이브에 없는 키를 만나도 그냥 기본값을 쓰면 되므로, 필드를 추가해도 기존
// 진행 상황(레벨/장비/특성 등)이 사라지지 않는다.

static void WriteKV(std::string& out, const char* key, long long v)        { out += key; out += '='; out += std::to_string(v); out += '\n'; }
static void WriteKV(std::string& out, const char* key, double v)            { char buf[32]; snprintf(buf, sizeof(buf), "%.0f", v); out += key; out += '='; out += buf; out += '\n'; }
static void WriteKV(std::string& out, const char* key, const std::string& v) { out += key; out += '='; out += v; out += '\n'; }

static std::string JoinInts(const int* vals, int count) {
    std::string s;
    for (int i = 0; i < count; i++) {
        if (i) s += ',';
        s += std::to_string(vals[i]);
    }
    return s;
}

static void WriteHero(std::string& out, int idx, const Hero& hero) {
    char key[32];
    auto K = [&](const char* suffix) -> const char* {
        snprintf(key, sizeof(key), "h%d_%s", idx, suffix);
        return key;
    };
    WriteKV(out, K("everPlayed"), (long long)(hero.everPlayed ? 1 : 0));
    if (!hero.everPlayed) return; // 빈 슬롯은 나머지 필드 생략
    WriteKV(out, K("level"), (long long)hero.level);
    WriteKV(out, K("xp"), hero.xp);
    WriteKV(out, K("gold"), hero.gold);
    WriteKV(out, K("stage"), (long long)hero.dungeon.stage);
    WriteKV(out, K("prestige"), (long long)hero.prestigeCount);
    WriteKV(out, K("talentPoints"), (long long)hero.talentPoints);
    WriteKV(out, K("talentPoints2"), (long long)hero.talentPoints2);
    WriteKV(out, K("deathCount"), hero.deathCount);

    int upLevels[UP_COUNT];
    for (int i = 0; i < UP_COUNT; i++) upLevels[i] = hero.upgrades[i].level;
    WriteKV(out, K("upgrades"), JoinInts(upLevels, UP_COUNT));

    int talLevels[TAL_COUNT];
    for (int i = 0; i < TAL_COUNT; i++) talLevels[i] = hero.talents[i].level;
    WriteKV(out, K("talents"), JoinInts(talLevels, TAL_COUNT));

    WriteKV(out, K("inventory"), SerializeInventory(hero.inventory));
}

std::string SerializeGameState(const GameState& state) {
    std::string out;
    WriteKV(out, "activeHero", (long long)state.activeHero);
    WriteKV(out, "legacyBonusPct", (long long)(state.legacyBonusPct * 1000.0f)); // 소수점 3자리 보존
    WriteKV(out, "legacyPrestigeCount", (long long)state.legacyPrestigeCount);
    WriteKV(out, "totalRunSec", state.totalRunSec);
    WriteKV(out, "dashboardOpenSec", state.dashboardOpenSec);
    WriteKV(out, "language", (long long)(int)g_lang);
    WriteKV(out, "disguiseMode", (long long)(state.disguiseMode ? 1 : 0));
    WriteKV(out, "backgroundEnabled", (long long)(state.backgroundEnabled ? 1 : 0));

    for (int i = 0; i < GameState::ROSTER_SIZE; i++)
        WriteHero(out, i, state.heroes[i]);

    return out;
}

void SaveGame(const GameState& state) {
    BackupSaveFile(); // 덮어쓰기 전에 이전 세이브를 .bak로 보존
    FILE* f = OpenSaveFileForWrite();
    if (!f) return;
    std::string text = SerializeGameState(state);
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

// 텍스트 전체를 key=value 맵으로 읽어들임. 모르는 키나 줄 형식이 이상한 줄은 그냥 무시.
static std::map<std::string, std::string> ReadKVText(const std::string& text) {
    std::map<std::string, std::string> kv;
    std::istringstream stream(text);
    std::string s;
    while (std::getline(stream, s)) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        kv[s.substr(0, eq)] = s.substr(eq + 1);
    }
    return kv;
}

static long long KVLL(const std::map<std::string, std::string>& kv, const char* key, long long def) {
    auto it = kv.find(key);
    if (it == kv.end() || it->second.empty()) return def;
    return strtoll(it->second.c_str(), nullptr, 10);
}
static double KVDouble(const std::map<std::string, std::string>& kv, const char* key, double def) {
    auto it = kv.find(key);
    if (it == kv.end() || it->second.empty()) return def;
    return strtod(it->second.c_str(), nullptr);
}
static void KVIntList(const std::map<std::string, std::string>& kv, const char* key, int* out, int count) {
    auto it = kv.find(key);
    if (it == kv.end()) return;
    std::istringstream ss(it->second);
    std::string tok;
    for (int i = 0; i < count && std::getline(ss, tok, ','); i++)
        out[i] = atoi(tok.c_str());
}

static void ReadHero(const std::map<std::string, std::string>& kv, int idx, Hero& hero) {
    char key[32];
    auto K = [&](const char* suffix) -> const char* {
        snprintf(key, sizeof(key), "h%d_%s", idx, suffix);
        return key;
    };
    hero.everPlayed = KVLL(kv, K("everPlayed"), 0) != 0;
    if (!hero.everPlayed) return;

    hero.playerClass = (ClassType)(idx + 1); // 슬롯 인덱스가 곧 클래스를 결정
    hero.level        = (int)KVLL(kv, K("level"), 1);
    hero.xp           = KVLL(kv, K("xp"), 0);
    hero.gold         = KVLL(kv, K("gold"), 0);
    hero.dungeon.stage = (int)KVLL(kv, K("stage"), 1);
    hero.prestigeCount = (int)KVLL(kv, K("prestige"), 0);
    hero.talentPoints   = (int)KVLL(kv, K("talentPoints"), 0);
    hero.talentPoints2  = (int)KVLL(kv, K("talentPoints2"), 0);
    hero.deathCount      = KVLL(kv, K("deathCount"), 0);

    InitUpgrades(hero);
    InitTalentsForClass(hero);

    int upLevels[UP_COUNT] = {};
    for (int i = 0; i < UP_COUNT; i++) upLevels[i] = hero.upgrades[i].level;
    KVIntList(kv, K("upgrades"), upLevels, UP_COUNT);
    for (int i = 0; i < UP_COUNT; i++) hero.upgrades[i].level = upLevels[i];

    int talLevels[TAL_COUNT] = {};
    for (int i = 0; i < TAL_COUNT; i++) talLevels[i] = hero.talents[i].level;
    KVIntList(kv, K("talents"), talLevels, TAL_COUNT);
    for (int i = 0; i < TAL_COUNT; i++) hero.talents[i].level = talLevels[i];

    auto invIt = kv.find(K("inventory"));
    if (invIt != kv.end())
        DeserializeInventory(invIt->second, hero.inventory);
}

// 로스터(다중 영웅) 도입 이전의 단일 캐릭터 세이브 포맷을 새 포맷으로 변환.
// "activeHero" 키가 없는데 "class" 키가 있으면 옛 포맷으로 보고, 그 캐릭터를
// 자기 클래스에 맞는 로스터 슬롯으로 그대로 옮긴다 — 포맷이 바뀌어도 진행
// 상황은 절대 날리지 않는다 (라이브 서비스에서 업데이트한다고 캐릭터를
// 지우지는 않으니까).
static void MigrateOldSingleHeroSave(const std::map<std::string, std::string>& kv, GameState& state) {
    long long cls = KVLL(kv, "class", CLASS_NONE);
    if (cls < CLASS_WARRIOR || cls > CLASS_ROGUE) return; // 직업도 못 골랐던 세이브 — 옮길 게 없음

    int idx = (int)cls - 1;
    Hero& hero = state.heroes[idx];
    hero.everPlayed   = true;
    hero.playerClass  = (ClassType)cls;
    hero.level         = (int)KVLL(kv, "level", 1);
    hero.xp            = KVLL(kv, "xp", 0);
    hero.gold          = KVLL(kv, "gold", 0);
    hero.dungeon.stage = (int)KVLL(kv, "stage", 1);
    int oldPrestige     = (int)KVLL(kv, "prestige", 0);
    hero.prestigeCount  = oldPrestige;
    hero.talentPoints   = (int)KVLL(kv, "talentPoints", 0);
    hero.talentPoints2  = (int)KVLL(kv, "talentPoints2", 0);
    hero.deathCount      = KVLL(kv, "deathCount", 0);

    InitUpgrades(hero);
    InitTalentsForClass(hero);

    int upLevels[UP_COUNT] = {};
    for (int i = 0; i < UP_COUNT; i++) upLevels[i] = hero.upgrades[i].level;
    KVIntList(kv, "upgrades", upLevels, UP_COUNT);
    for (int i = 0; i < UP_COUNT; i++) hero.upgrades[i].level = upLevels[i];

    int talLevels[TAL_COUNT] = {};
    for (int i = 0; i < TAL_COUNT; i++) talLevels[i] = hero.talents[i].level;
    KVIntList(kv, "talents", talLevels, TAL_COUNT);
    for (int i = 0; i < TAL_COUNT; i++) hero.talents[i].level = talLevels[i];

    auto invIt = kv.find("inventory");
    if (invIt != kv.end())
        DeserializeInventory(invIt->second, hero.inventory);

    state.activeHero = idx;
    // 옛 프레스티지는 회당 +15% 보너스였으니, 그 가치를 계승 보너스로 그대로 환산해서 보존.
    state.legacyBonusPct      = oldPrestige * 15.0f;
    state.legacyPrestigeCount = oldPrestige;
}

bool DeserializeGameState(const std::string& text, GameState& state) {
    std::map<std::string, std::string> kv = ReadKVText(text);
    if (kv.empty()) return false; // 새 세이브 — 기본 GameState로 시작

    state.totalRunSec      = KVDouble(kv, "totalRunSec", 0.0);
    state.dashboardOpenSec = KVDouble(kv, "dashboardOpenSec", 0.0);
    state.language = (int)KVLL(kv, "language", 0);
    g_lang = (state.language == 1) ? Lang::EN : Lang::KO;
    state.disguiseMode = KVLL(kv, "disguiseMode", 0) != 0;
    state.backgroundEnabled = KVLL(kv, "backgroundEnabled", 1) != 0;

    bool isRosterFormat = kv.find("activeHero") != kv.end();
    if (!isRosterFormat) {
        MigrateOldSingleHeroSave(kv, state);
        return true;
    }

    state.activeHero          = (int)KVLL(kv, "activeHero", -1);
    state.legacyBonusPct       = (float)KVLL(kv, "legacyBonusPct", 0) / 1000.0f;
    state.legacyPrestigeCount = (int)KVLL(kv, "legacyPrestigeCount", 0);

    for (int i = 0; i < GameState::ROSTER_SIZE; i++)
        ReadHero(kv, i, state.heroes[i]);

    if (state.activeHero < 0 || state.activeHero >= GameState::ROSTER_SIZE ||
        !state.heroes[state.activeHero].everPlayed) {
        state.activeHero = -1; // 손상되었거나 가리키는 영웅이 없으면 로스터 화면으로
    }
    return true;
}

void LoadGame(GameState& state) {
    FILE* f = OpenSaveFileForRead();
    if (!f) return;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    fclose(f);
    DeserializeGameState(text, state);
}
