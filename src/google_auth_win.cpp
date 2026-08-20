// ============================================================================
// PC용 Google 계정 연동 — "설치형 앱" OAuth 루프백 플로우(RFC 8252).
// 안드로이드는 Play 서비스가 앱 안에서 로그인을 끝내주지만, 데스크톱엔 그런
// 게 없어서 표준 패턴을 직접 구현한다:
//   1) 127.0.0.1의 임시 포트에 로컬 HTTP 서버를 하나 띄운다
//   2) 시스템 브라우저로 구글 로그인 페이지를 연다(redirect_uri가 그 로컬 서버)
//   3) 로그인 성공하면 브라우저가 로컬 서버로 리다이렉트되면서 인증 코드를 줌
//   4) 그 코드를 토큰으로 교환해서(id_token) 계정 고유 ID(sub)를 뽑아낸다
// 전체 과정은 브라우저 응답을 기다려야 해서 오래 걸릴 수 있으므로 백그라운드
// 스레드에서 돌리고, 결과는 폴링용 큐에 담아 메인 스레드(WM_TIMER)가 가져간다.
// ============================================================================
#include "platform.h"
#include "cloud_sync.h"
#include "game.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <string>
#include <mutex>
#include <thread>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

// 프로젝트 고유 값(Google Cloud Console의 "데스크톱 앱" OAuth 클라이언트)은
// google_auth_secrets.h에 있음 — 그 파일은 .gitignore 처리돼 레포에 안 올라감
// (GitHub push protection이 커밋에 박힌 시크릿을 자동 차단하기도 함). 레포를
// 새로 포크해서 쓰려면 이 파일을 직접 만들어야 함(google_auth_win.cpp 상단 주석 참고).
#include "google_auth_secrets.h"
static const char* kClientId     = kGoogleAuthClientId;
static const char* kClientSecret = kGoogleAuthClientSecret;

static std::mutex      g_resultMutex;
static bool            g_hasResult = false;
static std::string     g_pendingResult; // 계정 고유 ID 또는 "__FAILED__"

static void PushResult(const std::string& r) {
    std::lock_guard<std::mutex> lock(g_resultMutex);
    g_pendingResult = r;
    g_hasResult = true;
}

bool PlatformPollGoogleLinkResult(std::string& outCode) {
    std::lock_guard<std::mutex> lock(g_resultMutex);
    if (!g_hasResult) return false;
    outCode = g_pendingResult;
    g_hasResult = false;
    return true;
}

// ---- 문자열 유틸 -------------------------------------------------------------
static std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 0xF]; }
    }
    return out;
}

static std::string UrlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int v = 0; sscanf(s.c_str() + i + 1, "%2x", &v);
            out += (char)v; i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

// "key":"value" 형태의 문자열 값을 뽑는다 (cloud_sync_win.cpp와 동일한 단순 검색 방식).
static bool ExtractJsonStringField(const std::string& json, const std::string& key, std::string& out) {
    std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    p = json.find('"', p);
    if (p == std::string::npos) return false;
    p++;
    std::string raw;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) { p += 2; continue; } // 이스케이프는 건너뜀(sub엔 안 나옴)
        raw += json[p]; p++;
    }
    out = raw;
    return true;
}

// base64url(패딩 없음) 디코드 — JWT 페이로드 세그먼트 해독용.
static std::string Base64UrlDecode(std::string s) {
    for (char& c : s) { if (c == '-') c = '+'; else if (c == '_') c = '/'; }
    while (s.size() % 4) s += '=';

    static const std::string tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : s) {
        if (c == '=') break;
        size_t idx = tbl.find((char)c);
        if (idx == std::string::npos) continue;
        val = (val << 6) + (int)idx;
        bits += 6;
        if (bits >= 0) {
            out += (char)((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return out;
}

// ---- WinHTTP POST 헬퍼 (cloud_sync_win.cpp와 별개 — 파일 단위로 작게 중복 유지) ----
static bool HttpsPost(const wchar_t* host, const std::wstring& path, const std::string& body, std::string& outResponse) {
    HINTERNET hSession = WinHttpOpen(L"SyncAgent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    std::wstring headers = L"Content-Type: application/x-www-form-urlencoded";
    BOOL sent = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1,
        (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);

    bool ok = false;
    if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0, size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (WinHttpReadData(hRequest, chunk.data(), avail, &read)) outResponse.append(chunk.data(), read);
            else break;
        }
        ok = (statusCode >= 200 && statusCode < 300);
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

// ---- 로컬 루프백 서버로 인증 코드 받기 ---------------------------------------
// 5분 내에 브라우저가 리다이렉트해오지 않으면 타임아웃 처리.
static bool WaitForAuthCode(SOCKET listenSock, std::string& outCode) {
    fd_set fds; FD_ZERO(&fds); FD_SET(listenSock, &fds);
    timeval tv{300, 0};
    if (select(0, &fds, nullptr, nullptr, &tv) <= 0) return false;

    SOCKET client = accept(listenSock, nullptr, nullptr);
    if (client == INVALID_SOCKET) return false;

    char buf[4096] = {};
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    std::string request = (n > 0) ? std::string(buf, n) : "";

    const char* html =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
        "<html><body style=\"font-family:sans-serif;text-align:center;margin-top:80px\">"
        "<h2>Text RPG 로그인 완료</h2><p>이 창을 닫고 게임으로 돌아가세요.</p></body></html>";
    send(client, html, (int)strlen(html), 0);
    shutdown(client, SD_BOTH);
    closesocket(client);

    // 요청 라인에서 "code=" 파라미터만 뽑는다: "GET /?code=XXX&scope=... HTTP/1.1"
    size_t codePos = request.find("code=");
    if (codePos == std::string::npos) return false;
    codePos += 5;
    size_t end = request.find_first_of("& \r\n", codePos);
    outCode = UrlDecode(request.substr(codePos, end - codePos));
    return !outCode.empty();
}

// ---- 백그라운드 스레드 본체 --------------------------------------------------
static void DoGoogleLinkFlow() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { PushResult("__FAILED__"); return; }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // OS가 빈 포트 아무거나 골라줌
    if (listenSock == INVALID_SOCKET || bind(listenSock, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(listenSock, 1) != 0) {
        if (listenSock != INVALID_SOCKET) closesocket(listenSock);
        WSACleanup();
        PushResult("__FAILED__");
        return;
    }
    int addrLen = sizeof(addr);
    getsockname(listenSock, (sockaddr*)&addr, &addrLen);
    int port = ntohs(addr.sin_port);

    std::string redirectUri = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::string authUrl = "https://accounts.google.com/o/oauth2/v2/auth"
        "?client_id=" + UrlEncode(kClientId) +
        "&redirect_uri=" + UrlEncode(redirectUri) +
        "&response_type=code&scope=" + UrlEncode("openid") + "&access_type=online";

    // ShellExecuteA는 시스템 기본 인코딩을 타서 URL이 아스키(퍼센트 인코딩)뿐이라 안전.
    ShellExecuteA(nullptr, "open", authUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    std::string code;
    bool gotCode = WaitForAuthCode(listenSock, code);
    closesocket(listenSock);
    WSACleanup();

    if (!gotCode) { PushResult("__FAILED__"); return; }

    std::string body =
        "code=" + UrlEncode(code) +
        "&client_id=" + UrlEncode(kClientId) +
        "&client_secret=" + UrlEncode(kClientSecret) +
        "&redirect_uri=" + UrlEncode(redirectUri) +
        "&grant_type=authorization_code";

    std::string response;
    if (!HttpsPost(L"oauth2.googleapis.com", L"/token", body, response)) {
        PushResult("__FAILED__");
        return;
    }

    std::string idToken;
    if (!ExtractJsonStringField(response, "id_token", idToken) || idToken.empty()) {
        PushResult("__FAILED__");
        return;
    }

    // JWT의 가운데 세그먼트(페이로드)를 디코드해서 "sub"(계정 고유 ID)만 뽑는다.
    // 서명 검증은 안 함 — HTTPS로 구글이 직접 준 값이라 신뢰하는 용도지, 보안이
    // 중요한 서버 인증이 아니라 그냥 "동기화 코드로 쓸 고유 문자열"이 필요할 뿐임.
    size_t dot1 = idToken.find('.');
    size_t dot2 = (dot1 == std::string::npos) ? std::string::npos : idToken.find('.', dot1 + 1);
    if (dot1 == std::string::npos || dot2 == std::string::npos) { PushResult("__FAILED__"); return; }
    std::string payload = Base64UrlDecode(idToken.substr(dot1 + 1, dot2 - dot1 - 1));

    std::string sub;
    if (!ExtractJsonStringField(payload, "sub", sub) || sub.empty()) { PushResult("__FAILED__"); return; }

    PushResult(sub);
}

void PlatformRequestGoogleLink() {
    std::thread(DoGoogleLinkFlow).detach();
}
