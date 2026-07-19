#include "core/json.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace sillage::json {

namespace {

// RAII depth counter: increments while inside a nested container.
struct DepthGuard {
    int& d;
    explicit DepthGuard(int& depth) : d(depth) { ++d; }
    ~DepthGuard() { --d; }
};

class Parser {
public:
    static constexpr int kMaxDepth = 64;

    explicit Parser(const std::string& text) : text_(text) {}

    ParseResult run() {
        skipWs();
        auto v = parseValue();
        if (!v) {
            return {std::nullopt, error_};
        }
        skipWs();
        if (pos_ != text_.size()) {
            return {std::nullopt, fail("trailing characters")};
        }
        return {std::move(v), {}};
    }

private:
    std::string fail(const std::string& what) {
        if (error_.empty()) {
            error_ = what + " at offset " + std::to_string(pos_);
        }
        return error_;
    }

    void skipWs() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool literal(const char* word) {
        const size_t len = std::string_view(word).size();
        if (text_.compare(pos_, len, word) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    std::optional<Value> parseValue() {
        if (pos_ >= text_.size()) {
            fail("unexpected end");
            return std::nullopt;
        }
        // Bound recursion: this is a recursive-descent parser fed by the
        // network (POST /api/config), so deeply nested input would otherwise
        // overflow the stack. Config JSON nests only a handful of levels.
        if (depth_ >= kMaxDepth) {
            fail("nesting too deep");
            return std::nullopt;
        }
        const char c = text_[pos_];
        if (c == '{') {
            return parseObject();
        }
        if (c == '[') {
            return parseArray();
        }
        if (c == '"') {
            auto s = parseString();
            if (!s) {
                return std::nullopt;
            }
            return Value(std::move(*s));
        }
        if (literal("true")) {
            return Value(true);
        }
        if (literal("false")) {
            return Value(false);
        }
        if (literal("null")) {
            return Value(nullptr);
        }
        return parseNumber();
    }

    std::optional<Value> parseNumber() {
        const size_t start = pos_;
        if (consume('-')) {
        }
        while (pos_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.' ||
                text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '+' ||
                text_[pos_] == '-')) {
            ++pos_;
        }
        double value = 0.0;
        const auto [ptr, ec] =
            std::from_chars(text_.data() + start, text_.data() + pos_, value);
        if (ec != std::errc{} || ptr != text_.data() + pos_ || pos_ == start) {
            fail("invalid number");
            return std::nullopt;
        }
        return Value(value);
    }

    std::optional<std::string> parseString() {
        if (!consume('"')) {
            fail("expected string");
            return std::nullopt;
        }
        std::string out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    break;
                }
                const char esc = text_[pos_++];
                switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (pos_ + 4 > text_.size()) {
                        fail("bad \\u escape");
                        return std::nullopt;
                    }
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = text_[pos_++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') { code |= static_cast<unsigned>(h - '0'); }
                        else if (h >= 'a' && h <= 'f') { code |= static_cast<unsigned>(h - 'a' + 10); }
                        else if (h >= 'A' && h <= 'F') { code |= static_cast<unsigned>(h - 'A' + 10); }
                        else {
                            fail("bad \\u escape");
                            return std::nullopt;
                        }
                    }
                    // BMP only (config files have no business in surrogates).
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default:
                    fail("bad escape");
                    return std::nullopt;
                }
            } else {
                out += c;
            }
        }
        fail("unterminated string");
        return std::nullopt;
    }

    std::optional<Value> parseArray() {
        DepthGuard guard(depth_);
        consume('[');
        Array arr;
        skipWs();
        if (consume(']')) {
            return Value(std::move(arr));
        }
        while (true) {
            skipWs();
            auto v = parseValue();
            if (!v) {
                return std::nullopt;
            }
            arr.push_back(std::move(*v));
            skipWs();
            if (consume(']')) {
                return Value(std::move(arr));
            }
            if (!consume(',')) {
                fail("expected ',' or ']'");
                return std::nullopt;
            }
        }
    }

    std::optional<Value> parseObject() {
        DepthGuard guard(depth_);
        consume('{');
        Object obj;
        skipWs();
        if (consume('}')) {
            return Value(std::move(obj));
        }
        while (true) {
            skipWs();
            auto key = parseString();
            if (!key) {
                return std::nullopt;
            }
            skipWs();
            if (!consume(':')) {
                fail("expected ':'");
                return std::nullopt;
            }
            skipWs();
            auto v = parseValue();
            if (!v) {
                return std::nullopt;
            }
            obj.emplace(std::move(*key), std::move(*v));
            skipWs();
            if (consume('}')) {
                return Value(std::move(obj));
            }
            if (!consume(',')) {
                fail("expected ',' or '}'");
                return std::nullopt;
            }
        }
    }

    const std::string& text_;
    size_t pos_ = 0;
    int depth_ = 0;
    std::string error_;
};

void serializeTo(const Value& v, std::string& out, int indent, int depth);

void appendEscaped(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void newline(std::string& out, int indent, int depth) {
    if (indent > 0) {
        out += '\n';
        out.append(static_cast<size_t>(indent * depth), ' ');
    }
}

void serializeTo(const Value& v, std::string& out, int indent, int depth) {
    if (v.isNull()) {
        out += "null";
    } else if (v.isBool()) {
        out += v.asBool() ? "true" : "false";
    } else if (v.isNumber()) {
        const double d = v.asNumber();
        if (d == std::floor(d) && std::abs(d) < 1e15) {
            out += std::to_string(static_cast<long long>(d));
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.6g", d);
            out += buf;
        }
    } else if (v.isString()) {
        appendEscaped(out, v.asString());
    } else if (v.isArray()) {
        out += '[';
        const Array& arr = v.asArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i) {
                out += ',';
                if (indent) {
                    out += ' ';
                }
            }
            serializeTo(arr[i], out, 0, depth); // arrays stay inline
        }
        out += ']';
    } else {
        out += '{';
        const Object& obj = v.asObject();
        size_t i = 0;
        for (const auto& [key, value] : obj) {
            if (i++) {
                out += ',';
            }
            newline(out, indent, depth + 1);
            appendEscaped(out, key);
            out += indent ? ": " : ":";
            serializeTo(value, out, indent, depth + 1);
        }
        if (!obj.empty()) {
            newline(out, indent, depth);
        }
        out += '}';
    }
}

} // namespace

std::string Value::serialize(int indent) const {
    std::string out;
    serializeTo(*this, out, indent, 0);
    return out;
}

ParseResult parse(const std::string& text) { return Parser(text).run(); }

} // namespace sillage::json
