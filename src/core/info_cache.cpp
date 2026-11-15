// info_cache.cpp — see info_cache.h.
#include "info_cache.h"

#include <cstdio>

#include "json_lite.h"

namespace pkg::info_cache {
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
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04X", static_cast<unsigned char>(c));
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    return out;
}

std::string quoted(const std::string& value) { return "\"" + escape(value) + "\""; }

std::string lowerAscii(std::string s)
{
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

} // namespace

bool find(const Store& store, const std::string& id, Entry& out)
{
    if (id.empty())
        return false;
    const std::string wanted = lowerAscii(id);
    for (const auto& entry : store.entries) {
        if (lowerAscii(entry.id) == wanted) {
            out = entry;
            return true;
        }
    }
    return false;
}

bool stillMatches(const Entry& entry, const std::string& installedVersion)
{
    if (entry.version.empty() || installedVersion.empty())
        return false;
    return lowerAscii(entry.version) == lowerAscii(installedVersion);
}

void upsert(Store& store, Entry&& entry, std::size_t cap){
    if (entry.id.empty())
        return;
    drop(store, entry.id);
    store.entries.push_back(std::move(entry));
    while (store.entries.size() > cap)
        store.entries.erase(store.entries.begin());   // oldest first
}

bool drop(Store& store, const std::string& id)
{
    if (id.empty())
        return false;
    const std::string wanted = lowerAscii(id);
    for (auto it = store.entries.begin(); it != store.entries.end(); ++it) {
        if (lowerAscii(it->id) == wanted) {
            store.entries.erase(it);
            return true;
        }
    }
    return false;
}

std::string serialize(const Store& store)
{
    std::string out = "{\"infoVersion\":1,\"entries\":[";
    bool first = true;
    for (const auto& entry : store.entries) {
        if (!first)
            out += ",";
        first = false;
        out += "{\"id\":" + quoted(entry.id);
        out += ",\"observedAt\":" + quoted(entry.observedAt);
        out += ",\"version\":" + quoted(entry.version);
        out += ",\"fields\":{";
        bool firstField = true;
        for (const auto& [key, value] : entry.fields) {
            if (!firstField)
                out += ",";
            firstField = false;
            out += quoted(key) + ":" + quoted(value);
        }
        out += "}}";
    }
    out += "]}";
    return out;
}

bool parse(const std::string& text, Store& out, std::string& error)
{
    const auto root = jsonlite::parse(text, error);
    if (!root || !root->isObject()) {
        if (error.empty())
            error = "the info cache file is not a JSON object";
        return false;
    }
    const auto version = root->get("infoVersion");
    if (!version || version->kind != jsonlite::Value::Kind::Number) {
        error = "the info cache file has no infoVersion";
        return false;
    }
    if (static_cast<int>(version->number) != 1) {
        error = "the info cache file was written by a different version";
        return false;
    }
    const auto entries = root->get("entries");
    if (!entries || !entries->isArray()) {
        error = "the info cache file has no entries array";
        return false;
    }
    out = Store{};
    out.entries.reserve(entries->array.size());
    for (const auto& value : entries->array) {
        if (!value || !value->isObject())
            continue;
        Entry entry;
        entry.id = value->stringAt("id");
        if (entry.id.empty())
            continue;   // an entry without an identity can never be looked up
        entry.observedAt = value->stringAt("observedAt");
        // Optional: entries written before this field existed simply fall back
        // to the time-based TTL.
        entry.version = value->stringAt("version");
        if (const auto fields = value->get("fields"); fields && fields->isObject()) {
            for (const auto& [key, v] : fields->object) {
                if (v && v->isString())
                    entry.fields[key] = v->text;
            }
        }
        out.entries.push_back(std::move(entry));
    }
    return true;
}

} // namespace pkg::info_cache
