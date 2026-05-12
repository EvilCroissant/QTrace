#ifndef QTRACE_JSONLITE_H
#define QTRACE_JSONLITE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace jsonlite {

struct Value;

using Object = std::unordered_map<std::string, std::shared_ptr<Value>>;
using Array = std::vector<std::shared_ptr<Value>>;

struct Value {
    enum class Type {
        Null,
        Bool,
        Integer,
        String,
        Object,
        Array,
    };

    Type type = Type::Null;
    bool bool_value = false;
    long long int_value = 0;
    std::string string_value;
    std::shared_ptr<Object> object_value;
    std::shared_ptr<Array> array_value;

    const Object* asObject() const;
    const Array* asArray() const;
    const Value* get(const char* key) const;
};

bool parse(const char* json, std::shared_ptr<Value>& out, std::string& error);
bool parseUnsigned(const Value& value, size_t& out, std::string& error);

}  // namespace jsonlite

#endif  // QTRACE_JSONLITE_H
