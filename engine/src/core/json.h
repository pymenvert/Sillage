#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace sillage::json {

// Minimal JSON DOM: parse + serialize, strict RFC 8259 subset (no comments,
// UTF-8 passthrough). Enough for the project config file; replaced only if a
// profiler ever says so.
class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

class Value {
public:
    Value() : data_(nullptr) {}
    Value(std::nullptr_t) : data_(nullptr) {}
    Value(bool b) : data_(b) {}
    Value(double d) : data_(d) {}
    Value(int i) : data_(static_cast<double>(i)) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(Array a) : data_(std::move(a)) {}
    Value(Object o) : data_(std::move(o)) {}

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(data_); }
    bool isBool() const { return std::holds_alternative<bool>(data_); }
    bool isNumber() const { return std::holds_alternative<double>(data_); }
    bool isString() const { return std::holds_alternative<std::string>(data_); }
    bool isArray() const { return std::holds_alternative<Array>(data_); }
    bool isObject() const { return std::holds_alternative<Object>(data_); }

    bool asBool(bool fallback = false) const {
        return isBool() ? std::get<bool>(data_) : fallback;
    }
    double asNumber(double fallback = 0.0) const {
        return isNumber() ? std::get<double>(data_) : fallback;
    }
    const std::string& asString() const {
        static const std::string empty;
        return isString() ? std::get<std::string>(data_) : empty;
    }
    const Array& asArray() const {
        static const Array empty;
        return isArray() ? std::get<Array>(data_) : empty;
    }
    const Object& asObject() const {
        static const Object empty;
        return isObject() ? std::get<Object>(data_) : empty;
    }

    // Object member access; a null Value for missing keys.
    const Value& operator[](const std::string& key) const {
        static const Value null;
        if (!isObject()) {
            return null;
        }
        const auto it = std::get<Object>(data_).find(key);
        return it == std::get<Object>(data_).end() ? null : it->second;
    }

    std::string serialize(int indent = 0) const;

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data_;
};

struct ParseResult {
    std::optional<Value> value;
    std::string error; // empty on success; includes offset otherwise
};

ParseResult parse(const std::string& text);

} // namespace sillage::json
