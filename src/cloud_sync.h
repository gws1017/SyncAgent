#pragma once
#include <string>

// Firebase 기반 클라우드 동기화 — 계정 로그인 없이 "동기화 코드" 하나로
// PC/모바일 세이브를 연결한다. 코드가 없으면 CloudGenerateCode()로 새로 만들고,
// 다른 기기에서는 그 코드를 입력해서 같은 슬롯을 공유한다.
// 실제 HTTP/인증 처리는 플랫폼별 구현(cloud_sync_win.cpp, Android는 Kotlin+JNI)에 있다.

struct CloudSyncResult {
    bool ok = false;
    std::string message; // 실패 사유 또는 상태 메시지 (UI에 그대로 표시)
};

// 로컬에 저장된 동기화 코드를 가져옴 (없으면 빈 문자열)
std::string CloudGetSavedCode();
// 새 6자리 코드를 만들어 로컬에 저장 (다른 기기에서 이 코드를 입력해서 연결)
std::string CloudGenerateCode();
// 남이 만든 코드를 입력해서 이 기기에 연결 (로컬에 저장)
void        CloudSetCode(const std::string& code);

// 현재 세이브(saveText)를 클라우드에 업로드 — 마지막에 저장한 쪽이 우선(단순 last-write-wins)
CloudSyncResult CloudUpload(const std::string& code, const std::string& saveText);
// 클라우드에서 세이브를 받아옴 — 성공하면 outText에 세이브 텍스트가 채워짐
CloudSyncResult CloudDownload(const std::string& code, std::string& outText);

#ifndef _WIN32
struct android_app; // android_native_app_glue.h를 여기서 끌어오지 않기 위한 전방 선언
// android_main()에서 한 번 호출 — JNI 호출에 필요한 android_app*를 등록한다.
void CloudSyncAndroidInit(android_app* app);
#endif
