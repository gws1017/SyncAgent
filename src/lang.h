#pragma once

// 간단한 한/영 전환 — 위장용. g_lang을 바꾸면 다음 프레임부터 모든 UI 텍스트가
// 즉시 바뀐다 (T/TW가 매번 현재 값을 보고 골라주는 방식이라 별도 갱신 로직 불필요).
enum class Lang { KO = 0, EN = 1 };
extern Lang g_lang;

const char*    T(const char* ko, const char* en);  // ImGui 등 UTF-8 문자열용
const wchar_t* TW(const wchar_t* ko, const wchar_t* en); // 트레이/알림 등 와이드 문자열용
