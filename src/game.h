#pragma once
#include <string>

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
    int       level = 1;
    long long xp    = 0;
    long long gold  = 0;
    long long items = 0;
    std::wstring lastEvent = L"대기 중...";

    Upgrade upgrades[UP_COUNT];
    Dungeon dungeon;

    long long xpForNext()  const { return 50LL + (long long)level * 25LL; }
    float     xpProgress() const { return (float)xp / (float)xpForNext(); }
};

long long    GetUpgradeCost(const Upgrade& u);
bool         PurchaseUpgrade(GameState& state, int id);
std::wstring GameTick(GameState& state);
void         SaveGame(const GameState& state);
void         LoadGame(GameState& state);
