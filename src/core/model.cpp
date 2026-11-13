// model.cpp — see model.h.
#include "model.h"

#include <algorithm>
#include <cctype>

#include "version.h"

namespace pkg {
namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool matches(const Package& p, const std::string& needle)
{
    if (needle.empty())
        return true;
    const std::string n = lower(needle);
    return lower(p.name).find(n) != std::string::npos || lower(p.key.id).find(n) != std::string::npos;
}

bool managerOk(const Package& p, Manager onlyManager)
{
    return onlyManager == Manager::Unknown || p.key.manager == onlyManager;
}

} // namespace

const char* backendStateId(BackendState s)
{
    switch (s) {
    case BackendState::Missing: return "missing";
    case BackendState::Denied: return "denied";
    case BackendState::Unsupported: return "unsupported";
    case BackendState::Unavailable: return "unavailable";
    case BackendState::Ready: return "ready";
    case BackendState::Unknown: break;
    }
    return "unknown";
}

const char* evidenceId(Evidence e)
{
    switch (e) {
    case Evidence::L1: return "L1";
    case Evidence::L2: return "L2";
    case Evidence::L3: return "L3";
    case Evidence::L4: return "L4";
    case Evidence::None: break;
    }
    return "none";
}

const char* tabId(Tab t)
{
    switch (t) {
    case Tab::Installed: return "installed";
    case Tab::Discover: return "discover";
    case Tab::Updates: return "updates";
    case Tab::Tasks: return "tasks";
    }
    return "installed";
}

std::vector<Package> rowsForTab(const Snapshot& snapshot, Tab tab, const std::string& filter,
                                Manager onlyManager)
{
    std::vector<Package> out;
    for (const auto& p : snapshot.packages) {
        if (!managerOk(p, onlyManager))
            continue;
        switch (tab) {
        case Tab::Installed:
            // Anything the backend enumerated stays visible; provenance says how
            // much the row can be trusted, and the UI never shows observed as
            // managed.
            if (p.installedVersion.empty())
                continue;
            break;
        case Tab::Discover:
            // A search over candidates, not a filter of the installed list.
            break;
        case Tab::Updates:
            if (p.installedVersion.empty() || p.availableVersion.empty())
                continue;
            // Only real evidence of a newer version counts; an unknown target
            // version is not an update.
            if (!isNewer(p.installedVersion, p.availableVersion))
                continue;
            break;
        case Tab::Tasks:
            return {};
        }
        if (!matches(p, filter))
            continue;
        out.push_back(p);
    }
    return out;
}

std::vector<std::size_t> duplicateHints(const Snapshot& snapshot, std::size_t index)
{
    std::vector<std::size_t> out;
    if (index >= snapshot.packages.size())
        return out;
    const auto& base = snapshot.packages[index];
    for (std::size_t i = 0; i < snapshot.packages.size(); ++i) {
        if (i == index)
            continue;
        if (possibleDuplicate(base.key, snapshot.packages[i].key).has_value())
            out.push_back(i);
    }
    return out;
}

} // namespace pkg
