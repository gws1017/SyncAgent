#include "cloud_sync.h"
#include <jni.h>
#include <android_native_app_glue.h>

// 안드로이드는 네트워킹을 Kotlin(CloudSync.kt)에서 처리하고, 여기서는 JNI로
// MainActivity의 래퍼 메서드를 호출하기만 한다 (HTTPS 클라이언트를 NDK에 새로
// 붙이는 것보다 훨씬 가볍고, 인증서 검증 등을 시스템에 그대로 맡길 수 있음).

static android_app* g_androidApp = nullptr;

void CloudSyncAndroidInit(android_app* app) { g_androidApp = app; }

static JNIEnv* AttachEnv(JavaVM** outVm) {
    if (!g_androidApp) return nullptr;
    JavaVM* vm = g_androidApp->activity->vm;
    JNIEnv* env = nullptr;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    *outVm = vm;
    return env;
}

static std::string CallStringNoArg(JNIEnv* env, jobject activity, const char* method) {
    jclass cls = env->GetObjectClass(activity);
    jmethodID mid = env->GetMethodID(cls, method, "()Ljava/lang/String;");
    std::string result;
    if (mid) {
        jstring js = (jstring)env->CallObjectMethod(activity, mid);
        if (js) {
            const char* chars = env->GetStringUTFChars(js, nullptr);
            result = chars;
            env->ReleaseStringUTFChars(js, chars);
            env->DeleteLocalRef(js);
        }
    }
    return result;
}

// "OK\n메시지(또는 데이터)" / "ERR\n메시지" 형태 응답을 분리 — JNI 경계를
// 한 번만 넘기 위해 성공여부+본문을 문자열 하나로 합쳐서 받는다.
static void SplitStatus(const std::string& raw, bool& ok, std::string& rest) {
    size_t nl = raw.find('\n');
    if (nl == std::string::npos) { ok = false; rest = raw; return; }
    ok = raw.substr(0, nl) == "OK";
    rest = raw.substr(nl + 1);
}

std::string CloudGetSavedCode() {
    JavaVM* vm; JNIEnv* env = AttachEnv(&vm);
    if (!env) return "";
    std::string r = CallStringNoArg(env, g_androidApp->activity->clazz, "cloudGetSavedCode");
    vm->DetachCurrentThread();
    return r;
}

std::string CloudGenerateCode() {
    JavaVM* vm; JNIEnv* env = AttachEnv(&vm);
    if (!env) return "";
    std::string r = CallStringNoArg(env, g_androidApp->activity->clazz, "cloudGenerateCode");
    vm->DetachCurrentThread();
    return r;
}

void CloudSetCode(const std::string& code) {
    JavaVM* vm; JNIEnv* env = AttachEnv(&vm);
    if (!env) return;
    jclass cls = env->GetObjectClass(g_androidApp->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "cloudSetCode", "(Ljava/lang/String;)V");
    if (mid) {
        jstring jcode = env->NewStringUTF(code.c_str());
        env->CallVoidMethod(g_androidApp->activity->clazz, mid, jcode);
        env->DeleteLocalRef(jcode);
    }
    vm->DetachCurrentThread();
}

CloudSyncResult CloudUpload(const std::string& code, const std::string& saveText) {
    CloudSyncResult res;
    JavaVM* vm; JNIEnv* env = AttachEnv(&vm);
    if (!env) { res.message = "JNI 연결 실패"; return res; }

    jclass cls = env->GetObjectClass(g_androidApp->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "cloudUpload", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (mid) {
        jstring jcode = env->NewStringUTF(code.c_str());
        jstring jtext = env->NewStringUTF(saveText.c_str());
        jstring jresult = (jstring)env->CallObjectMethod(g_androidApp->activity->clazz, mid, jcode, jtext);
        if (jresult) {
            const char* chars = env->GetStringUTFChars(jresult, nullptr);
            std::string raw(chars);
            env->ReleaseStringUTFChars(jresult, chars);
            env->DeleteLocalRef(jresult);
            SplitStatus(raw, res.ok, res.message);
        }
        env->DeleteLocalRef(jcode);
        env->DeleteLocalRef(jtext);
    }
    vm->DetachCurrentThread();
    return res;
}

CloudSyncResult CloudDownload(const std::string& code, std::string& outText) {
    CloudSyncResult res;
    JavaVM* vm; JNIEnv* env = AttachEnv(&vm);
    if (!env) { res.message = "JNI 연결 실패"; return res; }

    jclass cls = env->GetObjectClass(g_androidApp->activity->clazz);
    jmethodID mid = env->GetMethodID(cls, "cloudDownload", "(Ljava/lang/String;)Ljava/lang/String;");
    if (mid) {
        jstring jcode = env->NewStringUTF(code.c_str());
        jstring jresult = (jstring)env->CallObjectMethod(g_androidApp->activity->clazz, mid, jcode);
        if (jresult) {
            const char* chars = env->GetStringUTFChars(jresult, nullptr);
            std::string raw(chars);
            env->ReleaseStringUTFChars(jresult, chars);
            env->DeleteLocalRef(jresult);
            bool ok; std::string rest;
            SplitStatus(raw, ok, rest);
            res.ok = ok;
            if (ok) { outText = rest; res.message = "다운로드 완료"; }
            else     res.message = rest;
        }
        env->DeleteLocalRef(jcode);
    }
    vm->DetachCurrentThread();
    return res;
}
