#pragma once
#include <string>
#include <functional>

struct GameState {
    int       level  = 1;
    long long xp     = 0;
    long long gold   = 0;
    long long items  = 0;
    std::wstring lastEvent = L"대기 중...";

    long long xpForNext() const { return 50LL + (long long)level * 25LL; }
    float     xpProgress() const { return (float)xp / (float)xpForNext(); }
};

// Called every tick. Returns a notification string if something notable
// happened (level-up, item drop), or empty string if nothing to notify.
std::wstring GameTick(GameState& state);

void SaveGame(const GameState& state);
void LoadGame(GameState& state);
