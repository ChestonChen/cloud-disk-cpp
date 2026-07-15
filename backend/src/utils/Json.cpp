#include "utils/Json.h"

#include <cctype>
#include <sstream>

namespace cloud_disk {
namespace {

// 跳过 JSON 文本中的空白字符。
void skipSpaces(const std::string& text, std::size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

// 解析 JSON 字符串值，支持当前接口需要的基础转义字符。
std::string parseString(const std::string& text, std::size_t& pos) {
    std::string out;
    if (pos >= text.size() || text[pos] != '"') {
        return out;
    }
    ++pos;
    while (pos < text.size()) {
        char ch = text[pos++];
        if (ch == '"') {
            break;
        }
        if (ch == '\\' && pos < text.size()) {
            char next = text[pos++];
            switch (next) {
            case '"':
            case '\\':
            case '/':
                out.push_back(next);
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 't':
                out.push_back('\t');
                break;
            default:
                out.push_back(next);
                break;
            }
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

// 解析数字、布尔值等非字符串简单值。
std::string parsePrimitive(const std::string& text, std::size_t& pos) {
    std::size_t start = pos;
    while (pos < text.size() && text[pos] != ',' && text[pos] != '}') {
        ++pos;
    }
    std::size_t end = pos;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
}

} // namespace

// 解析一层 JSON 对象，当前项目的请求体都保持为简单键值结构。
JsonObject parseFlatJsonObject(const std::string& body) {
    JsonObject result;
    std::size_t pos = 0;
    skipSpaces(body, pos);
    if (pos >= body.size() || body[pos] != '{') {
        return result;
    }
    ++pos;

    while (pos < body.size()) {
        skipSpaces(body, pos);
        if (pos < body.size() && body[pos] == '}') {
            break;
        }
        std::string key = parseString(body, pos);
        skipSpaces(body, pos);
        if (pos >= body.size() || body[pos] != ':') {
            break;
        }
        ++pos;
        skipSpaces(body, pos);
        std::string value = (pos < body.size() && body[pos] == '"') ? parseString(body, pos)
                                                                    : parsePrimitive(body, pos);
        result[key] = value;
        skipSpaces(body, pos);
        if (pos < body.size() && body[pos] == ',') {
            ++pos;
        }
    }
    return result;
}

// 转义 JSON 字符串中的特殊字符。
std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    return out.str();
}

// 把键值对拼成 JSON 对象。
std::string jsonObject(const JsonObject& fields) {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto& [key, value] : fields) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "\"" << jsonEscape(key) << "\":\"" << jsonEscape(value) << "\"";
    }
    out << "}";
    return out.str();
}

// 把多个 JSON 片段拼成数组。
std::string jsonArray(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << values[i];
    }
    out << "]";
    return out.str();
}

// 包装统一成功响应。
std::string okJson(const std::string& dataJson) {
    return "{\"code\":0,\"message\":\"ok\",\"data\":" + dataJson + "}";
}

// 包装统一错误响应。
std::string errorJson(int code, const std::string& message) {
    return "{\"code\":" + std::to_string(code) + ",\"message\":\"" + jsonEscape(message)
           + "\",\"data\":{}}";
}

} // namespace cloud_disk

