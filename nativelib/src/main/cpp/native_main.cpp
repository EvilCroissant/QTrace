#include <jni.h>
#include <string>
#include <dlfcn.h>
#include <utility>
#include "vm.h"
#include "TraceConfig.h"
#include "HookUtils.h"
#include "qbdihook.h"
#include "jnitrace.h"
#include "libctrace.h"
#include "shadowhook.h"
using namespace std;

namespace {

TraceConfig g_trace_config{};
bool g_has_config = false;
bool g_runtime_initialized = false;
bool g_trace_started = false;
string g_last_error;

void setLastError(const string& message)
{
    g_last_error = message;
    LOGE("%s", g_last_error.c_str());
}

void clearLastError()
{
    g_last_error.clear();
}

bool registerBuiltinLibcTraces()
{
    void* handle = dlopen("libc.so", RTLD_LAZY);
    if (handle == nullptr) {
        setLastError("dlopen libc.so fail");
        return false;
    }
    addLibctrace(handle, libc_access, "access");
    addLibctrace(handle, libc_system_property_get, "__system_property_get");
    addLibctrace(handle, libc_memcpy, "memcpy");
    addLibctrace(handle, libc_pthread_create, "pthread_create");
    addLibctrace(handle, libc_fopen, "fopen");
    addLibctrace(handle, libc_lstat, "lstat");
    addLibctrace(handle, libc_stat, "stat");
    addLibctrace(handle, libc_fstatat, "fstatat");
    addLibctrace(handle, libc_execve, "execve");
    addLibctrace(handle, libc_clock_gettime, "clock_gettime");
    addLibctrace(handle, libc_strlen, "strlen");
    addLibctrace(handle, libc_memmove, "memmove");
    addLibctrace(handle, libc_memset, "memset");
    addLibctrace(handle, libc_kill, "kill");
    addLibctrace(handle, libc_abort, "abort");
    addLibctrace(handle, libc_exit, "exit");
    dlclose(handle);
    return true;
}

bool registerBuiltinJniTraces()
{
    initJni();
    if (pJFunc == nullptr) {
        setLastError("JNI trace 初始化失败");
        return false;
    }
    addJNITrace((void*)pJFunc->NewStringUTF, "NewStringUTF", trace_NewStringUTF);
    addJNITrace((void*)pJFunc->GetStringUTFChars, "GetStringUTFChars", trace_GetStringUTFChars);
    addJNITrace((void*)pJFunc->NewString, "NewString", trace_NewString);
    addJNITrace((void*)pJFunc->FindClass, "FindClass", trace_FindClass);
    addJNITrace((void*)pJFunc->GetFieldID, "GetFieldID", trace_GetFieldID);
    addJNITrace((void*)pJFunc->GetMethodID, "GetMethodID", trace_GetMethodID);
    addJNITrace((void*)pJFunc->RegisterNatives, "RegisterNatives", trace_RegisterNatives);
    addJNITrace((void*)pJFunc->GetLongField, "GetLongField", trace_GetLongField);
    addJNITrace((void*)pJFunc->GetStaticMethodID, "GetStaticMethodID", trace_GetStaticMethodID);
    addJNITrace((void*)pJFunc->CallStaticObjectMethodV, "CallStaticObjectMethodV", trace_CallStaticObjectMethodV);
    addJNITrace((void*)pJFunc->GetStaticFieldID, "GetStaticFieldID", trace_GetStaticFieldID);
    addJNITrace((void*)pJFunc->GetIntField, "GetIntField", trace_GetIntField);
    addJNITrace((void*)pJFunc->GetByteArrayRegion, "GetByteArrayRegion", trace_GetByteArrayRegion);
    addJNITrace((void*)pJFunc->GetArrayLength, "GetArrayLength", trace_GetArrayLength);
    addJNITrace((void*)pJFunc->GetByteArrayElements, "GetByteArrayElements", trace_GetByteArrayElements);
    return true;
}

void* resolveTraceHookEntry(int hook_argc)
{
    switch (hook_argc) {
        case 0: return (void*)hook_and_trace_arg0;
        case 1: return (void*)hook_and_trace_arg1;
        case 2: return (void*)hook_and_trace_arg2;
        case 3: return (void*)hook_and_trace_arg3;
        case 4: return (void*)hook_and_trace_arg4;
        default: return nullptr;
    }
}

void** resolveOriginalFuncSlot(int hook_argc)
{
    switch (hook_argc) {
        case 0: return (void**)&ori_arg0;
        case 1: return (void**)&ori_arg1;
        case 2: return (void**)&ori_arg2;
        case 3: return (void**)&ori_arg3;
        case 4: return (void**)&ori_arg4;
        default: return nullptr;
    }
}

bool registerConfiguredHooks()
{
    initHookData();
    for (const auto& hook : g_trace_config.custom_hooks) {
        string error;
        if (!addNamedHook(hook.offset, hook.handler, error)) {
            setLastError(error);
            return false;
        }
    }
    return true;
}

bool init_shadowhook()
{
    if (g_runtime_initialized) {
        return true;
    }
    int result = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, true);
    if (result != 0) {
        int error_number = shadowhook_get_init_errno();
        const char* error_message = shadowhook_to_errmsg(error_number);
        setLastError(string("shadowhook init fail: ") + (error_message == nullptr ? "unknown" : error_message));
        return false;
    }
    shadowhook_set_debuggable(true);
    g_runtime_initialized = true;
    return true;
}

bool startTrace()
{
    if (!g_has_config) {
        setLastError("trace config 尚未加载");
        return false;
    }

    if (g_trace_started) {
        setLastError("trace 已经启动");
        return false;
    }

    setBufferSize(static_cast<int>(g_trace_config.buffer_size));
    enableDebugInsn(g_trace_config.debug_insn);
    enable_jni_trace_debug(g_trace_config.enable_jni_trace);
    enable_libc_trace_debug(g_trace_config.enable_libc_trace);
    configureTraceFilter(g_trace_config.filter);

    if (g_trace_config.enable_jni_trace && !registerBuiltinJniTraces()) {
        return false;
    }
    if (g_trace_config.enable_libc_trace) {
        initLibcTrace();
        if (!registerBuiltinLibcTraces()) {
            return false;
        }
    }
    if (!registerConfiguredHooks()) {
        return false;
    }

    auto soinfo = getLoadedSoInfoByName(g_trace_config.target_lib.c_str());
    if (soinfo.start == 0) {
        setLastError("未找到已加载目标 so: " + g_trace_config.target_lib);
        return false;
    }

    void* trace_entry = resolveTraceHookEntry(g_trace_config.hook_argc);
    void** original_slot = resolveOriginalFuncSlot(g_trace_config.hook_argc);
    if (trace_entry == nullptr || original_slot == nullptr) {
        setLastError("hook_argc 不支持");
        return false;
    }

    _g_trace_data = new g_trace_data();
    _g_trace_data->base = soinfo.start;
    _g_trace_data->start = soinfo.start;
    _g_trace_data->end = soinfo.start + soinfo.size;
    _g_trace_data->target = g_trace_config.target_offset;
    _g_trace_data->hooktask = shadowhook_hook_func_addr(
        (void*)(_g_trace_data->start + _g_trace_data->target),
        trace_entry,
        original_slot);

    if (_g_trace_data->hooktask == nullptr) {
        int error_number = shadowhook_get_errno();
        const char* error_message = shadowhook_to_errmsg(error_number);
        delete _g_trace_data;
        _g_trace_data = nullptr;
        setLastError(string("hook target fail: ") + (error_message == nullptr ? "unknown" : error_message));
        return false;
    }

    g_trace_started = true;
    clearLastError();
    return true;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int qtrace_init()
{
    return init_shadowhook() ? 0 : -1;
}

extern "C" __attribute__((visibility("default"))) int qtrace_apply_config(const char* json)
{
    if (!init_shadowhook()) {
        return -1;
    }
    if (g_trace_started) {
        setLastError("trace 已经启动，不能再更新配置");
        return -1;
    }

    TraceConfig parsed_config;
    string error;
    if (!loadTraceConfigFromJson(json, parsed_config, error)) {
        setLastError("config 解析失败: " + error);
        return -1;
    }

    g_trace_config = std::move(parsed_config);
    g_has_config = true;
    clearLastError();
    return 0;
}

extern "C" __attribute__((visibility("default"))) int qtrace_start_trace()
{
    if (!init_shadowhook()) {
        return -1;
    }
    return startTrace() ? 0 : -1;
}

extern "C" __attribute__((visibility("default"))) const char* qtrace_last_error()
{
    return g_last_error.c_str();
}

__unused __attribute__((constructor)) void init_main()
{
    LOGE("Injected!");
    qtrace_init();
}
