#ifndef QTRACE_TRACECONFIG_H
#define QTRACE_TRACECONFIG_H

#include <cstddef>
#include <string>
#include <vector>

enum class TraceFilterOp {
    None,
    Eq,
    Ne,
    Gt,
    Ge,
    Lt,
    Le,
};

struct TraceFilterConfig {
    bool enabled = false;
    int arg_index = 0;
    TraceFilterOp op = TraceFilterOp::None;
    size_t value = 0;
};

struct CustomHookConfig {
    size_t offset = 0;
    std::string handler;
};

struct TraceConfig {
    std::string target_lib;
    size_t target_offset = 0;
    int hook_argc = 0;
    size_t buffer_size = 0x10000000;
    bool debug_insn = false;
    bool enable_jni_trace = true;
    bool enable_libc_trace = true;
    TraceFilterConfig filter;
    std::vector<CustomHookConfig> custom_hooks;
};

bool loadTraceConfigFromJson(const char* json, TraceConfig& out_config, std::string& error);

#endif  // QTRACE_TRACECONFIG_H
