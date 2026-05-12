#include "JsonLite.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace jsonlite {

const Object* Value::asObject() const {
    return type == Type::Object ? object_value.get() : nullptr;
}

const Array* Value::asArray() const {
    return type == Type::Array ? array_value.get() : nullptr;
}

const Value* Value::get(const char* key) const {
    const Object* object = asObject();
    if (object == nullptr) {
        return nullptr;
    }
    auto it = object->find(key);
    return it == object->end() ? nullptr : it->second.get();
}

namespace {

class Parser {
public:
    explicit Parser(const char* input) : input_(input == nullptr ? "" : input) {}

    bool parse(std::shared_ptr<Value>& out) {
        skipWhitespace();
        out = parseValue();
        if (out == nullptr) {
            return false;
        }
        skipWhitespace();
        if (!isEnd()) {
            setError("JSON 尾部存在多余内容");
            return false;
        }
        return true;
    }

    const std::string& error() const {
        return error_;
    }

private:
    const std::string input_;
    size_t pos_ = 0;
    std::string error_;

    bool isEnd() const {
        return pos_ >= input_.size();
    }

    char peek() const {
        return isEnd() ? '\0' : input_[pos_];
    }

    char advance() {
        return isEnd() ? '\0' : input_[pos_++];
    }

    void skipWhitespace() {
        while (!isEnd() && std::isspace(static_cast<unsigned char>(peek()))) {
            ++pos_;
        }
    }

    void setError(const char* message) {
        if (error_.empty()) {
            error_ = std::string(message) + "，位置 " + std::to_string(pos_);
        }
    }

    bool consume(char expected) {
        if (peek() != expected) {
            setError("JSON 语法错误");
            return false;
        }
        ++pos_;
        return true;
    }

    std::shared_ptr<Value> parseValue() {
        skipWhitespace();
        switch (peek()) {
            case '{':
                return parseObject();
            case '[':
                return parseArray();
            case '"':
                return parseStringValue();
            case 't':
                return parseLiteral("true", true);
            case 'f':
                return parseLiteral("false", false);
            case 'n':
                return parseNull();
            default:
                if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek()))) {
                    return parseNumber();
                }
                setError("无法识别的 JSON 值");
                return nullptr;
        }
    }

    std::shared_ptr<Value> parseObject() {
        if (!consume('{')) {
            return nullptr;
        }
        auto value = std::make_shared<Value>();
        value->type = Value::Type::Object;
        value->object_value = std::make_shared<Object>();
        skipWhitespace();
        if (peek() == '}') {
            ++pos_;
            return value;
        }
        while (true) {
            std::string key;
            if (!parseString(key)) {
                return nullptr;
            }
            skipWhitespace();
            if (!consume(':')) {
                return nullptr;
            }
            auto child = parseValue();
            if (child == nullptr) {
                return nullptr;
            }
            (*value->object_value)[key] = child;
            skipWhitespace();
            if (peek() == '}') {
                ++pos_;
                return value;
            }
            if (!consume(',')) {
                return nullptr;
            }
            skipWhitespace();
        }
    }

    std::shared_ptr<Value> parseArray() {
        if (!consume('[')) {
            return nullptr;
        }
        auto value = std::make_shared<Value>();
        value->type = Value::Type::Array;
        value->array_value = std::make_shared<Array>();
        skipWhitespace();
        if (peek() == ']') {
            ++pos_;
            return value;
        }
        while (true) {
            auto child = parseValue();
            if (child == nullptr) {
                return nullptr;
            }
            value->array_value->push_back(child);
            skipWhitespace();
            if (peek() == ']') {
                ++pos_;
                return value;
            }
            if (!consume(',')) {
                return nullptr;
            }
            skipWhitespace();
        }
    }

    std::shared_ptr<Value> parseStringValue() {
        std::string string_value;
        if (!parseString(string_value)) {
            return nullptr;
        }
        auto value = std::make_shared<Value>();
        value->type = Value::Type::String;
        value->string_value = std::move(string_value);
        return value;
    }

    bool parseString(std::string& out) {
        if (!consume('"')) {
            return false;
        }
        while (!isEnd()) {
            char current = advance();
            if (current == '"') {
                return true;
            }
            if (current == '\\') {
                if (isEnd()) {
                    setError("字符串转义不完整");
                    return false;
                }
                char escaped = advance();
                switch (escaped) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default:
                        setError("暂不支持该 JSON 转义字符");
                        return false;
                }
                continue;
            }
            out.push_back(current);
        }
        setError("字符串未正常结束");
        return false;
    }

    std::shared_ptr<Value> parseLiteral(const char* literal, bool bool_value) {
        size_t length = std::strlen(literal);
        if (input_.compare(pos_, length, literal) != 0) {
            setError("JSON 字面量不合法");
            return nullptr;
        }
        pos_ += length;
        auto value = std::make_shared<Value>();
        value->type = Value::Type::Bool;
        value->bool_value = bool_value;
        return value;
    }

    std::shared_ptr<Value> parseNull() {
        if (input_.compare(pos_, 4, "null") != 0) {
            setError("JSON null 不合法");
            return nullptr;
        }
        pos_ += 4;
        return std::make_shared<Value>();
    }

    std::shared_ptr<Value> parseNumber() {
        size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            setError("JSON 数字不合法");
            return nullptr;
        }
        while (!isEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            ++pos_;
        }
        std::string number_text = input_.substr(start, pos_ - start);
        auto value = std::make_shared<Value>();
        value->type = Value::Type::Integer;
        value->int_value = std::strtoll(number_text.c_str(), nullptr, 10);
        return value;
    }
};

}  // namespace

bool parse(const char* json, std::shared_ptr<Value>& out, std::string& error) {
    Parser parser(json);
    if (!parser.parse(out)) {
        error = parser.error();
        return false;
    }
    return true;
}

bool parseUnsigned(const Value& value, size_t& out, std::string& error) {
    if (value.type == Value::Type::Integer) {
        if (value.int_value < 0) {
            error = "数值不能为负数";
            return false;
        }
        out = static_cast<size_t>(value.int_value);
        return true;
    }
    if (value.type != Value::Type::String) {
        error = "字段类型必须是整数或字符串";
        return false;
    }
    if (!value.string_value.empty() && value.string_value[0] == '-') {
        error = "数值不能为负数";
        return false;
    }
    char* end = nullptr;
    unsigned long long parsed = std::strtoull(value.string_value.c_str(), &end, 0);
    if (end == value.string_value.c_str() || *end != '\0') {
        error = "数字字符串格式不合法: " + value.string_value;
        return false;
    }
    out = static_cast<size_t>(parsed);
    return true;
}

}  // namespace jsonlite
