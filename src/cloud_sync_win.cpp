#include "cloud_sync.h"
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <cstdio>
#include <ctime>
#include <random>

#pragma comment(lib, "winhttp.lib")

// ============================================================================
// Firebase Realtime Database + Anonymous Auth를 REST로 직접 호출한다.
// 무거운 Firebase C++ SDK 대신 WinHTTP로 직접 HTTPS 요청을 날리는 방식 —
// Firebase SDK는 데스크톱(Windows) 지원이 사실상 없어서 이게 훨씬 가볍고 확실하다.
// ============================================================================

// TODO: 프로젝트 고유 값. 다른 사람이 이 레포를 포크해서 쓰려면 자기 Firebase
// 프로젝트 값으로 바꿔야 한다 (apiKey는 클라이언트에 노출돼도 되는 값 — 보안은
// Realtime Database 규칙으로 건다).
static const wchar_t* kApiKey      = L"AIzaSyAqvLIdwydpMnW2faZ3vF9A9slUFVLPaVY";
static const wchar_t* kDbHost      = L"syncagent-c5fbc-default-rtdb.firebaseio.com";
static const wchar_t* kIdentityHost = L"identitytoolkit.googleapis.com";
static const wchar_t* kTokenHost    = L"securetoken.googleapis.com";

struct AuthState {
    std::string idToken;
    std::string refreshToken;
    long long   expiresAt = 0; // epoch seconds
};

// ---- 로컬 파일 경로 ---------------------------------------------------------
static std::wstring CloudDataDir() {
    wchar_t appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::wstring dir = std::wstring(appdata) + L"\\idlegame";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return L".";
}
static std::wstring CodeFilePath() { return CloudDataDir() + L"\\synccode.txt"; }
static std::wstring AuthFilePath() { return CloudDataDir() + L"\\cloudauth.txt"; }

// ---- 아주 작은 JSON 유틸 (필드 몇 개만 뽑아내는 용도, 파서 라이브러리 없이) ------
static std::string JsonEscapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n";  break;
        case '\r': break;
        default:   out += c;
        }
    }
    return out;
}
static std::string JsonUnescapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'n') { out += '\n'; i++; continue; }
            if (n == '"' || n == '\\') { out += n; i++; continue; }
        }
        out += s[i];
    }
    return out;
}
// "key":"value" 형태의 문자열 값을 뽑는다 (단순 검색 — 중첩 JSON엔 안 씀).
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
        if (json[p] == '\\' && p + 1 < json.size()) { raw += json[p]; raw += json[p + 1]; p += 2; continue; }
        raw += json[p]; p++;
    }
    out = JsonUnescapeString(raw);
    return true;
}
// "key":123 형태의 숫자 값을 뽑는다.
static bool ExtractJsonNumberField(const std::string& json, const std::string& key, long long& out) {
    std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    p++;
    while (p < json.size() && (json[p] == ' ')) p++;
    out = strtoll(json.c_str() + p, nullptr, 10);
    return true;
}

// ---- WinHTTP 요청 헬퍼 ------------------------------------------------------
struct HttpResult { bool ok = false; int status = 0; std::string body; std::string error; };

static HttpResult HttpsRequest(const wchar_t* host, const std::wstring& pathWithQuery,
                                const wchar_t* method, const std::string& body,
                                const wchar_t* contentType) {
    HttpResult res;
    HINTERNET hSession = WinHttpOpen(L"SyncAgent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { res.error = "WinHttpOpen failed"; return res; }

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { res.error = "WinHttpConnect failed"; WinHttpCloseHandle(hSession); return res; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, pathWithQuery.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { res.error = "WinHttpOpenRequest failed"; WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return res; }

    std::wstring headers = std::wstring(L"Content-Type: ") + contentType;
    BOOL sent = WinHttpSendRequest(hRequest,
        headers.c_str(), (DWORD)-1,
        body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
        (DWORD)body.size(), (DWORD)body.size(), 0);

    if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0, size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        res.status = (int)statusCode;

        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (WinHttpReadData(hRequest, chunk.data(), avail, &read))
                res.body.append(chunk.data(), read);
            else break;
        }
        res.ok = (res.status >= 200 && res.status < 300);
    } else {
        res.error = "request failed, GetLastError=" + std::to_string(GetLastError());
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
}

// ---- 로컬 인증 캐시 ----------------------------------------------------------
static bool LoadAuthState(AuthState& a) {
    FILE* f = nullptr;
    _wfopen_s(&f, AuthFilePath().c_str(), L"r");
    if (!f) return false;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if (k == "idToken") a.idToken = v;
        else if (k == "refreshToken") a.refreshToken = v;
        else if (k == "expiresAt") a.expiresAt = strtoll(v.c_str(), nullptr, 10);
    }
    fclose(f);
    return !a.refreshToken.empty();
}
static void SaveAuthState(const AuthState& a) {
    FILE* f = nullptr;
    _wfopen_s(&f, AuthFilePath().c_str(), L"w");
    if (!f) return;
    fprintf(f, "idToken=%s\nrefreshToken=%s\nexpiresAt=%lld\n",
            a.idToken.c_str(), a.refreshToken.c_str(), a.expiresAt);
    fclose(f);
}

// ---- 인증: 최초 익명 로그인 / 만료 시 갱신 ------------------------------------
static bool SignInAnonymously(AuthState& a) {
    std::wstring path = std::wstring(L"/v1/accounts:signUp?key=") + kApiKey;
    HttpResult r = HttpsRequest(kIdentityHost, path, L"POST",
        "{\"returnSecureToken\":true}", L"application/json");
    if (!r.ok) return false;

    long long expiresIn = 3600;
    ExtractJsonStringField(r.body, "idToken", a.idToken);
    ExtractJsonStringField(r.body, "refreshToken", a.refreshToken);
    ExtractJsonNumberField(r.body, "expiresIn", expiresIn); // 문자열로 오는 경우도 있어 아래서 보정
    if (expiresIn <= 0) expiresIn = 3600;
    a.expiresAt = (long long)time(nullptr) + expiresIn - 60; // 여유 60초
    return !a.idToken.empty();
}

static bool RefreshIdToken(AuthState& a) {
    std::string body = "grant_type=refresh_token&refresh_token=" + a.refreshToken;
    std::wstring path = std::wstring(L"/v1/token?key=") + kApiKey;
    HttpResult r = HttpsRequest(kTokenHost, path, L"POST", body, L"application/x-www-form-urlencoded");
    if (!r.ok) return false;

    long long expiresIn = 3600;
    ExtractJsonStringField(r.body, "id_token", a.idToken);
    ExtractJsonStringField(r.body, "refresh_token", a.refreshToken);
    ExtractJsonNumberField(r.body, "expires_in", expiresIn);
    if (expiresIn <= 0) expiresIn = 3600;
    a.expiresAt = (long long)time(nullptr) + expiresIn - 60;
    return !a.idToken.empty();
}

// 유효한 idToken을 반환 (필요하면 갱신하거나 새로 로그인)
static bool EnsureAuth(AuthState& a) {
    if (LoadAuthState(a)) {
        if ((long long)time(nullptr) < a.expiresAt && !a.idToken.empty())
            return true;
        if (RefreshIdToken(a)) { SaveAuthState(a); return true; }
    }
    if (SignInAnonymously(a)) { SaveAuthState(a); return true; }
    return false;
}

// ============================================================================
// 공개 API
// ============================================================================
std::string CloudGetSavedCode() {
    FILE* f = nullptr;
    _wfopen_s(&f, CodeFilePath().c_str(), L"r");
    if (!f) return "";
    char buf[32] = {};
    fgets(buf, sizeof(buf), f);
    fclose(f);
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

void CloudSetCode(const std::string& code) {
    FILE* f = nullptr;
    _wfopen_s(&f, CodeFilePath().c_str(), L"w");
    if (!f) return;
    fputs(code.c_str(), f);
    fclose(f);
}

std::string CloudGenerateCode() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(100000, 999999);
    std::string code = std::to_string(dist(gen));
    CloudSetCode(code);
    return code;
}

CloudSyncResult CloudUpload(const std::string& code, const std::string& saveText) {
    CloudSyncResult res;
    if (code.empty()) { res.message = "동기화 코드가 없습니다"; return res; }

    AuthState auth;
    if (!EnsureAuth(auth)) { res.message = "로그인 실패 (네트워크 확인)"; return res; }

    std::string body = "{\"state\":\"" + JsonEscapeString(saveText) + "\","
                        "\"updatedAt\":" + std::to_string((long long)time(nullptr)) + ","
                        "\"updatedBy\":\"pc\"}";

    std::wstring wcode(code.begin(), code.end());
    std::wstring path = L"/syncs/" + wcode + L".json?auth=" + std::wstring(auth.idToken.begin(), auth.idToken.end());
    HttpResult r = HttpsRequest(kDbHost, path, L"PUT", body, L"application/json");
    if (!r.ok) { res.message = "업로드 실패 (" + std::to_string(r.status) + ")"; return res; }

    res.ok = true;
    res.message = "업로드 완료";
    return res;
}

CloudSyncResult CloudDownload(const std::string& code, std::string& outText) {
    CloudSyncResult res;
    if (code.empty()) { res.message = "동기화 코드가 없습니다"; return res; }

    AuthState auth;
    if (!EnsureAuth(auth)) { res.message = "로그인 실패 (네트워크 확인)"; return res; }

    std::wstring wcode(code.begin(), code.end());
    std::wstring path = L"/syncs/" + wcode + L".json?auth=" + std::wstring(auth.idToken.begin(), auth.idToken.end());
    HttpResult r = HttpsRequest(kDbHost, path, L"GET", "", L"application/json");
    if (!r.ok) { res.message = "다운로드 실패 (" + std::to_string(r.status) + ")"; return res; }
    if (r.body == "null" || r.body.empty()) { res.message = "클라우드에 저장된 데이터가 없습니다"; return res; }

    std::string state;
    if (!ExtractJsonStringField(r.body, "state", state)) {
        res.message = "응답 파싱 실패";
        return res;
    }
    outText = state;
    res.ok = true;
    res.message = "다운로드 완료";
    return res;
}
