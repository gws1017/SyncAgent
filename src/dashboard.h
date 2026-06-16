#pragma once
#include <windows.h>
#include "game.h"

bool  DashboardInit(HINSTANCE hInst);
void  DashboardDestroy();
void  DashboardToggle();          // show / hide
bool  DashboardIsVisible();
void  DashboardFrame(GameState& state);   // call every frame when visible
bool  DashboardHandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
