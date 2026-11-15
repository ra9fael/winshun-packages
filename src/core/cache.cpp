// cache.cpp — see cache.h.
#include "cache.h"

#include <cstdio>

#include "json_lite.h"

namespace pkg::cache {
namespace {

// Minimal JSON string escaping: the cache holds package ids, versions, sources
// and detail strings, so quotes, backslashes and control characters are the only
// things that need handling. Non-ASCII is passed through as UTF-8.
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

std::string backendId(BackendState state) { return quoted(backendStateId(state)); }

BackendState backendFromId(const std::string& id)
{
    if (id == "missing") return BackendState::Missing;
    if (id == "denied") return BackendState::Denied;
    if (id == "unsupported") return BackendState::Unsupported;
    if (id == "unavailable") return BackendState::Unavailable;
    if (id == "ready") return BackendState::Ready;
    return BackendState::Unknown;
}

// The snapshot fields worth keeping. `stale` is not: staleness is derived from
// observedAt against the refresh interval when the cache is read back, so a
// value stored here would be a second, contradicting source of truth.
std::string packageJson(const Package& p)
{
    std::string out = "{";
    out += "\"manager\":" + quoted(managerId(p.key.manager));
    out += ",\"source\":" + quoted(p.key.source);
    out += ",\"id\":" + quoted(p.key.id);
    out += ",\"scope\":" + quoted(scopeId(p.key.scope));
    out += ",\"name\":" + quoted(p.name);
    out += ",\"installed\":" + quoted(p.installedVersion);
    out += ",\"available\":" + quoted(p.availableVersion);
    out += ",\"availableKind\":" + quoted(p.availableKind);
    out += ",\"arch\":" + quoted(p.arch);
    out += ",\"provenance\":" + quoted(provenanceId(p.provenance));
    out += ",\"holdPolicy\":" + quoted(p.holdPolicy);
    out += ",\"dependencies\":" + quoted(p.dependencies);
    out += ",\"material\":" + quoted(p.material);
    out += ",\"limits\":" + quoted(p.limits);
    out += std::string(",\"canInstall\":") + (p.canInstall ? "true" : "false");
    out += std::string(",\"canUninstall\":") + (p.canUninstall ? "true" : "false");
    out += std::string(",\"canUpgrade\":") + (p.canUpgrade ? "true" : "false");
    out += ",\"metadataAt\":" + quoted(p.metadataAt);
    out += "}";
    return out;
}

Package packageFromJson(const jsonlite::Value& v)
{
    Package p;
    p.key.manager = managerFromId(v.stringAt("manager"));
    p.key.source = v.stringAt("source");
    p.key.id = v.stringAt("id");
    p.key.scope = scopeFromId(v.stringAt("scope"));
    p.name = v.stringAt("name");
    p.installedVersion = v.stringAt("installed");
    p.availableVersion = v.stringAt("available");
    p.availableKind = v.stringAt("availableKind");
    p.arch = v.stringAt("arch");
    p.provenance = provenanceFromId(v.stringAt("provenance"));
    p.holdPolicy = v.stringAt("holdPolicy");
    p.dependencies = v.stringAt("dependencies");
    p.material = v.stringAt("material");
    p.limits = v.stringAt("limits");
    const auto flag = [&v](const char* key) {
        const auto value = v.get(key);
        return value && value->kind == jsonlite::Value::Kind::Bool && value->boolean;
    };
    p.canInstall = flag("canInstall");
    p.canUninstall = flag("canUninstall");
    p.canUpgrade = flag("canUpgrade");
    p.metadataAt = v.stringAt("metadataAt");
    return p;
}

} // namespace

std::string serialize(const Snapshot& snapshot, const Snapshot* updates)
{
    std::string out = "{\"cacheVersion\":1";
    out += ",\"observedAt\":" + quoted(snapshot.observedAt);
    out += ",\"scanScope\":" + quoted(snapshot.scanScope);
    out += ",\"winget\":" + backendId(snapshot.winget);
    out += ",\"wingetDetail\":" + quoted(snapshot.wingetDetail);
    out += ",\"scoop\":" + backendId(snapshot.scoop);
    out += ",\"scoopDetail\":" + quoted(snapshot.scoopDetail);
    // Per-backend stamps: a partial refresh updates only one of them.
    out += ",\"wingetAt\":" + quoted(snapshot.wingetObservedAt);
    out += ",\"scoopAt\":" + quoted(snapshot.scoopObservedAt);
    out += ",\"packages\":[";
    bool first = true;
    for (const auto& p : snapshot.packages) {
        if (!first)
            out += ",";
        first = false;
        out += packageJson(p);
    }
    out += "]";
    // The update listing is cached alongside the inventory: it costs a `winget
    // list --upgrade-available` (tens of seconds) and a bucket scan, so without it
    // every restart showed an empty Updates tab that had to be waited for again.
    if (updates) {
        out += ",\"updates\":[";
        first = true;
        for (const auto& p : updates->packages) {
            if (!first)
                out += ",";
            first = false;
            out += packageJson(p);
        }
        out += "]";
        out += ",\"updatesAt\":" + quoted(updates->observedAt);
    }
    out += "}";
    return out;
}

bool parse(const std::string& text, Snapshot& out, std::string& error, Snapshot* updates)
{
    const auto root = jsonlite::parse(text, error);
    if (!root || !root->isObject()) {
        if (error.empty())
            error = "the cache file is not a JSON object";
        return false;
    }
    // A different or future format: refuse rather than half-read it, so a stale
    // schema can never produce rows this build would misinterpret.
    const auto version = root->get("cacheVersion");
    if (!version || version->kind != jsonlite::Value::Kind::Number) {
        error = "the cache file has no cacheVersion";
        return false;
    }
    if (static_cast<int>(version->number) != 1) {
        error = "the cache file was written by a different version";
        return false;
    }

    out = Snapshot{};
    out.observedAt = root->stringAt("observedAt");
    out.scanScope = root->stringAt("scanScope");
    out.winget = backendFromId(root->stringAt("winget"));
    out.wingetDetail = root->stringAt("wingetDetail");
    out.scoop = backendFromId(root->stringAt("scoop"));
    out.scoopDetail = root->stringAt("scoopDetail");
    // Written by 0.4.12 and later. A file from an older build has neither, so
    // both fall back to the whole-snapshot time and behave as before.
    out.wingetObservedAt = root->stringAt("wingetAt");
    out.scoopObservedAt = root->stringAt("scoopAt");
    if (out.wingetObservedAt.empty())
        out.wingetObservedAt = out.observedAt;
    if (out.scoopObservedAt.empty())
        out.scoopObservedAt = out.observedAt;

    const auto packages = root->get("packages");
    if (!packages || !packages->isArray()) {
        error = "the cache file has no packages array";
        return false;
    }
    out.packages.reserve(packages->array.size());
    for (const auto& entry : packages->array) {
        if (entry && entry->isObject())
            out.packages.push_back(packageFromJson(*entry));
    }

    if (updates) {
        *updates = Snapshot{};
        updates->scanScope = "updates";
        updates->winget = out.winget;
        updates->wingetDetail = out.wingetDetail;
        updates->scoop = out.scoop;
        updates->scoopDetail = out.scoopDetail;
        updates->observedAt = root->stringAt("updatesAt");
        updates->wingetObservedAt = out.wingetObservedAt;
        updates->scoopObservedAt = out.scoopObservedAt;
        // A cache written before updates were stored has no array at all; that is
        // not an error, it just means the Updates tab starts empty until the scan.
        const auto list = root->get("updates");
        if (list && list->isArray()) {
            updates->packages.reserve(list->array.size());
            for (const auto& entry : list->array) {
                if (entry && entry->isObject())
                    updates->packages.push_back(packageFromJson(*entry));
            }
        }
    }
    return true;
}

bool worthWriting(const Snapshot& snapshot)
{
    // Nothing was scanned, or every backend failed: keeping the previous cache
    // is better than replacing it with an empty one the next launch would show
    // as "you have no packages".
    if (snapshot.observedAt.empty())
        return false;
    if (snapshot.winget != BackendState::Ready && snapshot.scoop != BackendState::Ready)
        return false;
    return true;
}

} // namespace pkg::cache
