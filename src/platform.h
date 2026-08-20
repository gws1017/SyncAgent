#pragma once
#include <cstdio>

FILE* OpenSaveFileForRead();
FILE* OpenSaveFileForWrite();
// 새 세이브를 쓰기 전에 현재 세이브를 .bak로 복사 — 데이터 손실 방지.
void BackupSaveFile();

#ifndef _WIN32
// Android: android_main에서 internalDataPath를 받아 저장 경로를 초기화한다.
void PlatformInit(const char* dataPath);

// 리워드 광고(모바일 전용) — PC는 획득 경로 자체가 없어서(design_backlog.md 참고)
// 이 함수를 호출하는 UI가 아예 없음, PC 빌드에선 선언만 있고 정의/호출 둘 다 없음.
enum class AdRewardType { AutoCraft = 0, BagExpand = 1 };
// 리워드 광고 시청을 요청 — 실제 보상 지급은 비동기 콜백(main_android.cpp의
// 매 프레임 폴링)에서 이뤄짐, 이 함수는 그냥 "보여줘" 요청만 던짐.
void PlatformRequestRewardedAd(int rewardType);

// Google 계정 연동(모바일 전용) — 로그인 화면을 띄우는 요청만 던짐. 성공하면
// main_android.cpp가 계정 고유 ID를 동기화 코드로 써서 자동 업로드/다운로드하고,
// 최초 연동이면 GrantGoogleLinkReward()로 보상을 준다.
void PlatformRequestGoogleLink();
#endif
