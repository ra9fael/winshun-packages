// json_lite.cpp — see json_lite.h.
#include "json_lite.h"

#include <cstdlib>

namespace pkg::jsonlite {
namespace {

class Parser {
public:
    Parser(const std::string& text, std::string& error) : m_text(text), m_error(error) {}

    ValuePtr run()
    {
        skipSpace();
        auto value = parseValue(0);
        if (!value)
            return nullptr;
        skipSpace();
        if (m_at != m_text.size()) {
            fail("trailing data after the top-level value");
            return nullptr;
        }
        return value;
    }

private:
    const std::string& m_text;
    std::string& m_error;
    std::size_t m_at = 0;

    bool fail(const std::string& why)
    {
        if (m_error.empty())
            m_error = why + " at offset " + std::to_string(m_at);
        return false;
    }

    void skipSpace()
    {
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                ++m_at;
            else
                break;
        }
    }

    bool literal(const char* word)
    {
        const std::string w(word);
        if (m_text.compare(m_at, w.size(), w) != 0)
            return false;
        m_at += w.size();
        return true;
    }

    ValuePtr parseValue(int depth)
    {
        // A depth cap: a hostile file must not blow the stack.
        if (depth > 32) {
            fail("nesting is too deep");
            return nullptr;
        }
        if (m_at >= m_text.size()) {
            fail("unexpected end of input");
            return nullptr;
        }
        const char c = m_text[m_at];
        if (c == '{')
            return parseObject(depth + 1);
        if (c == '[')
            return parseArray(depth + 1);
        if (c == '"') {
            auto value = std::make_shared<Value>();
            value->kind = Value::Kind::String;
            if (!parseString(value->text))
                return nullptr;
            return value;
        }
        if (literal("true")) {
            auto value = std::make_shared<Value>();
            value->kind = Value::Kind::Bool;
            value->boolean = true;
            return value;
        }
        if (literal("false")) {
            auto value = std::make_shared<Value>();
            value->kind = Value::Kind::Bool;
            return value;
        }
        if (literal("null"))
            return std::make_shared<Value>();
        return parseNumber();
    }

    ValuePtr parseNumber()
    {
        const std::size_t start = m_at;
        if (m_at < m_text.size() && (m_text[m_at] == '-' || m_text[m_at] == '+'))
            ++m_at;
        bool digits = false;
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if ((c >= '0' && c <= '9')) {
                digits = true;
                ++m_at;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                ++m_at;
            } else {
                break;
            }
        }
        if (!digits) {
            fail("expected a value");
            return nullptr;
        }
        auto value = std::make_shared<Value>();
        value->kind = Value::Kind::Number;
        value->text = m_text.substr(start, m_at - start);
        value->number = std::strtod(value->text.c_str(), nullptr);
        return value;
    }

    bool parseString(std::string& out)
    {
        if (m_text[m_at] != '"') {
            fail("expected a string");
            return false;
        }
        ++m_at;
        out.clear();
        while (m_at < m_text.size()) {
            const char c = m_text[m_at++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (m_at >= m_text.size()) {
                fail("unterminated escape");
                return false;
            }
            const char e = m_text[m_at++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (m_at + 4 > m_text.size()) {
                    fail("truncated \\u escape");
                    return false;
                }
                const std::string hex = m_text.substr(m_at, 4);
                m_at += 4;
                const unsigned code = static_cast<unsigned>(std::strtoul(hex.c_str(), nullptr, 16));
                // UTF-8 encode. Surrogate pairs are not needed for Scoop's output,
                // so a lone surrogate becomes the replacement character rather
                // than silently producing broken text.
                if (code < 0x80) {
                    out.push_back(static_cast<char>(code));
                } else if (code < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else if (code >= 0xD800 && code <= 0xDFFF) {
                    out.append("\xEF\xBF\xBD");
                } else {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                break;
            }
            default:
                fail("unknown escape");
                return false;
            }
        }
        fail("unterminated string");
        return false;
    }

    ValuePtr parseObject(int depth)
    {
        auto value = std::make_shared<Value>();
        value->kind = Value::Kind::Object;
        ++m_at;   // '{'
        skipSpace();
        if (m_at < m_text.size() && m_text[m_at] == '}') {
            ++m_at;
            return value;
        }
        while (true) {
            skipSpace();
            std::string key;
            if (!parseString(key))
                return nullptr;
            skipSpace();
            if (m_at >= m_text.size() || m_text[m_at] != ':') {
                fail("expected ':' after a key");
                return nullptr;
            }
            ++m_at;
            skipSpace();
            auto member = parseValue(depth);
            if (!member)
                return nullptr;
            value->object[key] = std::move(member);
            skipSpace();
            if (m_at < m_text.size() && m_text[m_at] == ',') {
                ++m_at;
                continue;
            }
            if (m_at < m_text.size() && m_text[m_at] == '}') {
                ++m_at;
                return value;
            }
            fail("expected ',' or '}' in an object");
            return nullptr;
        }
    }

    ValuePtr parseArray(int depth)
    {
        auto value = std::make_shared<Value>();
        value->kind = Value::Kind::Array;
        ++m_at;   // '['
        skipSpace();
        if (m_at < m_text.size() && m_text[m_at] == ']') {
            ++m_at;
            return value;
        }
        while (true) {
            skipSpace();
            auto element = parseValue(depth);
            if (!element)
                return nullptr;
            value->array.push_back(std::move(element));
            skipSpace();
            if (m_at < m_text.size() && m_text[m_at] == ',') {
                ++m_at;
                continue;
            }
            if (m_at < m_text.size() && m_text[m_at] == ']') {
                ++m_at;
                return value;
            }
            fail("expected ',' or ']' in an array");
            return nullptr;
        }
    }
};

} // namespace

ValuePtr Value::get(const std::string& key) const
{
    if (kind != Kind::Object)
        return nullptr;
    const auto it = object.find(key);
    return it == object.end() ? nullptr : it->second;
}

std::string Value::stringAt(const std::string& key) const
{
    const auto member = get(key);
    return member && member->isString() ? member->text : std::string();
}

ValuePtr parse(const std::string& text, std::string& error)
{
    error.clear();
    // A UTF-8 BOM arrives when a fixture was written by a Windows tool.
    std::string body = text;
    if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
        static_cast<unsigned char>(body[1]) == 0xBB && static_cast<unsigned char>(body[2]) == 0xBF)
        body.erase(0, 3);
    Parser parser(body, error);
    return parser.run();
}

} // namespace pkg::jsonlite
