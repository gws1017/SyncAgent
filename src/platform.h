#pragma once
#include <string>

// 플랫폼별 구현 (platform_win.cpp 등). game.cpp는 이 함수만 알고
// Windows/Android 등 구체적인 OS API는 모른다.
std::wstring GetSaveFilePath();
