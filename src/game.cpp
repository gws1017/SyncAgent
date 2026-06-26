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
// 1차(슬롯 0~2): 레벨업마다 1포인트, 평생 15포인트 캡 (3개 다 풀업하는 30보다 적음).
// 2차(슬롯 3~5): 25레벨부터 별도 풀로 1포인트씩, 마찬가지로 평생 15포인트 캡.
// 클래스 컨셉에 맞춰 슬롯별 효과가 다름 (전사=탱커, 마법사=폭딜+자가회복, 도적=회피+다회타).
const Talent kTalentDefs[3][TAL_COUNT] = {
    { // 전사 — 단단한 근접 탱커
        { "철벽 방어 (패시브)",   "방어력 +10% / 포인트", 0, 10 },
        { "전장의 분노 (패시브)", "공격력 +8% / 포인트",  0, 10 },
        { "처단자 (패시브)",      "보스 데미지 +15% / 포인트", 0, 10 },
        { "불굴의 의지 (패시브)", "최대체력 +5% / 포인트", 0, 10 },
        { "광전사 (패시브)",      "공격력 +6% / 포인트",   0, 10 },
        { "수호자 (패시브)",      "방어력 +8% / 포인트",   0, 10 },
    },
    { // 마법사 — 불안정한 폭딜, 자가 회복으로 보완
        { "마나 증폭 (패시브)",   "폭발 데미지 +15% / 포인트",   0, 10 },
        { "마력 폭주 (패시브)",   "폭발 확률 +2%p / 포인트",     0, 10 },
        { "마나 흡혈 (패시브)",   "체력흡수 +3% / 포인트",       0, 10 },
        { "마나 코어 (패시브)",   "최대체력 +5% / 포인트",       0, 10 },
        { "원소 폭발 (패시브)",   "폭발 데미지 +10% / 포인트",   0, 10 },
        { "흡혼 (패시브)",        "체력흡수 +2% / 포인트",       0, 10 },
    },
    { // 도적 — 빠르고 잘 피하는 다회타
        { "연속 공격 (패시브)",   "공격속도 +10% / 포인트",      0, 10 },
        { "기습 (확률)",          "추가 공격 확률 +4%p / 포인트", 0, 10 },
        { "은신 회피 (패시브)",   "받는 피해 -5% / 포인트",       0, 10 },
        { "쾌속 (패시브)",        "공격속도 +6% / 포인트",        0, 10 },
        { "치명적 기습 (확률)",   "추가 공격 확률 +3%p / 포인트", 0, 10 },
        { "야성 (패시브)",        "받는 피해 -4% / 포인트",       0, 10 },
    },
};

void InitTalentsForClass(GameState& state) {
    int idx = (int)state.playerClass - 1;
    if (idx < 0 || idx >= 3) return; // CLASS_NONE이거나 손상된 값이면 건너뜀 (배열 범위 밖 접근 방지)
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
        b.hpBonus      = state.talents[TAL_3].level * 0.05f; // 불굴의 의지
        b.atkBonus    += state.talents[TAL_4].level * 0.06f; // 광전사
        b.defenseBonus+= state.talents[TAL_5].level * 0.08f; // 수호자
        break;
    case CLASS_MAGE:
        b.critDmgBonus    = state.talents[TAL_0].level * 0.15f; // 마나 증폭
        b.critChanceBonus = state.talents[TAL_1].level * 2.0f;  // 마력 폭주
        b.lifestealBonus  = state.talents[TAL_2].level * 0.03f; // 마나 흡혈
        b.hpBonus         = state.talents[TAL_3].level * 0.05f; // 마나 코어
        b.critDmgBonus   += state.talents[TAL_4].level * 0.10f; // 원소 폭발
        b.lifestealBonus += state.talents[TAL_5].level * 0.02f; // 흡혼
        break;
    case CLASS_ROGUE:
        b.atkSpeedBonus  = state.talents[TAL_0].level * 0.10f; // 연속 공격
        b.extraAtkChance = state.talents[TAL_1].level * 4.0f;  // 기습
        b.evasionBonus   = state.talents[TAL_2].level * 0.05f; // 은신 회피
        b.atkSpeedBonus += state.talents[TAL_3].level * 0.06f; // 쾌속
        b.extraAtkChance+= state.talents[TAL_4].level * 3.0f;  // 치명적 기습
        b.evasionBonus  += state.talents[TAL_5].level * 0.04f; // 야성
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

long long PlayerBaseMaxHp(int stage, int prestigeCount) {
    return 200 + (long long)stage * 8 + (long long)prestigeCount * PRESTIGE_HP_BONUS;
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

// 스테이지에 따라 드랍 등급 분포가 달라짐 — 프레스티지 첫 해금 지점부터 희귀가
// 섞이기 시작하고, 더 깊이 가면 영웅도 소량 나옴 (전설은 합성으로만 획득).
static Grade RollDropGrade(int stage) {
    std::uniform_int_distribution<int> roll(1, 100);
    int r = roll(g_rng);
    if (stage >= 40) {
        if (r <= 5)  return Grade::Epic;
        if (r <= 25) return Grade::Rare; // 5 + 20
        return Grade::Common;
    }
    if (stage >= PRESTIGE_STAGE_REQ) {
        if (r <= 20) return Grade::Rare;
        return Grade::Common;
    }
    return Grade::Common;
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
    bool isTier2 = id >= TIER1_TAL_COUNT;
    if (isTier2 && state.level < TIER2_LEVEL_REQ) return false; // 2차는 25레벨부터
    int& pool = isTier2 ? state.talentPoints2 : state.talentPoints;
    Talent& t = state.talents[id];
    if (t.level >= t.maxLevel) return false;
    if (pool <= 0) return false;
    pool--;
    t.level++;
    return true;
}

long long PrestigeRequirement(int prestigeCount) {
    return PRESTIGE_STAGE_REQ + (long long)prestigeCount * 10;
}

void DoPrestige(GameState& state) {
    // 유지: 장착 장비, 프레스티지 횟수, 위장 시간 기록. 클래스는 다시 선택하게 함
    // (직업을 바꿔서 다른 빌드를 시도해볼 수 있도록).
    std::vector<Item>  equipped = state.inventory.equipped;
    int                pc       = state.prestigeCount + 1;
    double             runSec   = state.totalRunSec;
    double             dashSec  = state.dashboardOpenSec;

    state = GameState{};
    InitUpgrades(state);

    state.inventory.equipped   = equipped;
    state.prestigeCount        = pc;
    state.totalRunSec          = runSec;
    state.dashboardOpenSec     = dashSec;
    state.lastEvent            = L"[sync] 프레스티지 완료 — 직업을 다시 선택하세요";
}

void ResetGame(GameState& state) {
    double runSec  = state.totalRunSec;
    double dashSec = state.dashboardOpenSec;
    state = GameState{};
    InitUpgrades(state);
    state.totalRunSec      = runSec;  // 위장 시간 기록은 세이브 초기화에도 유지
    state.dashboardOpenSec = dashSec;
    state.lastEvent        = L"[sync] 초기화 완료";
}

std::wstring GameTick(GameState& state) {
    if (state.dungeon.enemyMaxHp == 0)
        InitDungeonStage(state.dungeon);

    // 업그레이드 + 클래스 배율 계산
    float xpMult   = 1.0f + state.upgrades[UP_XP].level   * state.upgrades[UP_XP].multiplier;
    float goldMult = 1.0f + state.upgrades[UP_GOLD].level  * state.upgrades[UP_GOLD].multiplier;
    float dropMult = 1.0f + state.upgrades[UP_DROP].level  * state.upgrades[UP_DROP].multiplier;

    // 프레스티지 영구 보너스 (회당: XP/골드/드랍률 +15%, 공격력 +10%, 최대체력 +20 — game.h 상수 참고)
    float prestigeBonus = state.prestigeCount * PRESTIGE_ECON_BONUS;
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

    TalentBonuses tal = ComputeTalentBonuses(state);

    // 플레이어 체력 — 스테이지에 따라 소폭 성장 + 프레스티지마다 추가 증가 + 2차 특성 보너스
    long long maxHp = (long long)(PlayerBaseMaxHp(state.dungeon.stage, state.prestigeCount) * (1.0f + tal.hpBonus));
    state.playerMaxHp = maxHp;
    if (state.playerHp <= 0 || state.playerHp > maxHp) state.playerHp = maxHp;

    // 던전 전투 — 클래스별 공격 방식
    // 공격력 기반값은 몹보다 느리게 스테이지를 따라 성장하고, 그 위에
    // 투자(업그레이드/장비/프레스티지/특성)가 곱연산으로 얹힘 — 투자가 여전히 핵심.
    float atkMult = 1.0f + GetEquippedBonus(state.inventory, StatType::Attack)
                         + state.prestigeCount * PRESTIGE_ATK_BONUS
                         + state.upgrades[UP_ATK].level * state.upgrades[UP_ATK].multiplier
                         + tal.atkBonus;
    // 공격속도 — 데미지를 곱해 늘리는 게 아니라 "한 틱에 몇 번 때리는지"를 결정함.
    // 정수 부분만큼 추가 공격이 확정되고, 소수 부분은 그 확률로 한 번 더 때림.
    float atkSpeedBonus = GetEquippedBonus(state.inventory, StatType::AtkSpeed) + tal.atkSpeedBonus;
    long long baseAtk = (long long)(PlayerBaseAtk(state.dungeon.stage) * atkMult);
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

    long long enemyDef = EnemyDefForStage(state.dungeon.stage);
    totalDmg = std::max(0LL, totalDmg - enemyDef);
    state.dungeon.enemyHp -= totalDmg;

    // 체력흡수 — 실제로 들어간 데미지 기준으로 회복 (장비 + 마법사 마나 흡혈)
    float lifestealPct = GetEquippedBonus(state.inventory, StatType::Lifesteal) + tal.lifestealBonus;
    state.lastHealAmount = 0;
    if (totalDmg > 0 && lifestealPct > 0.0f) {
        long long heal = (long long)(totalDmg * lifestealPct);
        long long before = state.playerHp;
        state.playerHp = std::min(maxHp, state.playerHp + heal);
        state.lastHealAmount = state.playerHp - before; // 화면에 "+N 회복" 표시용
    }

    if (state.dungeon.enemyHp <= 0) {
        long long reward   = state.dungeon.stage * 10LL * (state.dungeon.bossStage ? 3 : 1);
        long long xpReward = state.dungeon.stage * 5LL;
        state.gold += reward;
        state.xp   += xpReward;

        wchar_t buf[128];
        if (state.dungeon.bossStage)
            swprintf(buf, 128, L"[sync] 보스 처치! 스테이지 %d 클리어 (+%lld G)", state.dungeon.stage, reward);
        else
            swprintf(buf, 128, L"[sync] 스테이지 %d 클리어 (+%lld G)", state.dungeon.stage, reward);
        state.lastEvent = buf;
        if (state.dungeon.bossStage) notify = buf; // 보스만 알림, 일반 클리어는 조용히

        state.dungeon.stage++;
        InitDungeonStage(state.dungeon);
        state.playerHp = state.playerMaxHp; // 스테이지 클리어 시 체력 리필
    }

    // 적 반격 — 방어력/체력흡수 투자가 부족하면 죽어서 전투가 리셋됨 (스테이지는 유지)
    long long playerDef = (long long)(PlayerBaseDef(state.dungeon.stage) * (1.0f + GetEquippedBonus(state.inventory, StatType::Defense) + tal.defenseBonus));
    long long enemyAtk   = EnemyAtkForStage(state.dungeon.stage);
    long long dmgToPlayer = MitigateDamage(enemyAtk, playerDef);
    dmgToPlayer = (long long)(dmgToPlayer * (1.0f - std::min(0.9f, tal.evasionBonus))); // 은신 회피
    state.playerHp -= dmgToPlayer;
    if (state.playerHp <= 0) {
        state.playerHp = state.playerMaxHp;
        state.dungeon.enemyHp = state.dungeon.enemyMaxHp;
        state.deathCount++;
        state.lastEvent = L"[sync] 패배 — 전투 초기화. 방어력/체력흡수를 보강하세요.";
        // 막혀서 매 틱 죽는 상황에선 알림이 도배되므로 토스트는 띄우지 않음
    }

    // 레벨업 (대시보드 현황에만 표시, 알림 없음) — 레벨업마다 1차 특성 포인트 1개,
    // 25레벨부터는 2차 특성 포인트도 1개 추가 지급. 각각 평생 캡(15)까지만.
    while (state.xp >= state.xpForNext()) {
        state.xp -= state.xpForNext();
        state.level++;

        int spent1 = state.talents[TAL_0].level + state.talents[TAL_1].level + state.talents[TAL_2].level;
        bool gotTier1 = false, gotTier2 = false;
        if (state.talentPoints + spent1 < MAX_TALENT_POINTS) {
            state.talentPoints++;
            gotTier1 = true;
        }
        if (state.level >= TIER2_LEVEL_REQ) {
            int spent2 = state.talents[TAL_3].level + state.talents[TAL_4].level + state.talents[TAL_5].level;
            if (state.talentPoints2 + spent2 < MAX_TALENT_POINTS_T2) {
                state.talentPoints2++;
                gotTier2 = true;
            }
        }

        wchar_t buf[64];
        if (gotTier1 && gotTier2)
            swprintf(buf, 64, L"[sync] 레벨 %d 달성 (특성 포인트 +1, 2차 +1)", state.level);
        else if (gotTier1)
            swprintf(buf, 64, L"[sync] 레벨 %d 달성 (특성 포인트 +1)", state.level);
        else if (gotTier2)
            swprintf(buf, 64, L"[sync] 레벨 %d 달성 (2차 특성 포인트 +1)", state.level);
        else
            swprintf(buf, 64, L"[sync] 레벨 %d 달성", state.level);
        state.lastEvent = buf;
    }

    // 아이템 드랍
    int dropChance = (int)(6.0f * dropMult);
    std::uniform_int_distribution<int> roll(1, 100);
    if (roll(g_rng) <= dropChance) {
        Item dropped = MakeItem(RollDropGrade(state.dungeon.stage));
        wchar_t buf[80];
        if ((int)state.inventory.items.size() < Inventory::MAX_ITEMS) {
            state.inventory.items.push_back(dropped);
            swprintf(buf, 80, L"[sync] %s 아이템 획득 (%s +%.0f%%)",
                     GradeNameW(dropped.grade),
                     StatNameW(dropped.stat),
                     dropped.bonus * 100.0f);
            state.lastEvent = buf;
            if (notify.empty() && dropped.grade >= Grade::Rare) notify = buf;
        } else {
            // 보관함이 가득 차서 드랍이 그대로 버려짐 — 놓치고 있다는 걸 알려줘야 함
            swprintf(buf, 80, L"[sync] 보관함 가득 참 — %s 아이템을 놓쳤습니다!", GradeNameW(dropped.grade));
            state.lastEvent = buf;
            if (notify.empty()) notify = buf;
        }
    }

    return notify;
}

// ---- 저장 / 불러오기 ---------------------------------------------------------
// 경로 탐색은 platform.h 뒤로 위임 (OS별 구현은 platform_win.cpp 등)

// 세이브 포맷이 바뀔 때마다 올림. 필드 개수/순서가 바뀌면 옛 세이브를 새 코드로
// 읽다가 값이 한 칸씩 밀려서 깨지는 사고가 반복됐기 때문에(유물 제거, 2차 특성
// 추가 등) 버전이 안 맞으면 그냥 새 게임으로 시작하도록 강제한다.
static constexpr int SAVE_FORMAT_VERSION = 2;

void SaveGame(const GameState& state) {
    FILE* f = OpenSaveFileForWrite();
    if (!f) return;
    fprintf(f, "%d\n", SAVE_FORMAT_VERSION);
    fprintf(f, "%d %lld %lld\n", state.level, state.xp, state.gold);
    for (int i = 0; i < UP_COUNT; i++) fprintf(f, "%d ", state.upgrades[i].level);
    fprintf(f, "\n%d\n%d\n%d\n", state.dungeon.stage, (int)state.playerClass, state.prestigeCount);
    std::string inv = SerializeInventory(state.inventory);
    fprintf(f, "%s\n", inv.c_str());
    fprintf(f, "%d ", state.talentPoints);
    for (int i = 0; i < TAL_COUNT; i++) fprintf(f, "%d ", state.talents[i].level);
    fprintf(f, "\n%.0f %.0f %lld %d\n", state.totalRunSec, state.dashboardOpenSec, state.deathCount, state.talentPoints2);
    fclose(f);
}

void LoadGame(GameState& state) {
    InitUpgrades(state);
    FILE* f = OpenSaveFileForRead();
    if (!f) return;
    int version = 0;
    if (fscanf(f, "%d", &version) != 1 || version != SAVE_FORMAT_VERSION) {
        // 버전이 없거나(구버전 세이브) 안 맞으면 필드가 밀려서 깨질 수 있으니
        // 파싱을 시도하지 않고 그냥 새 게임으로 시작한다.
        state = GameState{};
        InitUpgrades(state);
        fclose(f);
        return;
    }
    if (fscanf(f, "%d %lld %lld",
               &state.level, &state.xp, &state.gold) == 3) {
        for (int i = 0; i < UP_COUNT; i++)
            fscanf(f, "%d", &state.upgrades[i].level);
        int cls = 0;
        fscanf(f, "%d%d%d", &state.dungeon.stage, &cls, &state.prestigeCount);
        if (cls < CLASS_NONE || cls > CLASS_ROGUE) {
            // 구버전 포맷의 세이브 등으로 필드가 밀려서 깨진 값이 들어온 경우 —
            // 잘못된 클래스로 특성 테이블을 범위 밖으로 읽으면 크래시로 이어지므로
            // 깨진 세이브는 폐기하고 새 게임으로 안전하게 시작한다.
            state = GameState{};
            InitUpgrades(state);
            fclose(f);
            return;
        }
        state.playerClass = (ClassType)cls;
        InitTalentsForClass(state);
        char invBuf[4096] = {};
        if (fscanf(f, " %4095[^\n]", invBuf) == 1)
            DeserializeInventory(invBuf, state.inventory);
        if (fscanf(f, "%d", &state.talentPoints) == 1) {
            for (int i = 0; i < TAL_COUNT; i++)
                fscanf(f, "%d", &state.talents[i].level);
        }
        double runSec = 0.0, dashSec = 0.0;
        long long deaths = 0;
        int talPts2 = 0;
        int n = fscanf(f, "%lf %lf %lld %d", &runSec, &dashSec, &deaths, &talPts2);
        if (n >= 2) {
            state.totalRunSec      = runSec;
            state.dashboardOpenSec = dashSec;
        }
        if (n >= 3) state.deathCount = deaths;
        if (n >= 4) state.talentPoints2 = talPts2;
    } else {
        state = GameState{};
        InitUpgrades(state);
    }
    fclose(f);
}
