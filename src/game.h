#pragma once
#include <string>
#include "equipment.h"

// ---- 클래스 -----------------------------------------------------------------
enum ClassType { CLASS_NONE = 0, CLASS_WARRIOR, CLASS_MAGE, CLASS_ROGUE };

struct ClassDef {
    const char* name;
    const char* flavor;   // 한 줄 컨셉
    const char* stat0;    // 특성 설명 줄 1
    const char* stat1;    // 특성 설명 줄 2
    const char* stat2;    // 특성 설명 줄 3
};

// 인덱스 = ClassType - 1 (CLASS_NONE 제외)
extern const ClassDef kClasses[3];

// ---- 업그레이드 -------------------------------------------------------------
enum UpgradeId { UP_XP = 0, UP_GOLD, UP_DROP, UP_ATK, UP_COUNT };

struct Upgrade {
    const char* name       = "";
    const char* desc       = "";
    int         level      = 0;
    int         maxLevel   = 10;
    long long   baseCost   = 0;
    float       multiplier = 0.0f;
};

// ---- 특성 (레벨업 시 포인트 지급, 골드 아닌 포인트로 투자) --------------------
// 슬롯 0/1/2는 클래스마다 의미가 다름 (전사=탱커, 마법사=폭딜+자가회복, 도적=회피+다회타).
enum TalentSlot { TAL_0 = 0, TAL_1, TAL_2, TAL_COUNT };

struct Talent {
    const char* name     = "";
    const char* desc     = "";
    int         level    = 0;
    int         maxLevel = 10;
};

// 인덱스 = ClassType - 1. 클래스 선택 시 InitTalentsForClass()로 채워짐.
extern const Talent kTalentDefs[3][TAL_COUNT];

// 특성 3개 다 풀업하려면 30포인트가 필요한데, 평생 받을 수 있는 포인트를 이보다
// 적게 캡 걸어서 (절반 정도) 항상 일부만 선택해서 투자하도록 강제함.
static constexpr int MAX_TALENT_POINTS = 15;

// 특성 포인트를 수치 보너스로 환산한 결과 (클래스마다 슬롯 의미가 다름).
// GameTick과 대시보드 미리보기가 같은 함수를 써서 표시값과 실제값이 항상 일치하게 함.
struct TalentBonuses {
    float atkBonus        = 0.0f;
    float atkSpeedBonus   = 0.0f;
    float bossDmgBonus    = 0.0f;
    float critChanceBonus = 0.0f;
    float critDmgBonus    = 0.0f;
    float lifestealBonus  = 0.0f;
    float defenseBonus    = 0.0f;
    float evasionBonus    = 0.0f;
    float extraAtkChance  = 0.0f;
};

struct Dungeon {
    int       stage      = 1;
    long long enemyHp    = 0;
    long long enemyMaxHp = 0;
    bool      bossStage  = false;
};

static constexpr int   PRESTIGE_STAGE_REQ  = 20;    // 프레스티지 해금 조건
static constexpr float PRESTIGE_ECON_BONUS = 0.15f; // 회당 XP/골드/드랍률 보너스
static constexpr float PRESTIGE_ATK_BONUS  = 0.10f; // 회당 공격력 보너스
static constexpr long long PRESTIGE_HP_BONUS = 20;  // 회당 최대체력 보너스

long long XpForLevel(int level); // 지수 증가 — 레벨업이 점점 크게 느려짐

struct GameState {
    ClassType playerClass  = CLASS_NONE;
    int       prestigeCount = 0;

    int       level = 1;
    long long xp    = 0;
    long long gold  = 0;
    std::wstring lastEvent = L"대기 중...";

    Upgrade   upgrades[UP_COUNT];
    Talent    talents[TAL_COUNT];
    int       talentPoints = 0; // 미사용 특성 포인트 (레벨업마다 +1)

    long long playerHp       = 200;
    long long playerMaxHp    = 200;
    long long lastHealAmount = 0; // 이번 틱 체력흡수 회복량 (표시용, 저장 안 함)

    // 위장 시간 기록 — 백그라운드로 켜놓은 누적 시간 / 대시보드를 열어본 누적 시간
    double totalRunSec      = 0.0;
    double dashboardOpenSec = 0.0;

    Dungeon   dungeon;
    Inventory inventory;

    long long xpForNext()  const { return XpForLevel(level); }
    float     xpProgress() const { return (float)xp / (float)xpForNext(); }
};

long long    EnemyDefForStage(int stage);
long long    EnemyAtkForStage(int stage);
// 플레이어 기본 스탯 — 적보다 느린 속도로 스테이지에 따라 성장 (몹 성장 공식과 짝을 이룸).
// 투자(업그레이드/장비/특성) 없이도 어느 정도는 가지만, 결국 몹 성장 속도를 못 따라가서
// 투자가 필요해지는 "느려지는 장벽"을 만듦 (즉시 0데미지로 막히는 "딱딱한 장벽" 대신).
long long    PlayerBaseAtk(int stage);
long long    PlayerBaseDef(int stage);
long long    PlayerBaseMaxHp(int stage, int prestigeCount);
// 방어력을 적용한 후 실제로 들어오는 피해 — 비율 기반 감소(체력/(체력+K))라 투자가
// 클수록 효과가 크지만 0으로 완전히 막히지는 않음 (몹 성장이 항상 어느 정도는 위협이 됨).
long long    MitigateDamage(long long incomingDmg, long long defValue);
long long    GetUpgradeCost(const Upgrade& u);
bool         PurchaseUpgrade(GameState& state, int id);
void         InitTalentsForClass(GameState& state);
bool         InvestTalent(GameState& state, int id);
TalentBonuses ComputeTalentBonuses(const GameState& state);
long long    PrestigeRequirement(int prestigeCount);
void         DoPrestige(GameState& state);
void         ResetGame(GameState& state); // 세이브 초기화 (직업 재선택부터 다시 시작)
std::wstring GameTick(GameState& state);
void         SaveGame(const GameState& state);
void         LoadGame(GameState& state);
