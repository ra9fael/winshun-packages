// pins_cache.cpp — see pins_cache.h.
#include "pins_cache.h"

#include <cstdio>

#include "json_lite.h"

namespace pkg::pins_cache {
namespace {

std::string escape(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 8);
    for (const char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[7];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                out += buffer;
            } else {
                out.push_back(c);
            }
        }
    }
    return out;
}

std::string quoted(const std::string& value) { return "\"" + escape(value) + "\""; }

} // namespace

std::string serialize(const Store& store)
{
    std::string out = "{\"pinsVersion\":1,\"observedAt\":" + quoted(store.observedAt) + ",\"entries\":[";
    bool first = true;
    for (const auto& [id, policy] : store.entries) {
        if (!first)
            out += ",";
        first = false;
        out += "{\"id\":" + quoted(id) + ",\"policy\":" + quoted(policy) + "}";
    }
    out += "]}";
    return out;
}

bool parse(const std::string& text, Store& out, std::string& error)
{
    out = Store{};
    const auto root = jsonlite::parse(text, error);
    if (!root || !root->isObject()) {
        if (error.empty())
            error = "the pin cache file is not a JSON object";
        return false;
    }
    const auto version = root->get("pinsVersion");
    if (!version || !version->isNumber() || static_cast<int>(version->number) != 1) {
        error = "the pin cache file was written by a different version";
        return false;
    }
    out.observedAt = root->stringAt("observedAt");
    const auto entries = root->get("entries");
    if (!entries || !entries->isArray()) {
        error = "the pin cache file has no entries array";
        return false;
    }
    for (const auto& value : entries->array) {
        if (!value || !value->isObject())
            continue;
        const std::string id = value->stringAt("id");
        if (id.empty())
            continue;   // an entry without an id cannot block anything
        out.entries.emplace_back(id, value->stringAt("policy"));
    }
    return true;
}

} // namespace pkg::pins_cache
