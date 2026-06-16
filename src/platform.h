#pragma once
#include <cstdio>

// 플랫폼별 구현 (platform_win.cpp, platform_android.cpp 등). game.cpp는 이 함수만 알고
// Windows/Android의 경로 인코딩(와이드 vs UTF-8)이나 구체적인 OS API는 모른다.
FILE* OpenSaveFileForRead();
FILE* OpenSaveFileForWrite();
