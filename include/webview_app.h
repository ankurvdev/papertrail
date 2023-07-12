
#include "CommonMacros.h"

#include <cstdint>

#if !defined WEBVIEW_APP_EXPORT
#error "Please defined WEBVIEW_APP_EXPORT"
#endif

SUPPRESS_WARNINGS_START
#if defined __ANDROID__
#include <jni.h>
#endif
SUPPRESS_WARNINGS_END

typedef void (*callback_void)();
typedef void (*callback_str)(const char*);

struct app_callbacks
{
#if defined __ANDROID__
    callback_void on_request_location_update_start;
    callback_void on_request_location_update_stop;
    callback_void on_request_orientation_update_start;
    callback_void on_request_orientation_update_stop;
#endif
    callback_void on_request_foreground_service_start;
    callback_void on_request_foreground_service_stop;
    callback_str  on_request_iap_init;
    callback_str  on_request_iap_feature;
};

extern "C" WEBVIEW_APP_EXPORT void        app_init(app_callbacks const*);
extern "C" WEBVIEW_APP_EXPORT void        app_start();
extern "C" WEBVIEW_APP_EXPORT void        app_stop();
extern "C" WEBVIEW_APP_EXPORT const char* get_webserver_url();

// Features
extern "C" WEBVIEW_APP_EXPORT void iap_reset();
extern "C" WEBVIEW_APP_EXPORT void iap_available(const char* name);
extern "C" WEBVIEW_APP_EXPORT void iap_request_failed(const char* name, uint32_t code, const char* message);

#if defined                        __ANDROID__
extern "C" WEBVIEW_APP_EXPORT void android_app_init(JNIEnv* jniEnv);
extern "C" WEBVIEW_APP_EXPORT void android_update_location(JNIEnv* jniEnv, jobject thiz, jobject location);
extern "C" WEBVIEW_APP_EXPORT void android_update_orientation(JNIEnv* jniEnv, jobject thiz, jint orientation);
#endif
