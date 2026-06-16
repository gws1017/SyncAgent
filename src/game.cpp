#include "game.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <random>

static std::mt19937 g_rng{ std::random_device{}() };

std::wstring GameTick(GameState& state) {
    long long xpGain   = 5 + state.level * 2;
    long long goldGain = 3 + state.level;
    state.xp   += xpGain;
    state.gold += goldGain;

    std::wstring notify;

    while (state.xp >= state.xpForNext()) {
        state.xp -= state.xpForNext();
        state.level++;
        wchar_t buf[64];
        swprintf_s(buf, L"[sync] 레벨 %d 달성", state.level);
        state.lastEvent = buf;
        notify = buf;
    }

    std::uniform_int_distribution<int> roll(1, 100);
    if (roll(g_rng) <= 6) {
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

static std::wstring SavePath() {
    wchar_t appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::wstring dir = std::wstring(appdata) + L"\\idlegame";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\save.txt";
    }
    return L"idlegame_save.txt";
}

void SaveGame(const GameState& state) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, SavePath().c_str(), L"w") == 0 && f) {
        fprintf(f, "%d %lld %lld %lld\n",
                state.level, state.xp, state.gold, state.items);
        fclose(f);
    }
}

void LoadGame(GameState& state) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, SavePath().c_str(), L"r") == 0 && f) {
        if (fscanf_s(f, "%d %lld %lld %lld",
                     &state.level, &state.xp, &state.gold, &state.items) != 4)
            state = GameState{};
        fclose(f);
    }
}
