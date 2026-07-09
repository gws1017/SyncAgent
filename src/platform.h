#pragma once
#include <cstdio>

FILE* OpenSaveFileForRead();
FILE* OpenSaveFileForWrite();
// 새 세이브를 쓰기 전에 현재 세이브를 .bak로 복사 — 데이터 손실 방지.
void BackupSaveFile();

#ifndef _WIN32
// Android: android_main에서 internalDataPath를 받아 저장 경로를 초기화한다.
void PlatformInit(const char* dataPath);
#endif
