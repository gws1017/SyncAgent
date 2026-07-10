#pragma once
#include <windows.h>
#include <string>

void TrayInit(HWND hwnd, UINT callbackMsg, HINSTANCE hInst);
void TrayDestroy();
void TraySetTooltip(const std::wstring& tip);
// 프라이버시 모드 상태에 맞춰 트레이 아이콘을 판타지(기본) ↔ sync 아이콘으로 교체.
void TraySetIcon(bool privacyMode);
void TrayNotify(const std::wstring& title, const std::wstring& body);
void TrayShowContextMenu(HWND hwnd, UINT cmdStatus, UINT cmdQuit);
