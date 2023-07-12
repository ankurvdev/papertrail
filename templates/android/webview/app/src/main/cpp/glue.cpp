#include "native_glue_export.h"

#define WEBVIEW_APP_EXPORT JNIEXPORT

#include "webview_app.h"

#include <android/log.h>
#include <cinttypes>
#include <cstring>
#include <jni.h>

// #define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "android-webview-glue::", __VA_ARGS__))

static jclass    jcls_MainActivity                                 = nullptr;
static bool      mainActivityActive                                = false;
static bool      locationServiceActive                             = false;
static jmethodID jmtd_MainActivity_onStartForegroundLocationUpdate = nullptr;
static jmethodID jmtd_MainActivity_onStopForegroundLocationUpdate  = nullptr;
static jmethodID jmtd_MainActivity_onStartOrientationUpdate        = nullptr;
static jmethodID jmtd_MainActivity_onStopOrientationUpdate         = nullptr;
static jmethodID jmtd_MainActivity_onStartLocationUpdate           = nullptr;
static jmethodID jmtd_MainActivity_onStopLocationUpdate            = nullptr;
static jmethodID jmtd_MainActivity_onRequestIAPInit                = nullptr;
static jmethodID jmtd_MainActivity_onRequestIAPFeature             = nullptr;

static JavaVM* s_vm                               = nullptr;
static JNIEnv* s_env                              = nullptr;
static bool    app_started                        = false;
static bool    location_request_status            = false;
static bool    location_foreground_request_status = false;
static bool    orientation_request_status         = false;

static void jni_initvm(JNIEnv* env)
{
    if (s_vm == nullptr) { env->GetJavaVM(&s_vm); }
    if (s_env == nullptr) { s_env = env; }
    if (jcls_MainActivity == nullptr)
    {
        jcls_MainActivity = env->FindClass("com/zzzsAndroid.AppPackageNamezzze/MainActivity");
        jmtd_MainActivity_onStopForegroundLocationUpdate
            = env->GetStaticMethodID(jcls_MainActivity, "cbStopForegroundLocationUpdate", "()V");
        jmtd_MainActivity_onStartForegroundLocationUpdate
            = env->GetStaticMethodID(jcls_MainActivity, "cbStartForegroundLocationUpdate", "()V");
        jmtd_MainActivity_onStopOrientationUpdate  = env->GetStaticMethodID(jcls_MainActivity, "cbStopOrientationUpdate", "()V");
        jmtd_MainActivity_onStartOrientationUpdate = env->GetStaticMethodID(jcls_MainActivity, "cbStartOrientationUpdate", "()V");
        jmtd_MainActivity_onStopLocationUpdate     = env->GetStaticMethodID(jcls_MainActivity, "cbStopLocationUpdate", "()V");
        jmtd_MainActivity_onStartLocationUpdate    = env->GetStaticMethodID(jcls_MainActivity, "cbStartLocationUpdate", "()V");
        jmtd_MainActivity_onRequestIAPInit         = env->GetStaticMethodID(jcls_MainActivity, "cbRequestIAPInit", "(Ljava/lang/String;)V");
        jmtd_MainActivity_onRequestIAPFeature = env->GetStaticMethodID(jcls_MainActivity, "cbRequestIAPFeature", "(Ljava/lang/String;)V");
        jcls_MainActivity                     = reinterpret_cast<jclass>(env->NewGlobalRef(jcls_MainActivity));
    }
}
static JNIEnv* jnienv()
{
    JNIEnv* env;
    if (s_vm == nullptr) return nullptr;
    int status = s_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED)
    {
        JavaVMAttachArgs thr_args = {.version = JNI_VERSION_1_6, .name = nullptr, .group = nullptr};
        s_vm->AttachCurrentThread(&env, &thr_args);
    }
    return env;
}

static void request_iap_init(const char* name)
{
    jstring jstr = jnienv()->NewStringUTF(name);
    jnienv()->CallStaticVoidMethod(jcls_MainActivity, jmtd_MainActivity_onRequestIAPInit, jstr);
}

static void request_iap_feature(const char* name)
{
    jstring jstr = jnienv()->NewStringUTF(name);
    jnienv()->CallStaticVoidMethod(jcls_MainActivity, jmtd_MainActivity_onRequestIAPFeature, jstr);
}

static void request_location_update_start()
{
    location_request_status = true;
    jnienv()->CallStaticVoidMethod(jcls_MainActivity, jmtd_MainActivity_onStartLocationUpdate);
}

static void request_location_update_stop()
{
    location_request_status = false;
    jnienv()->CallStaticVoidMethod(jcls_MainActivity, jmtd_MainActivity_onStopLocationUpdate);
}

static void request_orientation_update_start()
{
    orientation_request_status = true;
    jnienv()->CallStaticVoidMethod(jcls_MainActivity, jmtd_MainActivity_onStartOrientationUpdate);
}

static void request_orientation_update_stop()
{
    orientation_request_status = false;
    jnienv()->CallStaticVoidMethod(jcls_MainActivity, jmtd_MainActivity_onStopOrientationUpdate);
}

static void request_foreground_service_start()
{
    location_foreground_request_status = true;
    jnienv()->CallStaticVoidMethod(jcls_MainActivity, jmtd_MainActivity_onStartForegroundLocationUpdate);
}

static void request_foreground_service_stop()
{
    location_foreground_request_status = false;
    jnienv()->CallStaticVoidMethod(jcls_MainActivity, jmtd_MainActivity_onStopForegroundLocationUpdate);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Unused-parameter"

extern "C" JNIEXPORT void JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_MainActivity_glueAppInit(JNIEnv* env, jobject /*thiz*/)
{
    if (s_vm != nullptr) return;

    jni_initvm(env);
    app_callbacks callbacks{request_location_update_start,
                            request_location_update_stop,
                            request_orientation_update_start,
                            request_orientation_update_stop,
                            request_foreground_service_start,
                            request_foreground_service_stop,
                            request_iap_init,
                            request_iap_feature};
    app_init(&callbacks);
    android_app_init(env);
}

extern "C" JNIEXPORT void JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_MainActivity_glueAppActivityStart(JNIEnv* env, jobject /*thiz*/)
{
    jni_initvm(env);
    mainActivityActive = true;
    if (!app_started)
    {
        app_start();
        app_started = true;
    }
    else
    {
        if (location_request_status) request_location_update_start();
        if (location_foreground_request_status) request_foreground_service_start();
        if (orientation_request_status) request_orientation_update_start();
    }
}

extern "C" JNIEXPORT void JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_MainActivity_glueAppActivityStop(JNIEnv* env, jobject /*thiz*/)
{
    jni_initvm(env);
    mainActivityActive = false;
    if (!locationServiceActive)
    {
        app_stop();
        app_started = false;
    }
}

extern "C" JNIEXPORT void
    JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_LocationForegroundService_glueAppLocationServiceStopped(JNIEnv* env, jobject /*thiz*/)
{

    jni_initvm(env);
    if (!mainActivityActive)
    {
        app_stop();
        app_started = false;
    }
    locationServiceActive = false;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_MainActivity_glueGetWebserverUrl(JNIEnv* env, jobject /*thiz*/)
{
    jni_initvm(env);
    return env->NewStringUTF(get_webserver_url());
}

extern "C" JNIEXPORT void JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_LocationForegroundService_glueUpdateLocation(JNIEnv* env,
                                                                                                                       jobject thiz,
                                                                                                                       jobject location)
{
    jni_initvm(env);
    locationServiceActive = true;
    android_update_location(env, thiz, location);
}

extern "C" JNIEXPORT void
    JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_MainActivity_glueUpdateOrientation(JNIEnv* env, jobject thiz, jint orientation)
{
    jni_initvm(env);
    android_update_orientation(env, thiz, orientation);
}

extern "C" JNIEXPORT void JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_BillingManager_glueIAPReset(JNIEnv* env, jobject /*thiz*/)
{
    jni_initvm(env);
    if (app_started) iap_reset();
}

extern "C" JNIEXPORT void JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_BillingManager_glueIAPInitialized(JNIEnv* /*env*/,
                                                                                                            jobject /*thiz*/)
{}

extern "C" JNIEXPORT void JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_BillingManager_glueIAPRequestFailed(JNIEnv* env,
                                                                                                              jobject /*thiz*/,
                                                                                                              jstring name,
                                                                                                              jint    code,
                                                                                                              jstring message)
{
    jni_initvm(env);
    iap_request_failed(env->GetStringUTFChars(name, nullptr), static_cast<uint32_t>(code), env->GetStringUTFChars(message, nullptr));
}

extern "C" JNIEXPORT void
    JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_BillingManager_glueIAPAvailable(JNIEnv* env, jobject /*thiz*/, jstring name)
{
    jni_initvm(env);
    iap_available(env->GetStringUTFChars(name, nullptr));
}

extern "C" JNIEXPORT void
    JNICALL Java_com_zzzsAndroid.AppPackageNamezzze_LocationForegroundService_glueAppLocationServiceStarted(JNIEnv* env, jobject /*thiz*/)
{
    jni_initvm(env);
    locationServiceActive = true;
}
#pragma clang diagnostic pop
