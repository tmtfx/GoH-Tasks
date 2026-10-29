#pragma once
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cmath>

class JsonValue {
public:
    enum Type {
        JSON_NULL,
        JSON_BOOL,
        JSON_NUMBER,
        JSON_STRING,
        JSON_ARRAY,
        JSON_OBJECT
    };

    JsonValue() : fType(JSON_NULL), fBool(false), fNumber(0.0) {}
    explicit JsonValue(bool b) : fType(JSON_BOOL), fBool(b), fNumber(0.0) {}
    explicit JsonValue(double n) : fType(JSON_NUMBER), fBool(false), fNumber(n) {}
    explicit JsonValue(const std::string& s) : fType(JSON_STRING), fBool(false), fNumber(0.0), fString(s) {}
    explicit JsonValue(const char* s) : fType(JSON_STRING), fBool(false), fNumber(0.0), fString(s ? s : "") {}

    Type type() const { return fType; }
    bool isNull() const { return fType == JSON_NULL; }
    bool isBool() const { return fType == JSON_BOOL; }
    bool isNumber() const { return fType == JSON_NUMBER; }
    bool isString() const { return fType == JSON_STRING; }
    bool isArray() const { return fType == JSON_ARRAY; }
    bool isObject() const { return fType == JSON_OBJECT; }

    bool boolean() const { return fBool; }
    double num() const { return fNumber; }
    std::string str() const { return fType == JSON_STRING ? fString : ""; }

    bool has(const std::string& key) const {
        if (fType != JSON_OBJECT) return false;
        return fObject.find(key) != fObject.end();
    }

    size_t size() const {
        if (fType == JSON_ARRAY) return fArray.size();
        if (fType == JSON_OBJECT) return fObject.size();
        return 0;
    }

    const JsonValue& operator[](const std::string& key) const {
        static JsonValue sNull;
        if (fType != JSON_OBJECT) return sNull;
        auto it = fObject.find(key);
        if (it == fObject.end()) return sNull;
        return it->second;
    }

    const JsonValue& operator[](size_t idx) const {
        static JsonValue sNull;
        if (fType != JSON_ARRAY || idx >= fArray.size()) return sNull;
        return fArray[idx];
    }

    // For building objects/arrays
    void set(const std::string& key, const JsonValue& val) {
        fType = JSON_OBJECT;
        fObject[key] = val;
    }

    void append(const JsonValue& val) {
        fType = JSON_ARRAY;
        fArray.push_back(val);
    }

    // --- Static parse entry point ---
    static JsonValue parse(const std::string& json) {
        size_t pos = 0;
        JsonValue result = parseValue(json, pos);
        return result;
    }

    // --- Escape a string for use in JSON ---
    static std::string escape(const std::string& s) {
        std::string result;
        result.reserve(s.size() + 4);
        for (unsigned char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b";  break;
                case '\f': result += "\\f";  break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                        result += buf;
                    } else {
                        result += (char)c;
                    }
                    break;
            }
        }
        return result;
    }

    // --- Serialize to JSON string ---
    std::string serialize() const {
        switch (fType) {
            case JSON_NULL:   return "null";
            case JSON_BOOL:   return fBool ? "true" : "false";
            case JSON_NUMBER: {
                std::ostringstream oss;
                double intpart;
                if (std::modf(fNumber, &intpart) == 0.0 && fNumber >= -1e15 && fNumber <= 1e15)
                    oss << (long long)fNumber;
                else
                    oss << fNumber;
                return oss.str();
            }
            case JSON_STRING:
                return "\"" + escape(fString) + "\"";
            case JSON_ARRAY: {
                std::string s = "[";
                for (size_t i = 0; i < fArray.size(); ++i) {
                    if (i) s += ",";
                    s += fArray[i].serialize();
                }
                s += "]";
                return s;
            }
            case JSON_OBJECT: {
                std::string s = "{";
                bool first = true;
                for (auto& kv : fObject) {
                    if (!first) s += ",";
                    first = false;
                    s += "\"" + escape(kv.first) + "\":" + kv.second.serialize();
                }
                s += "}";
                return s;
            }
        }
        return "null";
    }

private:
    Type fType;
    bool fBool;
    double fNumber;
    std::string fString;
    std::vector<JsonValue> fArray;
    std::map<std::string, JsonValue> fObject;

    static void skipWhitespace(const std::string& s, size_t& pos) {
        while (pos < s.size() && std::isspace((unsigned char)s[pos]))
            ++pos;
    }

    static JsonValue parseValue(const std::string& s, size_t& pos) {
        skipWhitespace(s, pos);
        if (pos >= s.size())
            throw std::runtime_error("Unexpected end of JSON");

        char c = s[pos];
        if (c == '"')   return parseString(s, pos);
        if (c == '{')   return parseObject(s, pos);
        if (c == '[')   return parseArray(s, pos);
        if (c == 't')   return parseTrue(s, pos);
        if (c == 'f')   return parseFalse(s, pos);
        if (c == 'n')   return parseNull(s, pos);
        if (c == '-' || std::isdigit((unsigned char)c))
                        return parseNumber(s, pos);
        throw std::runtime_error(std::string("Unexpected character: ") + c);
    }

    static JsonValue parseString(const std::string& s, size_t& pos) {
        ++pos; // skip opening "
        std::string result;
        while (pos < s.size() && s[pos] != '"') {
            if (s[pos] == '\\') {
                ++pos;
                if (pos >= s.size())
                    throw std::runtime_error("Unexpected end in string escape");
                char esc = s[pos];
                switch (esc) {
                    case '"':  result += '"';  break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/';  break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        // Parse 4 hex digits
                        if (pos + 4 >= s.size())
                            throw std::runtime_error("Short \\u escape");
                        std::string hex = s.substr(pos + 1, 4);
                        unsigned long cp = std::stoul(hex, nullptr, 16);
                        pos += 4;
                        // Encode as UTF-8
                        if (cp < 0x80) {
                            result += (char)cp;
                        } else if (cp < 0x800) {
                            result += (char)(0xC0 | (cp >> 6));
                            result += (char)(0x80 | (cp & 0x3F));
                        } else {
                            result += (char)(0xE0 | (cp >> 12));
                            result += (char)(0x80 | ((cp >> 6) & 0x3F));
                            result += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default:
                        result += esc;
                        break;
                }
            } else {
                result += s[pos];
            }
            ++pos;
        }
        if (pos >= s.size())
            throw std::runtime_error("Unterminated string");
        ++pos; // skip closing "
        return JsonValue(result);
    }

    static JsonValue parseNumber(const std::string& s, size_t& pos) {
        size_t start = pos;
        if (s[pos] == '-') ++pos;
        while (pos < s.size() && std::isdigit((unsigned char)s[pos])) ++pos;
        if (pos < s.size() && s[pos] == '.') {
            ++pos;
            while (pos < s.size() && std::isdigit((unsigned char)s[pos])) ++pos;
        }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
            while (pos < s.size() && std::isdigit((unsigned char)s[pos])) ++pos;
        }
        std::string numStr = s.substr(start, pos - start);
        double val = std::stod(numStr);
        return JsonValue(val);
    }

    static JsonValue parseTrue(const std::string& s, size_t& pos) {
        if (s.substr(pos, 4) != "true")
            throw std::runtime_error("Expected 'true'");
        pos += 4;
        return JsonValue(true);
    }

    static JsonValue parseFalse(const std::string& s, size_t& pos) {
        if (s.substr(pos, 5) != "false")
            throw std::runtime_error("Expected 'false'");
        pos += 5;
        return JsonValue(false);
    }

    static JsonValue parseNull(const std::string& s, size_t& pos) {
        if (s.substr(pos, 4) != "null")
            throw std::runtime_error("Expected 'null'");
        pos += 4;
        JsonValue v;
        return v;
    }

    static JsonValue parseObject(const std::string& s, size_t& pos) {
        ++pos; // skip '{'
        JsonValue obj;
        obj.fType = JSON_OBJECT;
        skipWhitespace(s, pos);
        if (pos < s.size() && s[pos] == '}') {
            ++pos;
            return obj;
        }
        while (pos < s.size()) {
            skipWhitespace(s, pos);
            if (s[pos] != '"')
                throw std::runtime_error("Expected string key in object");
            JsonValue keyVal = parseString(s, pos);
            std::string key = keyVal.fString;
            skipWhitespace(s, pos);
            if (pos >= s.size() || s[pos] != ':')
                throw std::runtime_error("Expected ':' in object");
            ++pos;
            JsonValue val = parseValue(s, pos);
            obj.fObject[key] = val;
            skipWhitespace(s, pos);
            if (pos >= s.size())
                throw std::runtime_error("Unterminated object");
            if (s[pos] == '}') { ++pos; break; }
            if (s[pos] != ',')
                throw std::runtime_error("Expected ',' or '}' in object");
            ++pos;
        }
        return obj;
    }

    static JsonValue parseArray(const std::string& s, size_t& pos) {
        ++pos; // skip '['
        JsonValue arr;
        arr.fType = JSON_ARRAY;
        skipWhitespace(s, pos);
        if (pos < s.size() && s[pos] == ']') {
            ++pos;
            return arr;
        }
        while (pos < s.size()) {
            JsonValue val = parseValue(s, pos);
            arr.fArray.push_back(val);
            skipWhitespace(s, pos);
            if (pos >= s.size())
                throw std::runtime_error("Unterminated array");
            if (s[pos] == ']') { ++pos; break; }
            if (s[pos] != ',')
                throw std::runtime_error("Expected ',' or ']' in array");
            ++pos;
        }
        return arr;
    }
};
