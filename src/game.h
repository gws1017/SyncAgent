#pragma once
#include <string>
#include "equipment.h"
#include "lang.h"

// 이름/설명을 한/영 쌍으로 들고, 호출 시점에 T()로 고르는 접근자를 제공하는 표들
// (전역 const 테이블은 프로그램 시작 시 한 번만 초기화되므로, 문자열을 거기서
// 미리 골라두면 나중에 언어를 바꿔도 안 바뀜 — 그래서 멤버 함수로 매번 고름).

// ---- 클래스 -----------------------------------------------------------------
enum ClassType { CLASS_NONE = 0, CLASS_WARRIOR, CLASS_MAGE, CLASS_ROGUE };

struct ClassDef {
    const char* nameKo;   const char* nameEn;
    const char* flavorKo; const char* flavorEn;   // 한 줄 컨셉
    const char* stat0Ko;  const char* stat0En;     // 특성 설명 줄 1
    const char* stat1Ko;  const char* stat1En;     // 특성 설명 줄 2
    const char* stat2Ko;  const char* stat2En;     // 특성 설명 줄 3

    const char* Name()   const { return T(nameKo, nameEn); }
    const char* Flavor() const { return T(flavorKo, flavorEn); }
    const char* Stat0()  const { return T(stat0Ko, stat0En); }
    const char* Stat1()  const { return T(stat1Ko, stat1En); }
    const char* Stat2()  const { return T(stat2Ko, stat2En); }
};

// 인덱스 = ClassType - 1 (CLASS_NONE 제외)
extern const ClassDef kClasses[3];

// ---- 업그레이드 -------------------------------------------------------------
enum UpgradeId { UP_XP = 0, UP_GOLD, UP_DROP, UP_ATK, UP_COUNT };

struct Upgrade {
    const char* nameKo = ""; const char* nameEn = "";
    const char* descKo = ""; const char* descEn = "";
    int         level      = 0;
    int         maxLevel   = 10;
    long long   baseCost   = 0;
    float       multiplier = 0.0f;

    const char* Name() const { return T(nameKo, nameEn); }
    const char* Desc() const { return T(descKo, descEn); }
};

// ---- 특성 (레벨업 시 포인트 지급, 골드 아닌 포인트로 투자) --------------------
// 1차(슬롯 0/1/2)는 클래스마다 의미가 다름 (전사=탱커, 마법사=폭딜+자가회복, 도적=회피+다회타).
// 2차(슬롯 3/4/5)는 25레벨부터 해금되며, 1차와 별도의 포인트 풀로 투자한다.
enum TalentSlot { TAL_0 = 0, TAL_1, TAL_2, TAL_3, TAL_4, TAL_5, TAL_COUNT };
static constexpr int TIER1_TAL_COUNT = 3; // 슬롯 0~2
static constexpr int TIER2_TAL_COUNT = 3; // 슬롯 3~5
static constexpr int TIER2_LEVEL_REQ = 25; // 2차 특성 해금 레벨

struct Talent {
    const char* nameKo = ""; const char* nameEn = "";
    const char* descKo = ""; const char* descEn = "";
    int         level    = 0;
    int         maxLevel = 10;

    const char* Name() const { return T(nameKo, nameEn); }
    const char* Desc() const { return T(descKo, descEn); }
};

// 인덱스 = ClassType - 1. 클래스 선택 시 InitTalentsForClass()로 채워짐.
extern const Talent kTalentDefs[3][TAL_COUNT];

// 1차/2차 각각 3개 다 풀업하려면 30포인트가 필요한데, 평생 받을 수 있는 포인트를
// 이보다 적게 캡 걸어서 (절반 정도) 항상 일부만 선택해서 투자하도록 강제함.
static constexpr int MAX_TALENT_POINTS     = 15; // 1차 풀 (talentPoints)
static constexpr int MAX_TALENT_POINTS_T2  = 15; // 2차 풀 (talentPoints2)

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
    float hpBonus         = 0.0f; // 2차 특성 전용 — 최대체력 % 증가
};

struct Dungeon {
    int       stage      = 1;
    long long enemyHp    = 0;
    long long enemyMaxHp = 0;
    bool      bossStage  = false;
};

static constexpr int   PRESTIGE_STAGE_REQ  = 40;    // 프레스티지(영웅 계승) 해금 스테이지

long long XpForLevel(int level); // 지수 증가 — 레벨업이 점점 크게 느려짐

// ---- 영웅 한 명의 진행 상황 (로스터 슬롯 하나) -------------------------------
// 슬롯 인덱스 = ClassType - 1. 영웅마다 완전히 독립적으로 성장/장비/특성을 가짐.
struct Hero {
    ClassType playerClass  = CLASS_NONE; // 이 슬롯이 비어있으면 CLASS_NONE
    bool      everPlayed   = false;      // 한 번이라도 만들어진 적 있는지 (로스터 표시용)

    int       level = 1;
    long long xp    = 0;
    long long gold  = 0;
    std::wstring lastEvent = TW(L"대기 중...", L"Waiting...");

    Upgrade   upgrades[UP_COUNT];
    Talent    talents[TAL_COUNT];
    int       talentPoints  = 0; // 1차 특성 미사용 포인트 (레벨업마다 +1)
    int       talentPoints2 = 0; // 2차 특성 미사용 포인트 (25레벨부터, 레벨업마다 +1)

    long long playerHp       = 200;
    long long playerMaxHp    = 200;
    long long lastHealAmount = 0; // 이번 틱 체력흡수 회복량 (표시용, 저장 안 함)
    long long deathCount     = 0; // 전투 중 체력 0이 되어 초기화된 횟수 (이 영웅 기준)

    int prestigeCount = 0; // 이 영웅이 계승(프레스티지)한 횟수

    Dungeon   dungeon;
    Inventory inventory;

    long long xpForNext()  const { return XpForLevel(level); }
    float     xpProgress() const { return (float)xp / (float)xpForNext(); }
};

// ---- 계정 전체 상태 — 영웅 로스터(최대 3, 클래스당 1) + 계승 보너스 풀 ----------
struct GameState {
    static constexpr int ROSTER_SIZE = 3;
    Hero heroes[ROSTER_SIZE];
    int  activeHero = -1; // -1 = 아직 영웅을 안 만듦 (로스터 화면 표시)

    // 계승 보너스 — 어떤 영웅이든 프레스티지할 때마다 그 영웅의 투자량에 비례해서
    // 증가하며, 모든 영웅(기존+신규)에게 항상 적용됨. "다른 영웅을 키워도 이전에
    // 쌓아둔 게 도움이 된다"는 느낌을 주는 계정 전체 공용 자원.
    float legacyBonusPct      = 0.0f; // 전체 보너스 (%, 공격력/방어력/XP/골드/드랍률/체력에 적용)
    int   legacyPrestigeCount = 0;    // 전체 영웅 합산 프레스티지 횟수 (표시용)

    // 위장 시간 기록 — 백그라운드로 켜놓은 누적 시간 / 대시보드를 열어본 누적 시간
    double totalRunSec      = 0.0;
    double dashboardOpenSec = 0.0;

    int language = 0; // 0=한국어, 1=영어. g_lang(lang.h)과 동기화해서 저장/불러오기함

    Hero&       Active()       { return heroes[activeHero]; }
    const Hero& Active() const { return heroes[activeHero]; }
};

long long    EnemyDefForStage(int stage);
long long    EnemyAtkForStage(int stage);
// 플레이어 기본 스탯 — 적보다 느린 속도로 스테이지에 따라 성장 (몹 성장 공식과 짝을 이룸).
// 투자(업그레이드/장비/특성) 없이도 어느 정도는 가지만, 결국 몹 성장 속도를 못 따라가서
// 투자가 필요해지는 "느려지는 장벽"을 만듦 (즉시 0데미지로 막히는 "딱딱한 장벽" 대신).
long long    PlayerBaseAtk(int stage);
long long    PlayerBaseDef(int stage);
long long    PlayerBaseMaxHp(int stage);
// 방어력을 적용한 후 실제로 들어오는 피해 — 비율 기반 감소(체력/(체력+K))라 투자가
// 클수록 효과가 크지만 0으로 완전히 막히지는 않음 (몹 성장이 항상 어느 정도는 위협이 됨).
long long    MitigateDamage(long long incomingDmg, long long defValue);
long long    GetUpgradeCost(const Upgrade& u);
bool         PurchaseUpgrade(Hero& hero, int id);
void         InitTalentsForClass(Hero& hero);
bool         InvestTalent(Hero& hero, int id);
TalentBonuses ComputeTalentBonuses(const Hero& hero);
std::wstring GameTick(Hero& hero, float legacyBonusPct);

// ---- 로스터 / 계승 ----------------------------------------------------------
bool         CreateOrSwitchHero(GameState& state, ClassType cls); // 없으면 새로 생성, 있으면 전환
// 이 영웅의 투자량(업그레이드+특성+장비)에 비례한 계승 보너스를 계정에 더하고,
// 이 영웅을 리셋해서 다시 키울 수 있게 함 (클래스/장비는 유지).
void         DoPrestige(GameState& state);
void         ResetAll(GameState& state); // 완전 초기화 — 로스터/계승 보너스까지 전부 삭제

std::wstring GameTick(GameState& state); // 활성 영웅 1틱 (없으면 무동작)
void         SaveGame(const GameState& state);
void         LoadGame(GameState& state);

// 세이브 데이터를 파일이 아닌 문자열로 다루는 버전 — 클라우드 동기화(cloud_sync)가
// 파일 I/O 없이 그대로 업로드/다운로드할 수 있도록 SaveGame/LoadGame의 파싱 로직을 재사용.
std::string  SerializeGameState(const GameState& state);
bool         DeserializeGameState(const std::string& text, GameState& state); // false면 빈 텍스트(신규)
