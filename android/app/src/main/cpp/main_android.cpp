#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"
#include "dashboard.h"
#include "game.h"
#include "platform.h"

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
// MainActivity.postEventNotification(String)을 호출한다.
static void PostEventNotification(const std::wstring& text) {
    if (!g_App || text.empty()) return;
    std::string utf8 = WideToUtf8(text);

    JavaVM* vm = g_App->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass cls = env->GetObjectClass(g_App->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "postEventNotification", "(Ljava/lang/String;)V");
    if (mid) {
        jstring jtext = env->NewStringUTF(utf8.c_str());
        env->CallVoidMethod(g_App->activity->clazz, mid, jtext);
        env->DeleteLocalRef(jtext);
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

    // 화면 배율 계산은 MainLoopStep에서 매 프레임 다시 함 (아래 kDesignWidth 참고).
    // 여기서는 초기값만 넣어둔다.
    g_densityScale = 1.0f;

    // 한국어 폰트: assets/font.ttf 를 넣어두면 자동 로드.
    // (예: NanumGothic.ttf, 없으면 기본 폰트 → 한글 깨짐)
    // PC와 동일하게 15pt로 로드 — 선명도는 DisplayFramebufferScale이 알아서 처리한다.
    void* fontData = nullptr;
    int   fontSize = LoadAsset("font.ttf", &fontData);
    if (fontSize > 0) {
        ImFontConfig fc;
        fc.OversampleH = 1; fc.OversampleV = 1; fc.PixelSnapH = true;
        io.Fonts->AddFontFromMemoryTTF(fontData, fontSize, 15.0f,
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

// 5초마다 게임 틱 — 창(EGL)이 없어도, 즉 앱이 백그라운드로 내려가 있어도 호출된다.
// (포그라운드 서비스가 프로세스를 살려두는 동안 android_main 루프 자체는 계속 돌기 때문에
//  방치형 게임의 핵심인 "안 보고 있어도 자란다"가 성립한다.)
static void TickIfDue() {
    auto now = Clock::now();
    if (std::chrono::duration<float>(now - g_lastTick).count() < TICK_SEC)
        return;
    g_lastTick = now;
    g_state.totalRunSec += TICK_SEC;
    std::wstring evt = GameTick(g_state);
    SaveGame(g_state);
    if (!evt.empty())
        PostEventNotification(evt);
}

static void MainLoopStep() {
    if (g_EglDisplay == EGL_NO_DISPLAY) return;

    // 소프트 키보드 / 유니코드 처리
    ImGuiIO& io = ImGui::GetIO();
    static bool wantTextLast = false;
    if (io.WantTextInput && !wantTextLast) ShowSoftKeyboard();
    wantTextLast = io.WantTextInput;
    PollUnicodeChars();

    // 프레임 렌더
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();

    // ImGui_ImplAndroid_NewFrame()은 DisplaySize를 실제 화면 픽셀 크기로 설정한다.
    // dashboard.cpp의 레이아웃은 PC 창 폭(460)을 기준으로 한 고정 좌표를 쓰므로,
    // 기기 밀도(dpi)가 아니라 "실제 폭 / 460" 비율로 직접 스케일을 정해서
    // 화면 폭이 얼마든 논리 좌표 폭이 항상 460이 되게 강제한다.
    // (dpi 버킷 기반으로 계산하면 기기별 논리 폭이 460보다 좁아질 수 있어
    //  SameLine(100)+ProgressBar(320) 같은 조합이 화면 밖으로 넘쳐 잘려 보였음)
    constexpr float kDesignWidth = 460.0f;
    float rawW = io.DisplaySize.x;
    float rawH = io.DisplaySize.y;
    g_densityScale = (rawW > 0.0f) ? (rawW / kDesignWidth) : 1.0f;
    io.DisplaySize = ImVec2(kDesignWidth, rawH / g_densityScale);
    io.DisplayFramebufferScale = ImVec2(g_densityScale, g_densityScale);

    ImGui::NewFrame();

    DashboardDrawUI(g_state);

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
            io.AddMouseButtonEvent(0, true);
            return 1;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, false);
            return 1;
        case AMOTION_EVENT_ACTION_MOVE:
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
