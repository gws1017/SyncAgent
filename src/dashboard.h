#pragma once
#include "game.h"

// 플랫폼 공통: NewFrame 이후 Render 이전에 호출. 순수 ImGui 위젯만 그린다.
void DashboardDrawUI(GameState& state);

#ifdef _WIN32
#include <windows.h>
bool  DashboardInit(HINSTANCE hInst);
void  DashboardDestroy();
void  DashboardToggle();
bool  DashboardIsVisible();
void  DashboardFrame(GameState& state);
bool  DashboardHandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
#endif
