#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"
#include "dashboard.h"
#include "game.h"
#include "platform.h"
#include "cloud_sync.h"

#include <android/log.h>
#include <android/input.h>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <chrono>
#include <string>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "SyncAgent", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SyncAgent", __VA_ARGS__)

static EGLDisplay           g_EglDisplay  = EGL_NO_DISPLAY;
static EGLSurface           g_EglSurface  = EGL_NO_SURFACE;
static EGLContext           g_EglContext  = EGL_NO_CONTEXT;
static struct android_app*  g_App         = nullptr;
static bool                 g_Initialized = false;
static std::string          g_IniFilename;
static float                g_densityScale = 1.0f; // 실제 픽셀 / 논리 dp 배율

static GameState g_state;
static AAssetManager* g_assetManager = nullptr;
static std::string g_widgetPath; // 홈화면 위젯이 읽어갈 표시용 데이터 파일 경로

using Clock = std::chrono::steady_clock;
static Clock::time_point g_lastTick;
static constexpr float   TICK_SEC = 5.0f;

// assets/ 폴더에서 파일 로드 (caller가 IM_FREE로 해제)
static int LoadAsset(const char* name, void** outData) {
    if (!g_assetManager) return 0;
    AAsset* a = AAssetManager_open(g_assetManager, name, AASSET_MODE_BUFFER);
    if (!a) return 0;
    int size = (int)AAsset_getLength(a);
    *outData = IM_ALLOC(size);
    AAsset_read(a, *outData, size);
    AAsset_close(a);
    return size;
}

// JNI: 소프트 키보드 표시 요청
static void ShowSoftKeyboard() {
    JavaVM* vm = g_App->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass cls = env->GetObjectClass(g_App->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "showSoftInput", "()V");
    if (mid) env->CallVoidMethod(g_App->activity->clazz, mid);
    vm->DetachCurrentThread();
}

// JNI: 키보드 유니코드 문자 폴링
static void PollUnicodeChars() {
    JavaVM* vm = g_App->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass cls = env->GetObjectClass(g_App->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "pollUnicodeChar", "()I");
    if (mid) {
        ImGuiIO& io = ImGui::GetIO();
        jint ch;
        while ((ch = env->CallIntMethod(g_App->activity->clazz, mid)) != 0)
            io.AddInputCharacter((unsigned)ch);
    }
    vm->DetachCurrentThread();
}

// wchar_t(안드로이드는 UTF-32) → UTF-8. dashboard.cpp의 Android용 ToUtf8과 동일 로직.
static std::string WideToUtf8(const std::wstring& src) {
    std::string out;
    for (wchar_t wc : src) {
        unsigned cp = (unsigned)wc;
        if      (cp < 0x80)    out += (char)cp;
        else if (cp < 0x800)   { out += (char)(0xC0|(cp>>6));  out += (char)(0x80|(cp&0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0|(cp>>12)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
        else                   { out += (char)(0xF0|(cp>>18)); out += (char)(0x80|((cp>>12)&0x3F)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
    }
    return out;
}

// JNI: 게임 이벤트를 안드로이드 알림으로 표시 (PC의 트레이 토스트에 대응).
// MainActivity.postEventNotification(String, boolean)을 호출한다.
static void PostEventNotification(const std::wstring& text, bool privacyMode) {
    if (!g_App || text.empty()) return;
    std::string utf8 = WideToUtf8(text);
    LOGI("PostEventNotification: wlen=%d utf8len=%d utf8='%s'",
         (int)text.size(), (int)utf8.size(), utf8.c_str());

    JavaVM* vm = g_App->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass cls = env->GetObjectClass(g_App->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "postEventNotification", "(Ljava/lang/String;Z)V");
    if (mid) {
        jstring jtext = env->NewStringUTF(utf8.c_str());
        env->CallVoidMethod(g_App->activity->clazz, mid, jtext, (jboolean)privacyMode);
        env->DeleteLocalRef(jtext);
        // Kotlin 쪽에서 예외가 안 잡히고 올라오면 JNIEnv에 예외가 걸린 채로 남아서
        // 스레드가 살아있는 동안 이후 JNI 호출이 전부 조용히 실패한다 — 방어적으로 비움.
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    vm->DetachCurrentThread();
}

// JNI: 리워드 광고 시청 요청 — MainActivity.showRewardedAd(int)을 호출해서 실제
// 광고를 띄운다. 여기선 그냥 "보여줘"만 요청하고, 시청 완료 보상은 비동기로
// 오기 때문에(광고 SDK 콜백은 안드로이드 UI 스레드에서 옴) PollAdRewards()에서
// 매 프레임 폴링해서 받는다.
void PlatformRequestRewardedAd(int rewardType) {
    if (!g_App) return;
    JavaVM* vm = g_App->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass cls = env->GetObjectClass(g_App->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "showRewardedAd", "(I)V");
    if (mid) env->CallVoidMethod(g_App->activity->clazz, mid, (jint)rewardType);
    if (env->ExceptionCheck()) env->ExceptionClear();
    vm->DetachCurrentThread();
}

// JNI: 완료된 광고 보상 폴링. Kotlin의 pollAdReward()는 pollUnicodeChar()와 같은
// 패턴 — 대기 중인 보상이 없으면 0, 있으면 (AdRewardType + 1)을 반환하고 비움
// (0을 "큐 비어있음" 신호로 쓰기 위해 +1 오프셋).
static void PollAdRewards() {
    if (!g_App || g_state.activeHero < 0) return;
    JavaVM* vm = g_App->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass cls = env->GetObjectClass(g_App->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "pollAdReward", "()I");
    if (mid) {
        jint reward;
        while ((reward = env->CallIntMethod(g_App->activity->clazz, mid)) != 0) {
            Hero& hero = g_state.Active();
            if (reward - 1 == (int)AdRewardType::AutoCraft)      GrantAutoCraftBuff(hero, g_state.totalRunSec);
            else if (reward - 1 == (int)AdRewardType::BagExpand) ExpandBag(hero.inventory);
            SaveGame(g_state); // 보상은 즉시 저장(다음 세이브 틱까지 기다리다 유실되면 안 됨)
        }
    }
    vm->DetachCurrentThread();
}

// JNI: Google 계정 연동 요청 — MainActivity.requestGoogleLink()를 호출해서 로그인
// 화면을 띄운다. 결과는 비동기로 오므로(로그인 액티비티가 끝나야 앎)
// PollGoogleLinkResult()에서 매 프레임 폴링해서 받는다.
void PlatformRequestGoogleLink() {
    if (!g_App) return;
    JavaVM* vm = g_App->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass cls = env->GetObjectClass(g_App->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "requestGoogleLink", "()V");
    if (mid) env->CallVoidMethod(g_App->activity->clazz, mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
    vm->DetachCurrentThread();
}

// JNI: 완료된 Google 로그인 결과 폴링. Kotlin의 pollGoogleLinkResult()는 빈
// 문자열이면 "아직 없음", "__FAILED__"면 로그인 실패, 그 외엔 계정 고유 ID
// (그대로 동기화 코드로 씀). 성공하면 그 코드로 클라우드에서 기존 세이브를
// 받아오거나(있으면 그대로 교체), 없으면 지금 로컬 세이브를 올리고 최초
// 연동 보상을 준다 — dashboard.cpp의 수동 "지금 다운로드" 버튼과 같은 로직.
static void PollGoogleLinkResult() {
    if (!g_App) return;
    JavaVM* vm = g_App->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass cls = env->GetObjectClass(g_App->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "pollGoogleLinkResult", "()Ljava/lang/String;");
    if (mid) {
        jstring jresult = (jstring)env->CallObjectMethod(g_App->activity->clazz, mid);
        if (jresult) {
            const char* chars = env->GetStringUTFChars(jresult, nullptr);
            std::string code(chars);
            env->ReleaseStringUTFChars(jresult, chars);
            env->DeleteLocalRef(jresult);

            if (!code.empty() && code != "__FAILED__") {
                CloudSetCode(code);
                std::string downloaded;
                CloudSyncResult dl = CloudDownload(code, downloaded);
                GameState fresh{};
                if (dl.ok && DeserializeGameState(downloaded, fresh)) {
                    g_state = fresh; // 기존에 이 계정으로 저장된 세이브가 있으면 그대로 교체
                    g_lang = (g_state.language == 1) ? Lang::EN : Lang::KO;
                } else {
                    CloudUpload(code, SerializeGameState(g_state)); // 새 계정이면 지금 세이브를 올림
                }
                g_state.googleLinked = true;
                GrantGoogleLinkReward(g_state);
                SaveGame(g_state);
            } else if (code == "__FAILED__") {
                LOGE("PollGoogleLinkResult: Google sign-in failed");
            }
        }
    }
    vm->DetachCurrentThread();
}

static void Init(struct android_app* app) {
    if (g_Initialized) return;
    g_App = app;
    g_assetManager = app->activity->assetManager;

    // EGL 초기화
    g_EglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(g_EglDisplay, 0, 0);
    const EGLint attrs[] = {
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE
    };
    EGLint numCfg = 0;
    eglChooseConfig(g_EglDisplay, attrs, nullptr, 0, &numCfg);
    EGLConfig cfg;
    eglChooseConfig(g_EglDisplay, attrs, &cfg, 1, &numCfg);
    EGLint fmt;
    eglGetConfigAttrib(g_EglDisplay, cfg, EGL_NATIVE_VISUAL_ID, &fmt);
    ANativeWindow_setBuffersGeometry(app->window, 0, 0, fmt);
    const EGLint ctxAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_EglContext = eglCreateContext(g_EglDisplay, cfg, EGL_NO_CONTEXT, ctxAttrs);
    g_EglSurface = eglCreateWindowSurface(g_EglDisplay, cfg, app->window, nullptr);
    eglMakeCurrent(g_EglDisplay, g_EglSurface, g_EglSurface, g_EglContext);

    // Dear ImGui 초기화
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    g_IniFilename = std::string(app->activity->internalDataPath) + "/imgui.ini";
    io.IniFilename = g_IniFilename.c_str();

    // PC 대시보드와 동일한 다크 테마
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding   = 2.0f;
    s.FrameRounding    = 2.0f;
    s.TabRounding      = 2.0f;
    s.WindowBorderSize = 0.0f;
    s.Colors[ImGuiCol_WindowBg]      = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    s.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    s.Colors[ImGuiCol_Tab]           = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    s.Colors[ImGuiCol_TabSelected]   = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);

    // 터치로 누르기엔 PC 기준 패딩/간격이 너무 빡빡해서 모바일에서만 살짝 키움.
    // 버튼 폭(320, 90 등)은 코드에 고정값으로 박혀있어 안 바뀌지만, 높이는
    // FramePadding.y가 커지면 같이 커져서(고정폭 버튼도 높이는 auto라) 탭하기
    // 편해짐 — 가로 폭을 안 건드리니 기존에 잡아둔 잘림 방지 여유폭도 안 깨짐.
    s.FramePadding  = ImVec2(10.0f, 8.0f);
    s.ItemSpacing.y = 10.0f;

    // 화면 배율 계산은 MainLoopStep에서 매 프레임 다시 함 (아래 kDesignWidth 참고).
    // 여기서는 초기값만 넣어둔다.
    g_densityScale = 1.0f;

    // 한국어 폰트: assets/font.ttf 를 넣어두면 자동 로드.
    // (예: NanumGothic.ttf, 없으면 기본 폰트 → 한글 깨짐)
    // PC(15pt)보다 살짝 키워서 모바일에서 더 읽기/누르기 편하게 함.
    // 선명도는 DisplayFramebufferScale이 알아서 처리한다.
    void* fontData = nullptr;
    int   fontSize = LoadAsset("font.ttf", &fontData);
    if (fontSize > 0) {
        ImFontConfig fc;
        fc.OversampleH = 1; fc.OversampleV = 1; fc.PixelSnapH = true;
        io.Fonts->AddFontFromMemoryTTF(fontData, fontSize, 17.0f,
                                       &fc, io.Fonts->GetGlyphRangesKorean());
    } else {
        LOGI("font.ttf not found in assets — Korean text will show as boxes");
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplAndroid_Init(app->window);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    g_lastTick    = Clock::now();
    g_Initialized = true;
    LOGI("ImGui + EGL initialized");
}

static void Shutdown() {
    if (!g_Initialized) return;
    SaveGame(g_state);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    if (g_EglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_EglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_EglContext != EGL_NO_CONTEXT) eglDestroyContext(g_EglDisplay, g_EglContext);
        if (g_EglSurface != EGL_NO_SURFACE) eglDestroySurface(g_EglDisplay, g_EglSurface);
        eglTerminate(g_EglDisplay);
    }
    g_EglDisplay  = EGL_NO_DISPLAY;
    g_EglContext  = EGL_NO_CONTEXT;
    g_EglSurface  = EGL_NO_SURFACE;
    g_Initialized = false;
    LOGI("Shutdown complete");
}

// JNI: 최근 앱 목록(TaskDescription)과 상시 알림의 이름/아이콘을 프라이버시 모드에
// 맞춰 갱신한다. MainActivity.updatePrivacyPresentation(boolean)을 호출한다.
static void UpdatePrivacyPresentation(bool privacyMode) {
    if (!g_App) return;
    JavaVM* vm = g_App->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass cls = env->GetObjectClass(g_App->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "updatePrivacyPresentation", "(Z)V");
    if (mid) {
        env->CallVoidMethod(g_App->activity->clazz, mid, (jboolean)privacyMode);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    vm->DetachCurrentThread();
}

// JNI: 카메라 펀치홀/노치의 상단 안전 영역 높이(px)를 가져온다.
// MainActivity.getSafeInsetTopPx()를 호출한다.
static int GetSafeInsetTopPx() {
    if (!g_App) return 0;
    JavaVM* vm = g_App->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return 0;
    jclass cls = env->GetObjectClass(g_App->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "getSafeInsetTopPx", "()I");
    int px = 0;
    if (mid) {
        px = (int)env->CallIntMethod(g_App->activity->clazz, mid);
        if (env->ExceptionCheck()) { env->ExceptionClear(); px = 0; }
    }
    vm->DetachCurrentThread();
    return px;
}

// 안전 영역은 회전 등으로만 바뀌니 매 프레임 JNI를 타지 않고 1초에 한 번 정도만 확인.
static void SyncTopInsetIfChanged() {
    static int lastPx = -1;
    static Clock::time_point lastCheck;
    auto now = Clock::now();
    if (lastPx >= 0 && std::chrono::duration<float>(now - lastCheck).count() < 1.0f)
        return;
    lastCheck = now;

    int px = GetSafeInsetTopPx();
    if (px != lastPx) {
        lastPx = px;
        DashboardSetTopInset(g_densityScale > 0.0f ? (float)px / g_densityScale : 0.0f);
    }
}

// 값이 바뀌었을 때만 JNI를 넘어가도록 변경 감지 (매 프레임 호출해도 저렴하게).
static bool g_privacyInit = false;
static bool g_lastPrivacyMode = false;
static void SyncPrivacyPresentationIfChanged() {
    if (!g_privacyInit || g_state.disguiseMode != g_lastPrivacyMode) {
        UpdatePrivacyPresentation(g_state.disguiseMode);
        g_lastPrivacyMode = g_state.disguiseMode;
        g_privacyInit = true;
    }
}

// JNI: 백그라운드 실행 여부에 맞춰 포그라운드 서비스를 시작/중지한다.
// MainActivity.setBackgroundEnabled(boolean)을 호출한다.
static void UpdateBackgroundEnabled(bool enabled) {
    if (!g_App) return;
    JavaVM* vm = g_App->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass cls = env->GetObjectClass(g_App->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "setBackgroundEnabled", "(Z)V");
    if (mid) {
        env->CallVoidMethod(g_App->activity->clazz, mid, (jboolean)enabled);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    vm->DetachCurrentThread();
}

static bool g_bgEnabledInit = false;
static bool g_lastBgEnabled = true;
static void SyncBackgroundEnabledIfChanged() {
    if (!g_bgEnabledInit || g_state.backgroundEnabled != g_lastBgEnabled) {
        UpdateBackgroundEnabled(g_state.backgroundEnabled);
        g_lastBgEnabled = g_state.backgroundEnabled;
        g_bgEnabledInit = true;
    }
}

// 홈화면 위젯용 표시 데이터 기록. 코틀린 쪽(SyncWidgetProvider)이 세이브 포맷이나
// 게임 공식(xpForNext 등)을 다시 구현할 필요 없도록, 표시에 필요한 값만 뽑아서 써준다.
static void WriteWidgetInfo() {
    if (g_widgetPath.empty() || g_state.activeHero < 0) return;
    const Hero& h = g_state.heroes[g_state.activeHero];
    FILE* f = fopen(g_widgetPath.c_str(), "w");
    if (!f) return;
    fprintf(f, "level=%d\nstage=%d\nxpPct=%.3f\ngold=%lld\n",
            h.level, h.dungeon.stage, h.xpProgress(), h.gold);
    fclose(f);
}

// 5초마다 게임 틱 — 창(EGL)이 없어도, 즉 앱이 백그라운드로 내려가 있어도 호출된다.
// (포그라운드 서비스가 프로세스를 살려두는 동안 android_main 루프 자체는 계속 돌기 때문에
//  방치형 게임의 핵심인 "안 보고 있어도 자란다"가 성립한다.)
// Google 계정 연동 중이면 이 주기마다 클라우드에 조용히 자동 업로드 — 매 틱(5초)마다
// 하면 네트워크 낭비라 훨씬 느슨한 주기로 함(수동 코드 동기화와 달리 유저가 신경 안 써도 됨).
static constexpr float CLOUD_AUTO_UPLOAD_SEC = 120.0f;
static std::chrono::steady_clock::time_point g_lastCloudUpload;

static void TickIfDue() {
    auto now = Clock::now();
    if (std::chrono::duration<float>(now - g_lastTick).count() < TICK_SEC)
        return;
    g_lastTick = now;
    g_state.totalRunSec += TICK_SEC;
    std::wstring evt = GameTick(g_state);
    SaveGame(g_state);
    WriteWidgetInfo();
    SyncPrivacyPresentationIfChanged();
    SyncBackgroundEnabledIfChanged();
    if (!evt.empty())
        PostEventNotification(evt, g_state.disguiseMode);

    if (g_state.googleLinked &&
        std::chrono::duration<float>(now - g_lastCloudUpload).count() >= CLOUD_AUTO_UPLOAD_SEC) {
        g_lastCloudUpload = now;
        std::string code = CloudGetSavedCode();
        if (!code.empty())
            CloudUpload(code, SerializeGameState(g_state));
    }
}

static void MainLoopStep() {
    if (g_EglDisplay == EGL_NO_DISPLAY) return;

    // 소프트 키보드 / 유니코드 처리
    ImGuiIO& io = ImGui::GetIO();
    static bool wantTextLast = false;
    if (io.WantTextInput && !wantTextLast) ShowSoftKeyboard();
    wantTextLast = io.WantTextInput;
    PollUnicodeChars();
    PollAdRewards();
    PollGoogleLinkResult();

    // 프레임 렌더
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();

    // ImGui_ImplAndroid_NewFrame()은 DisplaySize를 실제 화면 픽셀 크기로 설정한다.
    // dashboard.cpp의 레이아웃은 PC 창 폭(460)을 기준으로 한 고정 좌표를 쓰므로,
    // 기기 밀도(dpi)가 아니라 "실제 폭 / 460" 비율로 직접 스케일을 정해서
    // 화면 폭이 얼마든 논리 좌표 폭이 항상 460이 되게 강제한다.
    // (dpi 버킷 기반으로 계산하면 기기별 논리 폭이 460보다 좁아질 수 있어
    //  SameLine(100)+ProgressBar(320) 같은 조합이 화면 밖으로 넘쳐 잘려 보였음)
    // 좌우 여백만큼 논리 캔버스를 더 넓게(460+여백*2) 잡고, 실제 460폭 콘텐츠는
    // dashboard.cpp에서 가운데로 밀어 넣는다 — 콘텐츠 폭 자체는 안 바뀌므로 잘림 위험 없음.
    constexpr float kDesignWidth  = 460.0f;
    constexpr float kSideMarginDp = 14.0f;
    constexpr float kTotalWidth   = kDesignWidth + kSideMarginDp * 2.0f;
    float rawW = io.DisplaySize.x;
    float rawH = io.DisplaySize.y;
    g_densityScale = (rawW > 0.0f) ? (rawW / kTotalWidth) : 1.0f;
    io.DisplaySize = ImVec2(kTotalWidth, rawH / g_densityScale);
    io.DisplayFramebufferScale = ImVec2(g_densityScale, g_densityScale);
    DashboardSetSideMargin(kSideMarginDp);
    SyncTopInsetIfChanged(); // g_densityScale이 막 갱신됐으니 이 시점에 dp로 환산

    ImGui::NewFrame();

    DashboardDrawUI(g_state);
    SyncPrivacyPresentationIfChanged(); // 방금 대시보드에서 토글했으면 즉시 반영
    SyncBackgroundEnabledIfChanged();

    ImGui::Render();
    glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    eglSwapBuffers(g_EglDisplay, g_EglSurface);
}

static void handleAppCmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:  Init(app);         break;
    case APP_CMD_TERM_WINDOW:  Shutdown();         break;
    case APP_CMD_SAVE_STATE:   SaveGame(g_state);  break;
    default: break;
    }
}

// 터치를 그냥 마우스 클릭으로만 매핑하면, 버튼이 빽빽한 화면(장비탭 등)에서는
// 스크롤하려고 손가락을 대는 지점마다 버튼 위여서 드래그해도 스크롤이 안 되고
// 버튼이 눌려버림 — 다른 앱들처럼 "누른 채로 일정 거리 이상 움직이면 그건 스크롤,
// 안 움직이고 뗐으면 그건 탭"으로 직접 구분해줘야 함. 그래서 DOWN 시점엔 아직
// 버튼을 누르지 않고 있다가, MOVE에서 임계값을 넘으면 스크롤 모드로 전환해서
// 휠 이벤트로 흘려보내고(버튼 위에 있어도 스크롤됨), 임계값을 안 넘고 UP이 오면
// 그제서야 그 자리에서 클릭(다운+업)을 발생시켜서 탭으로 처리한다.
static bool  g_touchActive     = false;
static bool  g_touchIsScroll   = false;
static float g_touchDownX      = 0.0f;
static float g_touchDownY      = 0.0f;
static float g_touchLastY      = 0.0f;
static constexpr float kTouchScrollThresholdDp = 8.0f;  // 이 이상 움직이면 스크롤로 판정
static constexpr float kTouchScrollPixelsPerWheelUnit = 40.0f; // 드래그 픽셀 -> 휠 단위 환산

// 터치 좌표는 실제 화면 픽셀 단위로 들어오는데, io.DisplaySize는 dp(논리 좌표)로
// 축소해뒀기 때문에 vendor 백엔드(ImGui_ImplAndroid_HandleInputEvent)에 그대로
// 넘기면 좌표가 안 맞아서 버튼이 거의 눌리지 않는다. 모션 이벤트만 직접 처리해서
// 같은 g_densityScale로 나눠준다 (키 이벤트는 좌표가 없으니 그대로 위임).
static int32_t handleInputEvent(struct android_app* app, AInputEvent* event) {
    (void)app;
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        ImGuiIO& io = ImGui::GetIO();
        int32_t action = AMotionEvent_getAction(event);
        int32_t pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(event, pointerIndex) / g_densityScale;
        float y = AMotionEvent_getY(event, pointerIndex) / g_densityScale;

        switch (actionMasked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            // 아직 버튼 다운은 보내지 않음 — 스크롤인지 탭인지 MOVE/UP에서 결정.
            g_touchActive   = true;
            g_touchIsScroll = false;
            g_touchDownX    = x;
            g_touchDownY    = y;
            g_touchLastY    = y;
            return 1;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            if (g_touchActive && !g_touchIsScroll) {
                // 스크롤로 전환되지 않고 그대로 뗐으면 탭 — 이 시점에 클릭 발생시킴.
                io.AddMouseButtonEvent(0, true);
                io.AddMouseButtonEvent(0, false);
            }
            g_touchActive = false;
            return 1;
        case AMOTION_EVENT_ACTION_MOVE:
            if (g_touchActive) {
                if (!g_touchIsScroll) {
                    float dx = x - g_touchDownX, dy = y - g_touchDownY;
                    if (dx*dx + dy*dy > kTouchScrollThresholdDp * kTouchScrollThresholdDp)
                        g_touchIsScroll = true;
                }
                if (g_touchIsScroll) {
                    float dy = y - g_touchLastY;
                    io.AddMouseWheelEvent(0.0f, -dy / kTouchScrollPixelsPerWheelUnit);
                    g_touchLastY = y;
                    return 1; // 스크롤 중엔 위치 갱신을 안 보내서 버튼 호버/클릭이 안 걸리게 함
                }
            }
            io.AddMousePosEvent(x, y);
            return 1;
        default:
            return 1;
        }
    }
    return ImGui_ImplAndroid_HandleInputEvent(event);
}

void android_main(struct android_app* app) {
    // 게임 초기화 — 창이 열리기 전에 세이브를 로드해야 하므로 여기서 먼저 처리
    PlatformInit(app->activity->internalDataPath);
    CloudSyncAndroidInit(app);
    g_widgetPath = std::string(app->activity->internalDataPath) + "/widget.txt";
    LoadGame(g_state);
    g_lastTick = Clock::now();

    app->onAppCmd     = handleAppCmd;
    app->onInputEvent = handleInputEvent;

    while (true) {
        int outEvents;
        struct android_poll_source* outData;
        // 창이 떠 있을 땐 논블로킹(0, 렌더 루프가 계속 돔),
        // 백그라운드일 땐 1초마다 깨어나서 틱 타이머를 체크한다
        // (계속 블로킹(-1)하면 앱이 최소화된 동안 게임이 완전히 멈춰버림 — 방치형의 핵심인
        //  "백그라운드 성장"이 안 되므로, 여기서 짧은 타임아웃으로 깨어나게 해야 한다).
        int timeoutMs = g_Initialized ? 0 : 1000;
        while (ALooper_pollOnce(timeoutMs, nullptr, &outEvents,
                                (void**)&outData) >= 0) {
            if (outData) outData->process(app, outData);
            if (app->destroyRequested) {
                if (!g_Initialized) Shutdown();
                return;
            }
        }
        TickIfDue();
        if (g_Initialized) MainLoopStep();
    }
}
