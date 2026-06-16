#pragma once
#include <windows.h>
#include <string>

void TrayInit(HWND hwnd, UINT callbackMsg, HINSTANCE hInst);
void TrayDestroy();
void TraySetTooltip(const std::wstring& tip);
void TrayNotify(const std::wstring& title, const std::wstring& body);
void TrayShowContextMenu(HWND hwnd, UINT cmdStatus, UINT cmdQuit);
