#pragma once
#include "game.h"

// 플랫폼 공통: NewFrame 이후 Render 이전에 호출. 순수 ImGui 위젯만 그린다.
void DashboardDrawUI(GameState& state);

// 화면 상단 안전 영역(카메라 펀치홀/노치) 높이를 논리 좌표(dp) 단위로 설정.
// 안드로이드에서만 0보다 큰 값이 들어오며, PC는 항상 0 (기본값)이라 영향 없음.
void DashboardSetTopInset(float insetDp);

// 좌우 여백(dp) — 화면이 꽉 차 보여서 답답한 느낌을 줄이기 위함. 창 폭 자체를
// 줄이는 게 아니라, 논리 캔버스를 그만큼 넓게 잡고 그 안에서 대시보드 창을
// 가운데로 밀어 넣는 방식이라 기존 레이아웃(잘림 방지용 고정 폭)엔 영향 없음.
void DashboardSetSideMargin(float marginDp);

#ifdef _WIN32
#include <windows.h>
bool  DashboardInit(HINSTANCE hInst);
void  DashboardDestroy();
void  DashboardToggle();
bool  DashboardIsVisible();
void  DashboardFrame(GameState& state);
bool  DashboardHandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
#endif
