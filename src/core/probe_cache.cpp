// probe_cache.cpp — see probe_cache.h.
#include "probe_cache.h"

#include <cstdio>

#include "json_lite.h"

namespace pkg::probe_cache {
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

BackendState stateFromId(const std::string& id)
{
    for (const BackendState state : {BackendState::Unknown, BackendState::Missing, BackendState::Denied,
                                     BackendState::Unsupported, BackendState::Unavailable, BackendState::Ready}) {
        if (id == backendStateId(state))
            return state;
    }
    return BackendState::Unknown;
}

void writeRecord(std::string& out, const char* name, const Record& record)
{
    out += ",\"";
    out += name;
    out += "\":{";
    out += "\"state\":\"" + escape(backendStateId(record.state)) + "\",";
    out += "\"detail\":\"" + escape(record.detail) + "\",";
    out += "\"version\":\"" + escape(record.version) + "\",";
    out += "\"program\":\"" + escape(record.program) + "\",";
    out += "\"stamp\":\"" + escape(record.stamp) + "\",";
    out += "\"observedAt\":\"" + escape(record.observedAt) + "\"}";
}

bool readRecord(const jsonlite::ValuePtr& node, Record& out)
{
    if (!node || !node->isObject())
        return false;
    out.state = stateFromId(node->stringAt("state"));
    out.detail = node->stringAt("detail");
    out.version = node->stringAt("version");
    out.program = node->stringAt("program");
    out.stamp = node->stringAt("stamp");
    out.observedAt = node->stringAt("observedAt");
    // A record without a program or a stamp cannot be matched against a file, so
    // it is not usable and is treated as absent.
    return !out.program.empty() && !out.stamp.empty();
}

} // namespace

bool reusable(const Record& record, const std::string& currentStamp)
{
    if (record.stamp.empty() || currentStamp.empty())
        return false;
    if (record.stamp != currentStamp)
        return false;
    // Only a verdict that says "this backend answers" is worth reusing. Missing,
    // Denied and Unknown must be re-probed: they may be a transient condition
    // (the earlier launch had no `processes` permission, a lock, a broken env).
    return record.state == BackendState::Ready || record.state == BackendState::Unsupported;
}

std::string serialize(const Store& store)
{
    std::string out = "{\"probeVersion\":1";
    if (store.hasWinget)
        writeRecord(out, "winget", store.winget);
    if (store.hasScoop)
        writeRecord(out, "scoop", store.scoop);
    out += "}\n";
    return out;
}

bool parse(const std::string& text, Store& out, std::string& error)
{
    out = Store{};
    const auto root = jsonlite::parse(text, error);
    if (!root || !root->isObject()) {
        if (error.empty())
            error = "probe cache is not a JSON object";
        return false;
    }
    const auto version = root->get("probeVersion");
    if (!version || !version->isNumber() || static_cast<int>(version->number) != 1) {
        error = "probe cache was written by a different version";
        return false;
    }
    out.hasWinget = readRecord(root->get("winget"), out.winget);
    out.hasScoop = readRecord(root->get("scoop"), out.scoop);
    return out.hasWinget || out.hasScoop;
}

} // namespace pkg::probe_cache
