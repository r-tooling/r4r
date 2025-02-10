#ifndef JSON_H
#define JSON_H

#include "common.h"
#include <cctype>
#include <charconv>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

class JsonParseError : public std::runtime_error {
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

class JsonParser {
  public:
    static JsonValue parse(std::string const& input) {
        JsonParser parser{input};
        return parser.parse();
    }

  private:
    explicit JsonParser(std::string const& input) : input_{input} {}

    JsonValue parse() {
        JsonValue value = parse_value();
        if (!eof()) {
            throw JsonParseError(
                STR("Unexpected reminder after JSON value parsed: "
                    << input_.substr(pos_)),
                pos_);
        }
        return value;
    }

    [[nodiscard]] bool eof() const { return pos_ >= input_.size(); }

    void next(size_t n = 1) {
        for (size_t i = 0; i < n; i++) {
            if (eof()) {
                return;
            }
            ++pos_;
        }
    }

    void skip_whitespace() {
        while (!eof() && std::isspace(input_[pos_])) {
            next();
        }
    }

    [[nodiscard]] char current() const { return !eof() ? input_[pos_] : '\0'; }

    JsonValue parse_value() {
        skip_whitespace();
        char const c = current();

        switch (c) {
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
            throw JsonParseError(STR("Unexpected character '" << c << "'"),
                                 pos_);
        }
    }

    JsonObject parse_object() {
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

    JsonArray parse_array() {
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

    std::string parse_string() {
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
                char const esc = current();
                switch (esc) {
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

    bool parse_boolean() {
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

    std::nullptr_t parse_null() {
        size_t start = pos_;
        size_t rem = input_.size() - pos_;

        if (rem >= 4 && input_.substr(pos_, 4) == "null") {
            next(4);
            return nullptr;
        }

        throw JsonParseError("Invalid null value", start);
    }

    JsonValue parse_number() {
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
        auto [end, ec] =
            std::from_chars(str.data(), str.data() + str.size(), res);

        if (ec != std::errc() || end != str.end()) {
            throw JsonParseError("Invalid number format", start);
        }

        return res;
    }

    std::string_view input_;
    size_t pos_{};
};

#endif // JSON_H
