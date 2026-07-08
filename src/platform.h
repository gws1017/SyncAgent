#pragma once
#include <cstdio>

// 플랫폼별 구현 (platform_win.cpp, platform_android.cpp 등). game.cpp는 이 함수만 알고
// Windows/Android의 경로 인코딩(와이드 vs UTF-8)이나 구체적인 OS API는 모른다.
FILE* OpenSaveFileForRead();
FILE* OpenSaveFileForWrite();
// 새 세이브를 쓰기 전에 현재 세이브를 .bak로 복사해둠 — 뭔가 잘못돼도 한 단계는
// 되돌릴 수 있게 (저장 로직 버그, 동시 실행 등으로 인한 데이터 손실 방지).
void BackupSaveFile();
