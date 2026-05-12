#include "TraceConfig.h"

#include "JsonLite.h"

namespace {

using jsonlite::Object;
using jsonlite::Value;

bool parseBoolean(const Value* value, bool& out, std::string& error, const char* field_name) {
    if (value == nullptr) {
        return true;
    }
    if (value->type != Value::Type::Bool) {
        error = std::string(field_name) + " 必须是布尔值";
        return false;
    }
    out = value->bool_value;
    return true;
}

bool parseString(const Value* value, std::string& out, std::string& error, const char* field_name) {
    if (value == nullptr || value->type != Value::Type::String || value->string_value.empty()) {
        error = std::string(field_name) + " 必须是非空字符串";
        return false;
    }
    out = value->string_value;
    return true;
}

bool parseFilter(const Value* filter_value, TraceFilterConfig& out, std::string& error) {
    if (filter_value == nullptr) {
        return true;
    }
    const Object* object = filter_value->asObject();
    if (object == nullptr) {
        error = "filter 必须是对象";
        return false;
    }

    const Value* arg_index = filter_value->get("arg_index");
    const Value* op = filter_value->get("op");
    const Value* value = filter_value->get("value");
    if (arg_index == nullptr || op == nullptr || value == nullptr) {
        error = "filter 必须包含 arg_index、op、value";
        return false;
    }

    size_t parsed_arg_index = 0;
    if (!jsonlite::parseUnsigned(*arg_index, parsed_arg_index, error)) {
        error = "filter.arg_index 解析失败: " + error;
        return false;
    }
    if (op->type != Value::Type::String) {
        error = "filter.op 必须是字符串";
        return false;
    }
    if (!jsonlite::parseUnsigned(*value, out.value, error)) {
        error = "filter.value 解析失败: " + error;
        return false;
    }

    out.enabled = true;
    out.arg_index = static_cast<int>(parsed_arg_index);
    if (op->string_value == "eq") out.op = TraceFilterOp::Eq;
    else if (op->string_value == "ne") out.op = TraceFilterOp::Ne;
    else if (op->string_value == "gt") out.op = TraceFilterOp::Gt;
    else if (op->string_value == "ge") out.op = TraceFilterOp::Ge;
    else if (op->string_value == "lt") out.op = TraceFilterOp::Lt;
    else if (op->string_value == "le") out.op = TraceFilterOp::Le;
    else {
        error = "filter.op 仅支持 eq/ne/gt/ge/lt/le";
        return false;
    }
    return true;
}

bool parseCustomHooks(const Value* hooks_value, std::vector<CustomHookConfig>& out, std::string& error) {
    if (hooks_value == nullptr) {
        return true;
    }
    const auto* array = hooks_value->asArray();
    if (array == nullptr) {
        error = "custom_hooks 必须是数组";
        return false;
    }

    for (const auto& item : *array) {
        const Object* object = item->asObject();
        if (object == nullptr) {
            error = "custom_hooks 的每一项都必须是对象";
            return false;
        }
        CustomHookConfig config;
        const Value* offset = item->get("offset");
        const Value* handler = item->get("handler");
        if (offset == nullptr || handler == nullptr) {
            error = "custom_hooks 的每一项都必须包含 offset 和 handler";
            return false;
        }
        if (!jsonlite::parseUnsigned(*offset, config.offset, error)) {
            error = "custom_hooks.offset 解析失败: " + error;
            return false;
        }
        if (!parseString(handler, config.handler, error, "custom_hooks.handler")) {
            return false;
        }
        out.push_back(std::move(config));
    }
    return true;
}

bool validateConfig(const TraceConfig& config, std::string& error) {
    if (config.target_lib.empty()) {
        error = "target_lib 不能为空";
        return false;
    }
    if (config.target_offset == 0) {
        error = "target_offset 不能为 0";
        return false;
    }
    if (config.hook_argc < 0 || config.hook_argc > 4) {
        error = "hook_argc 仅支持 0 到 4";
        return false;
    }
    if (config.filter.enabled && config.filter.arg_index >= config.hook_argc) {
        error = "filter.arg_index 超出了 hook_argc 范围";
        return false;
    }
    return true;
}

}  // namespace

bool loadTraceConfigFromJson(const char* json, TraceConfig& out_config, std::string& error) {
    std::shared_ptr<Value> root;
    if (!jsonlite::parse(json, root, error)) {
        return false;
    }

    const Object* object = root->asObject();
    if (object == nullptr) {
        error = "顶层 JSON 必须是对象";
        return false;
    }

    TraceConfig parsed;
    if (!parseString(root->get("target_lib"), parsed.target_lib, error, "target_lib")) {
        return false;
    }
    if (root->get("target_offset") == nullptr) {
        error = "target_offset 缺失";
        return false;
    }
    if (!jsonlite::parseUnsigned(*root->get("target_offset"), parsed.target_offset, error)) {
        error = "target_offset 解析失败: " + error;
        return false;
    }
    if (root->get("hook_argc") == nullptr) {
        error = "hook_argc 缺失";
        return false;
    }
    size_t hook_argc = 0;
    if (!jsonlite::parseUnsigned(*root->get("hook_argc"), hook_argc, error)) {
        error = "hook_argc 解析失败: " + error;
        return false;
    }
    parsed.hook_argc = static_cast<int>(hook_argc);

    if (root->get("buffer_size") != nullptr &&
        !jsonlite::parseUnsigned(*root->get("buffer_size"), parsed.buffer_size, error)) {
        error = "buffer_size 解析失败: " + error;
        return false;
    }
    if (!parseBoolean(root->get("debug_insn"), parsed.debug_insn, error, "debug_insn") ||
        !parseBoolean(root->get("enable_jni_trace"), parsed.enable_jni_trace, error, "enable_jni_trace") ||
        !parseBoolean(root->get("enable_libc_trace"), parsed.enable_libc_trace, error, "enable_libc_trace") ||
        !parseFilter(root->get("filter"), parsed.filter, error) ||
        !parseCustomHooks(root->get("custom_hooks"), parsed.custom_hooks, error)) {
        return false;
    }
    if (!validateConfig(parsed, error)) {
        return false;
    }

    out_config = std::move(parsed);
    return true;
}
