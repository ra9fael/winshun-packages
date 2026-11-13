// json_lite.h — a minimal, allocation-light JSON reader.
//
// Scoop's only structured output is `scoop export`, whose shape is fixed and
// simple. Rather than pull a dependency into a plugin that must stay a single
// small exe, this reader handles exactly the JSON that appears there: objects,
// arrays, strings (with escapes), numbers, true/false/null. It reports failure
// instead of guessing, which is the same contract the WinGet parser has.
//
// Pure text logic: no OS headers.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pkg::jsonlite {

struct Value;
using ValuePtr = std::shared_ptr<Value>;

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<ValuePtr> array;
    std::map<std::string, ValuePtr> object;

    bool isNull() const { return kind == Kind::Null; }
    bool isString() const { return kind == Kind::String; }
    bool isNumber() const { return kind == Kind::Number; }
    bool isArray() const { return kind == Kind::Array; }
    bool isObject() const { return kind == Kind::Object; }

    // Member lookup; returns nullptr when absent or when this is not an object.
    ValuePtr get(const std::string& key) const;
    // String value of a member, or "" when absent/not a string.
    std::string stringAt(const std::string& key) const;
};

// Parses `text`. Returns nullptr and fills `error` on any syntax problem.
ValuePtr parse(const std::string& text, std::string& error);

} // namespace pkg::jsonlite
