#ifndef JSON_H
#define JSON_H

#include "common.h"
#include "util.h"
#include <cctype>
#include <charconv>
#include <iomanip>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

class JsonParseError final : public std::runtime_error {
    size_t pos_;

  public:
    JsonParseError(std::string const& msg, size_t pos)
        : std::runtime_error(STR(msg << " at offset " << pos)), pos_(pos) {}

    [[nodiscard]] size_t pos() const { return pos_; }
};

struct JsonArray;
struct JsonObject;

using JsonValue = std::variant<std::nullptr_t, bool, int, double, std::string,
                               JsonArray, JsonObject>;

struct JsonArray : std::vector<JsonValue> {
    using vector::vector;
};

struct JsonObject : std::map<std::string, JsonValue> {
    using map::map;
};

template <typename T>
T json_query(JsonValue const& json, std::string_view path) {
    JsonValue const* v = &json;

    while (!path.empty()) {
        auto end = path.find('.');
        std::string_view part;

        if (end != std::string::npos) {
            part = path.substr(0, end);
        } else {
            part = path;
        }

        if (part.empty()) {
            throw std::invalid_argument("Invalid path");
        }

        if (auto idx = to_number<long>(part); idx) {
            if (auto* arr = std::get_if<JsonArray>(v)) {
                if (idx >= arr->size()) {
                    throw std::out_of_range("Array index out of range");
                }
                v = &(*arr)[*idx];
            } else {
                throw std::invalid_argument("Expected array");
            }
        } else {
            std::string key{part};
            if (auto* obj = std::get_if<JsonObject>(v);
                obj && obj->contains(key)) {
                v = &obj->at(key);
            } else {
                throw std::invalid_argument("Expected object");
            }
        }

        if (end != std::string::npos) {
            path = path.substr(end + 1);
            continue;
        }

        break;
    }

    if (!std::holds_alternative<T>(*v)) {
        throw std::invalid_argument("Invalid type");
    }

    return std::get<T>(*v);
}

class JsonParser {
  public:
    static JsonValue parse(std::string const& input) {
        JsonParser parser{input};
        return parser.parse();
    }

  private:
    explicit JsonParser(std::string const& input) : input_{input} {}

    JsonValue parse();

    [[nodiscard]] bool eof() const { return pos_ >= input_.size(); }
    [[nodiscard]] char current() const { return !eof() ? input_[pos_] : '\0'; }
    void next(size_t n = 1);
    void skip_whitespace();

    JsonValue parse_value();
    JsonObject parse_object();
    JsonArray parse_array();
    std::string parse_string();
    bool parse_boolean();
    std::nullptr_t parse_null();
    JsonValue parse_number();

    std::string_view input_;
    size_t pos_{};
};

std::ostream& operator<<(std::ostream& os, JsonValue const& jv);

struct JsonValuePrinter {

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::ostream& os;

    void operator()(std::nullptr_t) const { os << "null"; }

    void operator()(bool b) const { os << (b ? "true" : "false"); }

    void operator()(int i) const { os << i; }

    void operator()(double d) const { os << d; }

    void operator()(std::string const& s) const {
        os << '"';
        for (char c : s) {
            switch (c) {
            case '"':
                os << "\\\"";
                break;
            case '\\':
                os << "\\\\";
                break;
            case '\b':
                os << "\\b";
                break;
            case '\f':
                os << "\\f";
                break;
            case '\n':
                os << "\\n";
                break;
            case '\r':
                os << "\\r";
                break;
            case '\t':
                os << "\\t";
                break;
            default:
                os << c;
                break;
            }
        }
        os << '"';
    }

    void operator()(JsonArray const& arr) const {
        os << '[';
        bool first = true;
        for (auto const& elem : arr) {
            if (!first) {
                os << ',';
            }
            first = false;
            std::visit(JsonValuePrinter{os}, elem);
        }
        os << ']';
    }

    void operator()(JsonObject const& obj) const {
        os << '{';
        bool first = true;
        for (auto const& [key, value] : obj) {
            if (!first) {
                os << ',';
            }
            first = false;
            (*this)(key);
            os << ':';
            std::visit(JsonValuePrinter{os}, value);
        }
        os << '}';
    }
};

inline JsonValue JsonParser::parse() {
    JsonValue value = parse_value();
    if (!eof()) {
        throw JsonParseError(STR("Unexpected reminder after JSON value parsed: "
                                 << input_.substr(pos_)),
                             pos_);
    }
    return value;
}

inline void JsonParser::next(size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (eof()) {
            return;
        }
        ++pos_;
    }
}

inline void JsonParser::skip_whitespace() {
    while (!eof() && std::isspace(input_[pos_])) {
        next();
    }
}

inline JsonValue JsonParser::parse_value() {
    skip_whitespace();

    switch (char const c = current()) {
    case '{':
        return parse_object();
    case '[':
        return parse_array();
    case '"':
        return parse_string();
    case 't':
    case 'f':
        return parse_boolean();
    case 'n':
        return parse_null();
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return parse_number();
    default:
        throw JsonParseError(STR("Unexpected character '" << c << "'"), pos_);
    }
}

inline JsonObject JsonParser::parse_object() {
    JsonObject obj;
    next(); // skip '{'

    while (true) {
        skip_whitespace();
        if (current() == '}') {
            next();
            break;
        }

        auto const key = parse_string();
        skip_whitespace();

        if (current() != ':') {
            throw JsonParseError("Expected ':'", pos_);
        }
        next();

        obj.emplace(key, parse_value());
        skip_whitespace();

        if (current() == ',') {
            next();
        } else if (current() != '}') {
            throw JsonParseError("Expected ',' or '}'", pos_);
        }
    }
    return obj;
}

inline JsonArray JsonParser::parse_array() {
    JsonArray arr;
    next(); // skip '['

    while (true) {
        skip_whitespace();
        if (current() == ']') {
            next();
            break;
        }

        arr.emplace_back(parse_value());
        skip_whitespace();

        if (current() == ',') {
            next();
        } else if (current() != ']') {
            throw JsonParseError("Expected ',' or ']'", pos_);
        }
    }
    return arr;
}

inline std::string JsonParser::parse_string() {
    std::string str;
    next(); // skip '"'

    while (!eof()) {
        char const c = current();
        if (c == '"') {
            next();
            return str;
        }

        if (c == '\\') {
            next();
            switch (current()) {
            case '"':
                str += '"';
                break;
            case '\\':
                str += '\\';
                break;
            case '/':
                str += '/';
                break;
            case 'b':
                str += '\b';
                break;
            case 'f':
                str += '\f';
                break;
            case 'n':
                str += '\n';
                break;
            case 'r':
                str += '\r';
                break;
            case 't':
                str += '\t';
                break;
            default:
                throw JsonParseError("Invalid escape character", pos_);
            }
            next();
        } else {
            str += c;
            next();
        }
    }

    throw JsonParseError("Unterminated string", pos_);
}

inline bool JsonParser::parse_boolean() {
    size_t start = pos_;
    size_t rem = input_.size() - pos_;

    if (rem >= 4 && input_.substr(pos_, 4) == "true") {
        next(4);
        return true;
    }
    if (rem >= 5 && input_.substr(pos_, 5) == "false") {
        next(5);
        return false;
    }

    throw JsonParseError("Invalid boolean value", start);
}

inline std::nullptr_t JsonParser::parse_null() {
    size_t start = pos_;
    size_t rem = input_.size() - pos_;

    if (rem >= 4 && input_.substr(pos_, 4) == "null") {
        next(4);
        return nullptr;
    }

    throw JsonParseError("Invalid null value", start);
}

inline JsonValue JsonParser::parse_number() {
    size_t start = pos_;
    bool is_dbl = false;

    if (current() == '-') {
        next();
    }

    while (!eof()) {
        char c = current();
        if (std::isdigit(c)) {
            next();
        } else if (c == '.') {
            is_dbl = true;
            next();
        } else if (c == 'e' || c == 'E') {
            is_dbl = true;
            next();
            c = current();
            if (c == '+' || c == '-') {
                next();
            }
        } else {
            break;
        }
    }

    std::string_view const str = input_.substr(start, pos_ - start);
    if (!is_dbl) {
        int res{};
        auto [end, ec] =
            std::from_chars(str.data(), str.data() + str.size(), res);

        if (end != str.end() || ec == std::errc::invalid_argument) {
            throw JsonParseError("Invalid number format", start);
        }
        if (ec == std::errc()) {
            return res;
        }

        // out of range should fall to the double case
    }

    double res{};
    auto [end, ec] = std::from_chars(str.data(), str.data() + str.size(), res);

    if (ec != std::errc() || end != str.end()) {
        throw JsonParseError("Invalid number format", start);
    }

    return res;
}

inline std::ostream& operator<<(std::ostream& os, JsonValue const& jv) {
    std::visit(JsonValuePrinter{os}, jv);
    return os;
}

#endif // JSON_H
