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
enum UpgradeId { UP_XP = 0, UP_GOLD, UP_DROP, UP_COUNT };

struct Upgrade {
    const char* name       = "";
    const char* desc       = "";
    int         level      = 0;
    int         maxLevel   = 10;
    long long   baseCost   = 0;
    float       multiplier = 0.0f;
};

struct Dungeon {
    int       stage      = 1;
    long long enemyHp    = 0;
    long long enemyMaxHp = 0;
    bool      bossStage  = false;
};

struct GameState {
    ClassType playerClass = CLASS_NONE;

    int       level = 1;
    long long xp    = 0;
    long long gold  = 0;
    long long items = 0;
    std::wstring lastEvent = L"대기 중...";

    Upgrade   upgrades[UP_COUNT];
    Dungeon   dungeon;
    Inventory inventory;

    long long xpForNext()  const { return 50LL + (long long)level * 25LL; }
    float     xpProgress() const { return (float)xp / (float)xpForNext(); }
};

long long    GetUpgradeCost(const Upgrade& u);
bool         PurchaseUpgrade(GameState& state, int id);
std::wstring GameTick(GameState& state);
void         SaveGame(const GameState& state);
void         LoadGame(GameState& state);
