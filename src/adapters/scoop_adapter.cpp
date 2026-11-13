// scoop_adapter.cpp — see scoop_adapter.h.
#include "scoop_adapter.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "../core/json_lite.h"
#include "../core/version.h"
#include "../platform/process.h"

#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
#endif

namespace pkg {
namespace {

std::string nowText()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string trim(const std::string& s)
{
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    return lines;
}

} // namespace

ScoopRootChoice resolveScoopRoot(const std::vector<std::pair<std::string, std::string>>& env,
                                 const std::string& pathShim)
{
    // A caller-supplied environment (not this process's) so the order is testable
    // and so the reason can be reported. An explicit SCOOP always wins: Scoop
    // itself reads it, so honouring it cannot pick a different installation than
    // the one the user is using.
    const auto get = [&env](const char* name) {
        for (const auto& [k, v] : env)
            if (k == name)
                return std::string(v);
        return std::string();
    };

    ScoopRootChoice choice;
    if (const std::string scoop = get("SCOOP"); !scoop.empty()) {
        choice.root = scoop;
        choice.source = "SCOOP";
        return choice;
    }
    if (const std::string profile = get("USERPROFILE"); !profile.empty()) {
        choice.root = profile + "\\scoop";
        choice.source = "profile";
        return choice;
    }
    // No profile: `scoop.ps1` on PATH lives at <root>\shims\, so its parent of
    // parent is the root. This is the only fallback that needs no profile and
    // no environment variable at all.
    if (!pathShim.empty()) {
        std::string shim = pathShim;
        std::replace(shim.begin(), shim.end(), '/', '\\');
        const auto shimsDir = shim.rfind("\\shims\\");
        if (shimsDir != std::string::npos) {
            choice.root = shim.substr(0, shimsDir);
            choice.source = "path-shim";
            return choice;
        }
    }
    if (const std::string programData = get("ProgramData"); !programData.empty()) {
        choice.root = programData + "\\scoop";
        choice.source = "common";
        return choice;
    }
    choice.source = "none";
    choice.detail = "no SCOOP, no user profile, no scoop.ps1 on PATH and no ProgramData";
    return choice;
}

#ifdef _WIN32

namespace {

std::string env(const wchar_t* name)
{
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD n = ::GetEnvironmentVariableW(name, buffer, MAX_PATH * 2);
    return n > 0 && n < MAX_PATH * 2 ? platform::toUtf8(std::wstring(buffer, n)) : std::string();
}

bool fileExists(const std::string& path)
{
    return !path.empty() && ::GetFileAttributesW(platform::toWide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

// This process's environment as the (name, value) list resolveScoopRoot expects.
std::vector<std::pair<std::string, std::string>> environmentPairs()
{
    std::vector<std::pair<std::string, std::string>> pairs;
    wchar_t* block = ::GetEnvironmentStringsW();
    if (!block)
        return pairs;
    for (const wchar_t* p = block; *p; p += std::wcslen(p) + 1) {
        const std::wstring entry(p);
        const auto eq = entry.find(L'=');
        if (eq == std::wstring::npos || eq == 0)
            continue;
        pairs.emplace_back(platform::toUtf8(entry.substr(0, eq)), platform::toUtf8(entry.substr(eq + 1)));
    }
    ::FreeEnvironmentStringsW(block);
    return pairs;
}

// The real profile folder for this process's user, from the registry, NOT from
// %USERPROFILE%. An elevated plugin is started with a system environment
// (WinShun's runner gives it CreateEnvironmentBlock without a user token and
// then fills USERPROFILE from FOLDERID_Profile, which is C:\Users\Default when no
// user profile is loaded), so trusting that variable would send the search to a
// folder that does not exist.
std::string profileFromRegistry()
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};

    std::string profile;
    DWORD size = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size > 0) {
        std::vector<unsigned char> buffer(size);
        if (::GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
            const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
            LPWSTR sidString = nullptr;
            if (::ConvertSidToStringSidW(const_cast<PSID>(user->User.Sid), &sidString) && sidString) {
                const std::wstring key =
                    std::wstring(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\") + sidString;
                ::LocalFree(sidString);

                HKEY handle = nullptr;
                if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &handle) ==
                    ERROR_SUCCESS) {
                    wchar_t value[32767] = {};
                    DWORD bytes = sizeof(value) - sizeof(wchar_t);
                    DWORD type = 0;
                    if (::RegQueryValueExW(handle, L"ProfileImagePath", nullptr, &type,
                                           reinterpret_cast<LPBYTE>(value), &bytes) == ERROR_SUCCESS) {
                        if (type == REG_EXPAND_SZ) {
                            wchar_t expanded[32767] = {};
                            profile = ::ExpandEnvironmentStringsW(value, expanded,
                                                                  static_cast<DWORD>(std::size(expanded)))
                                          ? platform::toUtf8(std::wstring(expanded))
                                          : platform::toUtf8(std::wstring(value));
                        } else if (type == REG_SZ) {
                            profile = platform::toUtf8(std::wstring(value));
                        }
                    }
                    ::RegCloseKey(handle);
                }
            }
        }
    }
    ::CloseHandle(token);
    while (!profile.empty() && (profile.back() == '\\' || profile.back() == '/'))
        profile.pop_back();
    return profile;
}

// `scoop.ps1` found through PATH, if any. The plugin does not execute it to ask
// for a root; it only reads the path, which is what locateScoop has always
// required (one documented entry point, never a PATH lookup for execution).
std::string shimFromPath()
{
    const std::wstring path = platform::toWide(env(L"PATH"));
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto sep = path.find(L';', start);
        std::wstring folder = path.substr(start, sep == std::wstring::npos ? std::wstring::npos : sep - start);
        if (!folder.empty()) {
            if (folder.front() == L'"' && folder.back() == L'"')
                folder = folder.substr(1, folder.size() - 2);
            const std::wstring candidate = folder + L"\\scoop.ps1";
            if (fileExists(platform::toUtf8(candidate)))
                return platform::toUtf8(candidate);
        }
        if (sep == std::wstring::npos)
            break;
        start = sep + 1;
    }
    return {};
}

} // namespace

ScoopLocation locateScoop()
{
    ScoopLocation location;

    // The profile is injected from the registry rather than the environment, so
    // the resolver sees a real folder even when the host ran us elevated. The
    // environment is read as-is otherwise, because %SCOOP% must still win.
    std::vector<std::pair<std::string, std::string>> pairs = environmentPairs();
    const std::string profile = profileFromRegistry();
    if (!profile.empty()) {
        bool replaced = false;
        for (auto& [name, value] : pairs) {
            if (name == "USERPROFILE") {
                value = profile;
                replaced = true;
            }
        }
        if (!replaced)
            pairs.emplace_back("USERPROFILE", profile);
    }

    const ScoopRootChoice choice = resolveScoopRoot(pairs, shimFromPath());
    if (choice.root.empty()) {
        location.detail = choice.detail.empty() ? "Scoop's root could not be determined" : choice.detail;
        return location;
    }
    location.root = choice.root;

    const std::string shim = location.root + "\\shims\\scoop.ps1";
    if (!fileExists(shim)) {
        // A Scoop installed only as a shim elsewhere is not trusted: the plugin
        // drives exactly one documented entry point, and it reads the root's own
        // copy rather than whatever PATH happens to resolve to.
        location.detail = "scoop.ps1 was not found at " + location.root + "\\shims"
                          + " (root from " + choice.source + ")";
        return location;
    }
    location.shim = shim;

    // Use the system PowerShell absolute path, never a PATH lookup: a PATH entry
    // in a user-writable folder would let another program run in Scoop's place.
    const std::string systemRoot = env(L"SystemRoot");
    const std::string powerShell = systemRoot.empty()
        ? std::string()
        : systemRoot + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    if (!fileExists(powerShell)) {
        location.detail = "the system powershell.exe was not found";
        return location;
    }
    location.powershell = powerShell;
    location.found = true;
    return location;
}

#else

ScoopLocation locateScoop()
{
    ScoopLocation location;
    location.detail = "Scoop is Windows-only";
    return location;
}

#endif

ScoopAdapter::ScoopAdapter(std::string root, std::string powershellPath)
    : m_root(std::move(root)), m_powershell(std::move(powershellPath))
{
    if (m_root.empty() || m_powershell.empty()) {
        const auto location = locateScoop();
        if (m_root.empty())
            m_root = location.root;
        if (m_powershell.empty())
            m_powershell = location.powershell;
        m_shim = location.shim;
    } else {
        m_shim = m_root + "\\shims\\scoop.ps1";
    }
}

bool ScoopAdapter::buildShimArgs(const std::string& shimPath, const std::vector<std::string>& scoopArgs,
                                 std::vector<std::string>& out, std::string& refusal)
{
    out.clear();
    refusal.clear();
    if (shimPath.empty()) {
        refusal = "the scoops shim path is empty";
        return false;
    }
    // Fixed, non-interactive invocation. -NoProfile keeps a user profile from
    // changing behaviour; -NonInteractive stops any prompt from hanging the UI.
    //
    // -Command rather than -File, and the shim is invoked with the call operator.
    // Under -File, PowerShell's formatting subsystem is not attached to the
    // pipeline, so a command that emits *objects* (scoop-search emits a table)
    // prints only its own header text and the rows are lost. `scoop search` on
    // Scoop 0.6.0 goes through scoop-search, so -File returned "Results from
    // local buckets..." and nothing else — every Scoop search parsed to zero
    // rows. -Command keeps the formatter, and Out-String -Width pins the table
    // width so the columns do not wrap at the console default.
    out = {"-NoProfile", "-NonInteractive", "-NoLogo", "-ExecutionPolicy", "Bypass",
           "-Command", "& '" + shimPath + "'"};

    for (const auto& a : scoopArgs) {
        if (a.empty()) {
            refusal = "an empty argument was supplied";
            return false;
        }
        // Every argument must be a plain token: no whitespace (it would split),
        // no leading dash (it would become a Scoop option), no shell or
        // PowerShell metacharacters, no URL scheme.
        if (a.front() == '-') {
            refusal = "refusing an option-like argument: " + a;
            return false;
        }
        if (a.find("://") != std::string::npos) {
            refusal = "refusing a URL as an argument: " + a;
            return false;
        }
        for (const char ch : a) {
            const auto c = static_cast<unsigned char>(ch);
            if (c <= 0x20 || c == 0x7f) {
                refusal = "refusing an argument containing whitespace or a control character";
                return false;
            }
            switch (ch) {
            case '"': case '\'': case '`': case '\\': case '/': case '$': case ';': case '&':
            case '|': case '<': case '>': case '(': case ')': case '{': case '}': case '[':
            case ']': case '*': case '?': case '!': case '^': case '%': case '~': case '=':
            case ',': case '#': case ':': case '@':
                refusal = "refusing an argument containing shell metacharacters";
                return false;
            default:
                break;
            }
        }
        out.push_back(a);
    }
    return true;
}

const ProbeResult& ScoopAdapter::probeCached()
{
    // Same rule as winget's: the root and shim cannot change for the life of
    // the process, so Ready/Unsupported stick and a Denied probe (a transient
    // lock) is retried on the next refresh.
    if (m_probed)
        return m_probeCache;
    m_probeCache = probe();
    if (m_probeCache.state == BackendState::Ready || m_probeCache.state == BackendState::Unsupported)
        m_probed = true;
    return m_probeCache;
}

ProbeResult ScoopAdapter::probe()
{
    ProbeResult result;
    const auto location = locateScoop();
    if (!location.found) {
        result.state = BackendState::Missing;
        result.detail = location.detail;
        return result;
    }
    result.programPath = location.powershell;

    std::uint32_t childError = 0;
    if (!platform::childProcessesAllowed(childError)) {
        result.state = BackendState::Denied;
        result.detail = childError == 1816
            ? "the host did not grant `processes`: child processes are blocked (error 1816)"
            : "could not create a child process (error " + std::to_string(childError) + ")";
        return result;
    }

    std::vector<std::string> args;
    std::string refusal;
    // `scoop --version` is not a Scoop verb; `scoop help` is, and prints usage.
    if (!buildShimArgs(location.shim, {"help"}, args, refusal)) {
        result.state = BackendState::Unsupported;
        result.detail = refusal;
        return result;
    }

    platform::RunOptions options;
    options.application = location.powershell;
    options.arguments = args;
    options.timeoutMs = 20000;
    const auto run = platform::run(options);
    if (!run.launched) {
        result.state = BackendState::Denied;
        result.detail = "could not start powershell: " + run.errorText;
        return result;
    }
    if (run.exitCode != 0) {
        result.state = BackendState::Denied;
        result.detail = "`scoop help` exited with " + std::to_string(run.exitCode);
        return result;
    }
    // The help text starts with "Usage: scoop <command> [<args>]"; requiring it
    // avoids declaring Ready on some other program's output.
    if (run.output.find("Usage: scoop") == std::string::npos) {
        result.state = BackendState::Unsupported;
        result.detail = "the shim did not print Scoop usage text";
        return result;
    }

    // The revision is what the capability table keys on; Scoop reports it only in
    // its own bucket metadata, so record the root and treat the revision as
    // unknown until a future probe reads it.
    result.version = "unknown";
    result.capability.manager = Manager::Scoop;
    result.capability.backendVersion = result.version;
    result.capability.scope = Scope::User;
    result.capability.evidence = Evidence::L3;   // the shim ran and answered
    result.capability.note = "read-only verbs verified through the fixed shim entry; mutations not verified";
    result.state = BackendState::Ready;
    return result;
}

namespace {

// Runs one shim verb. Every caller uses this so the invocation form is identical
// everywhere and a test can see exactly one code path.
platform::RunResult runScoop(const std::string& powerShell, const std::string& shim,
                            const std::vector<std::string>& scoopArgs, std::uint32_t timeoutMs,
                            const bool* cancel, std::string& refusal)
{
    std::vector<std::string> args;
    if (!ScoopAdapter::buildShimArgs(shim, scoopArgs, args, refusal)) {
        platform::RunResult failed;
        failed.errorText = refusal;
        return failed;
    }
    platform::RunOptions options;
    options.application = powerShell;
    options.arguments = std::move(args);
    options.timeoutMs = timeoutMs;
    options.cancelRequested = cancel;
    return platform::run(options);
}

} // namespace

// One `scoop export` PowerShell run, shared by every query in a scan.
struct ScoopAdapter::ExportResult {
    bool launched = false;
    std::string output;
    std::string refusal;
};

void ScoopAdapter::invalidateCaches()
{
    std::lock_guard<std::mutex> lock(m_exportMutex);
    m_exportFetched = false;
    m_exportLaunched = false;
    m_exportOutput.clear();
    m_exportRefusal.clear();
}

ScoopAdapter::ExportResult ScoopAdapter::exportCached()
{
    std::lock_guard<std::mutex> lock(m_exportMutex);
    if (!m_exportFetched) {
        m_exportFetched = true;
        const auto run = runScoop(m_powershell, m_shim, {"export"}, 60000, nullptr, m_exportRefusal);
        m_exportLaunched = run.launched;
        if (run.launched)
            m_exportOutput = run.output;
        else if (m_exportRefusal.empty())
            m_exportRefusal = run.errorText;
    }
    ScoopAdapter::ExportResult r;
    r.launched = m_exportLaunched;
    r.output = m_exportOutput;
    r.refusal = m_exportRefusal;
    return r;
}

Snapshot ScoopAdapter::readInstalled(const ProbeResult& probe, bool includeObserved)
{
    Snapshot snapshot;
    snapshot.scoop = probe.state;
    snapshot.scoopDetail = probe.detail;
    if (probe.state != BackendState::Ready) {
        snapshot.stale = true;
        snapshot.staleReason = probe.detail;
        return snapshot;
    }

    std::string refusal;
    // `export` is the only structured listing Scoop offers; `list` prints a
    // human table with no machine-readable form.
    const auto run = exportCached();
    if (!run.launched) {
        snapshot.scoop = BackendState::Denied;
        snapshot.scoopDetail = run.refusal;
        return snapshot;
    }
    std::string failure;
    snapshot = fromExportJson(run.output, nowText(), includeObserved, &failure);
    snapshot.scoop = probe.state;
    snapshot.scoopDetail = probe.detail;

    // Held apps come from `scoop hold` state, which `export` does not carry.
    // Reading status is read-only and tells the UI why a row cannot upgrade.
    std::string holdRefusal;
    const auto status = runScoop(m_powershell, m_shim, {"status"}, 60000, nullptr, holdRefusal);
    if (status.launched) {
        const auto held = parseHeldApps(status.output);
        for (auto& p : snapshot.packages)
            for (const auto& name : held)
                if (name == p.key.id)
                    p.holdPolicy = "held";
    }
    if (!failure.empty()) {
        snapshot.scoop = BackendState::Unsupported;
        snapshot.scoopDetail = failure;
    }
    return snapshot;
}

Snapshot ScoopAdapter::search(const ProbeResult& probe, const std::string& text, const bool* cancelRequested)
{
    Snapshot snapshot;
    snapshot.scoop = probe.state;
    if (probe.state != BackendState::Ready) {
        snapshot.scoopDetail = probe.detail;
        return snapshot;
    }
    if (text.empty())
        return snapshot;

    // `scoop search` rather than `scoop update`: search reads the local bucket
    // metadata and must not sync anything.
    std::string refusal;
    const auto run = runScoop(m_powershell, m_shim, {"search", text}, 90000, cancelRequested, refusal);
    if (!run.launched) {
        snapshot.scoop = BackendState::Denied;
        snapshot.scoopDetail = refusal.empty() ? run.errorText : refusal;
        return snapshot;
    }
    std::string failure;
    snapshot = fromSearchText(run.output, nowText(), text, &failure);
    if (!failure.empty()) {
        snapshot.scoop = BackendState::Unsupported;
        snapshot.scoopDetail = failure;
    }
    return snapshot;
}

Snapshot ScoopAdapter::readUpdates(const ProbeResult& probe)
{
    // Scoop has no "list updates" verb, and `scoop status` may sync buckets —
    // a side effect a query must never have. What Scoop does leave on disk is
    // every bucket's manifest: %SCOOP%\buckets\<bucket>\bucket\<app>.json
    // carries the current "version". Comparing that against the export JSON's
    // installed version is a pure file read, needs no process at all, and is
    // exactly the evidence `scoop update <app>` would act on.
    Snapshot snapshot;
    snapshot.scoop = probe.state;
    snapshot.observedAt = nowText();
    snapshot.scanScope = "scoop bucket manifests (local inventory)";
    if (probe.state != BackendState::Ready) {
        snapshot.scoopDetail = probe.detail;
        return snapshot;
    }

    // The installed inventory comes from export, which this adapter already
    // reads; the shared cache keeps it to one PowerShell run per scan.
    const auto run = exportCached();
    if (!run.launched) {
        snapshot.scoop = BackendState::Denied;
        snapshot.scoopDetail = run.refusal;
        return snapshot;
    }
    std::string failure;
    Snapshot installed = fromExportJson(run.output, snapshot.observedAt, true, &failure);
    if (!failure.empty()) {
        snapshot.scoop = BackendState::Unsupported;
        snapshot.scoopDetail = failure;
        return snapshot;
    }

    // The root comes from the same locateScoop the probe used, so the manifests
    // read are the ones this Scoop actually installs from.
    const auto location = locateScoop();
    if (!location.found) {
        snapshot.scoopDetail = location.detail;
        return snapshot;
    }
    fromBucketManifests(location.root, installed.packages, snapshot.observedAt, snapshot, &failure);
    if (!failure.empty()) {
        snapshot.scoop = BackendState::Unsupported;
        snapshot.scoopDetail = failure;
    }
    return snapshot;
}

CommandSpec ScoopAdapter::buildMutation(const Plan& plan, const std::vector<std::string>& agreementsAccepted)
{
    CommandSpec spec;
    (void)agreementsAccepted;   // Scoop has no source/installer agreement concept

    if (!isResolvable(plan.key)) {
        spec.refusal = "identity is not exactly resolvable";
        return spec;
    }
    if (plan.key.scope != Scope::User) {
        // Scoop installs per user; a machine/global scope is not something this
        // plugin can do and it never elevates. Scoop rows always record user
        // scope (its export does not state one, and user is what Scoop installs),
        // so this only rejects a row that was built wrong.
        spec.refusal = "this plugin only performs user-scope Scoop operations";
        return spec;
    }

    std::vector<std::string> scoopArgs;
    switch (plan.action) {
    case Action::Install: {
        scoopArgs = {"install", plan.key.id};
        break;
    }
    case Action::Uninstall:
        scoopArgs = {"uninstall", plan.key.id};
        break;
    case Action::Upgrade:
        // `update <app>` upgrades one app. Scoop may sync buckets first; the plan
        // discloses that rather than pretending it is a pure local operation.
        scoopArgs = {"update", plan.key.id};
        break;
    case Action::Reinstall:
        // Explicit two-phase only: force install and reset are both wrong.
        spec.refusal = "Scoop reinstall is an explicit uninstall followed by an install; it is never one command";
        return spec;
    case Action::None:
        spec.refusal = "no Scoop command for this action";
        return spec;
    }

    std::vector<std::string> args;
    std::string refusal;
    if (!buildShimArgs(m_shim, scoopArgs, args, refusal)) {
        spec.refusal = refusal;
        return spec;
    }

    spec.ok = true;
    spec.options.application = m_powershell;
    spec.options.arguments = std::move(args);
    spec.options.timeoutMs = 900000;
    spec.redactedPreview = "powershell " + scoopArgs[0];
    for (std::size_t i = 1; i < scoopArgs.size(); ++i)
        spec.redactedPreview += " " + scoopArgs[i];
    return spec;
}

bool ScoopAdapter::verifyInstalled(const ProbeResult& probe, const PackageKey& key, std::string& installedVersion,
                                   std::string& detail)
{
    installedVersion.clear();
    if (probe.state != BackendState::Ready) {
        detail = probe.detail;
        return false;
    }
    std::string refusal;
    // Verification must not read the scan cache: the whole point is that the
    // inventory changed, and a stale answer would report the pre-run state.
    const auto run = runScoop(m_powershell, m_shim, {"export"}, 60000, nullptr, refusal);
    if (!run.launched) {
        detail = refusal.empty() ? run.errorText : refusal;
        return false;
    }
    std::string failure;
    const auto snapshot = fromExportJson(run.output, nowText(), true, &failure);
    if (!failure.empty()) {
        detail = "verification output was unsupported: " + failure;
        return false;
    }
    for (const auto& p : snapshot.packages) {
        if (p.key.id == key.id) {
            installedVersion = p.installedVersion;
            return true;
        }
    }
    detail = "the app was not found after the run";
    return false;
}

// ---------------------------------------------------------------- pure helpers

Snapshot ScoopAdapter::fromExportJson(const std::string& text, const std::string& observedAt,
                                      bool includeObserved, std::string* failure)
{
    Snapshot snapshot;
    snapshot.observedAt = observedAt;
    snapshot.scanScope = "scoop export (local inventory)";

    std::string error;
    const auto root = jsonlite::parse(text, error);
    if (!root || !root->isObject()) {
        snapshot.scoop = BackendState::Unsupported;
        snapshot.scoopDetail = "scoop export output is unsupported here: " + error;
        if (failure)
            *failure = snapshot.scoopDetail;
        return snapshot;
    }
    const auto apps = root->get("apps");
    if (!apps || !apps->isArray()) {
        snapshot.scoop = BackendState::Unsupported;
        snapshot.scoopDetail = "scoop export has no apps array";
        if (failure)
            *failure = snapshot.scoopDetail;
        return snapshot;
    }

    for (const auto& entry : apps->array) {
        if (!entry || !entry->isObject())
            continue;
        const std::string name = entry->stringAt("Name");
        if (name.empty())
            continue;
        Package p;
        p.key.manager = Manager::Scoop;
        p.key.source = entry->stringAt("Source");
        p.key.id = name;
        // Upgrade/uninstall are refused for a non-user scope, and Scoop's own
        // export does not state scope; user is what Scoop installs by default.
        p.key.scope = Scope::User;
        p.name = name;
        p.installedVersion = entry->stringAt("Version");
        p.availableVersion = p.installedVersion;   // export carries no remote version
        p.availableKind = "same";
        p.arch = "unknown";
        // export is Scoop's own record of what it installed.
        p.provenance = Provenance::BackendRecorded;
        p.metadataAt = observedAt;
        p.dependencies = "unknown";
        p.material = "installed bucket manifest";
        p.canInstall = false;
        p.canUninstall = !p.key.source.empty();
        p.canUpgrade = false;   // needs a remote version no read-only verb gives
        if (p.key.source.empty())
            p.limits = "the bucket is not recorded, so the identity is incomplete";
        // Scoop's provenance is always its own record, so includeObserved has no
        // rows to hide; it is accepted for signature symmetry with WinGet.
        (void)includeObserved;
        snapshot.packages.push_back(std::move(p));
    }
    snapshot.scoop = BackendState::Ready;
    return snapshot;
}

void ScoopAdapter::fromBucketManifests(const std::string& scoopRoot, std::vector<Package>& installed,
                                       const std::string& observedAt, Snapshot& out, std::string* failure)
{
    // One version field per manifest is all this needs; jsonlite keeps the read
    // bounded and free of any evaluation, so a manifest is data, never code.
    int manifestsRead = 0;
    int manifestsMissing = 0;
    for (auto& p : installed) {
        if (p.key.source.empty() || p.installedVersion.empty())
            continue;
        const std::string manifestPath =
            scoopRoot + "\\buckets\\" + p.key.source + "\\bucket\\" + p.key.id + ".json";
        HANDLE file = ::CreateFileW(platform::toWide(manifestPath).c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            // A manifest can be gone because the bucket dropped the app; that is
            // not an error and not an update.
            ++manifestsMissing;
            continue;
        }
        LARGE_INTEGER size{};
        if (!::GetFileSizeEx(file, &size) || size.QuadPart > 4 * 1024 * 1024) {
            ::CloseHandle(file);
            continue;   // oversized or unreadable: no evidence, no guess
        }
        std::string text(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        const BOOL ok = ::ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
        ::CloseHandle(file);
        if (!ok || read == 0)
            continue;
        ++manifestsRead;

        std::string error;
        const auto root = jsonlite::parse(text, error);
        if (!root || !root->isObject())
            continue;
        const std::string manifestVersion = root->stringAt("version");
        if (manifestVersion.empty())
            continue;

        // A pin still wins: a held app shows its policy, never an upgrade.
        if (!p.holdPolicy.empty())
            continue;
        if (pkg::isNewer(p.installedVersion, manifestVersion)) {
            p.availableVersion = manifestVersion;
            p.availableKind = "version";
            p.canUpgrade = true;
            // It is installed, and a Scoop removal is by name: no scope needed.
            p.canUninstall = !p.key.source.empty();
            p.material = "bucket manifest " + p.key.source;
            out.packages.push_back(p);
        }
    }
    if (manifestsRead == 0 && manifestsMissing > 0 && installed.size() > 0) {
        // Every manifest was gone: the buckets directory is probably not where
        // locateScoop said it was. Say so rather than report a silently empty
        // update list.
        if (failure)
            *failure = "no bucket manifest could be read under " + scoopRoot + "\\buckets (" +
                       std::to_string(manifestsMissing) + " missing)";
    }
    out.observedAt = observedAt;
    out.scanScope = "scoop bucket manifests (local inventory)";
}

Snapshot ScoopAdapter::fromSearchText(const std::string& text, const std::string& observedAt,
                                      const std::string& query, std::string* failure)
{
    Snapshot snapshot;
    snapshot.observedAt = observedAt;
    snapshot.scanScope = "scoop search " + query;
    (void)failure;   // plain text has no parse failure mode: unmatched lines are skipped

    // Two real shapes exist and both must parse, because which one appears
    // depends on the Scoop version rather than on anything this plugin controls.
    //
    // Scoop 0.6.0 and later print a table:
    //
    //     Results from local buckets...
    //
    //     Name             Version           Source   Binaries
    //     ----             -------           ------   --------
    //     vim              9.2.1320          main
    //     fvim             0.3.549           extras
    //
    // Scoop 0.5.x (and scoop-search) print grouped sections:
    //
    //     'extras' bucket:
    //         fvim (0.3.549)
    //         wsl-terminal (0.9.2) --> includes 'vim.exe'
    //
    //     'main' bucket:
    //         vim (9.1.0)
    //
    // Only the table shape was handled before, so on 0.6.0 every search parsed
    // to zero rows and the Discover tab showed nothing for Scoop at all.
    //
    // A row only becomes installable once it has both a name and a bucket,
    // because Scoop's own record needs the bucket to form an exact identity.
    if (looksLikeSearchTable(text))
        return fromSearchTable(text, observedAt, query);

    std::string currentBucket;
    for (const auto& raw : splitLines(text)) {
        const std::string line = trim(raw);
        if (line.empty())
            continue;
        if (line.rfind("Results from", 0) == 0 || line.rfind("WARN", 0) == 0 || line.rfind("INFO", 0) == 0)
            continue;

        // A section header: 'name' bucket:  (quotes may be single or none).
        if (line.find("bucket:") != std::string::npos && line.back() == ':') {
            const auto q1 = line.find('\'');
            const auto q2 = q1 == std::string::npos ? std::string::npos : line.find('\'', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos)
                currentBucket = line.substr(q1 + 1, q2 - q1 - 1);
            else
                currentBucket = line.substr(0, line.find(' '));
            continue;
        }

        const auto open = line.find(" (");
        if (open == std::string::npos)
            continue;
        const auto close = line.find(')', open);
        if (close == std::string::npos)
            continue;
        const std::string name = line.substr(0, open);
        const std::string version = line.substr(open + 2, close - open - 2);
        if (name.empty() || name.find(' ') != std::string::npos)
            continue;   // a row is one token; anything else is a note, not a package

        Package p;
        p.key.manager = Manager::Scoop;
        p.key.source = currentBucket;
        p.key.id = name;
        p.key.scope = Scope::User;
        p.name = name;
        p.availableVersion = version;
        p.availableKind = version.empty() ? "unknown" : "version";
        p.arch = "unknown";
        p.provenance = Provenance::None;
        p.metadataAt = observedAt;
        p.dependencies = "unknown";
        p.material = currentBucket.empty() ? "unknown bucket" : "bucket " + currentBucket;
        // Installing needs a bucket; without one the identity is incomplete.
        p.canInstall = !currentBucket.empty();
        if (currentBucket.empty())
            p.limits = "the search output did not name a bucket, so the identity is incomplete";
        snapshot.packages.push_back(std::move(p));
    }
    snapshot.scoop = BackendState::Ready;
    return snapshot;
}

bool ScoopAdapter::looksLikeSearchTable(const std::string& text)
{
    // The header row is the discriminator: "Name" followed by "Version" and
    // "Source" on the same line, with a dashed rule underneath. Checking for the
    // words alone would misfire on a package literally named "name".
    bool sawHeader = false;
    for (const auto& raw : splitLines(text)) {
        const std::string line = trim(raw);
        if (line.empty())
            continue;
        if (sawHeader)
            return line.rfind("----", 0) == 0;
        const auto name = line.find("Name");
        const auto version = line.find("Version");
        const auto source = line.find("Source");
        if (name == 0 && version != std::string::npos && source != std::string::npos)
            sawHeader = true;
    }
    return false;
}

Snapshot ScoopAdapter::fromSearchTable(const std::string& text, const std::string& observedAt,
                                       const std::string& query)
{
    Snapshot snapshot;
    snapshot.observedAt = observedAt;
    snapshot.scanScope = "scoop search " + query;

    // Column positions come from the header, not from fixed offsets: the widths
    // depend on the longest name in the result set, so a hard-coded layout would
    // break on the next query. "Binaries" is optional and may be absent.
    std::size_t nameCol = 0;
    std::size_t versionCol = std::string::npos;
    std::size_t sourceCol = std::string::npos;
    std::size_t binariesCol = std::string::npos;
    bool inBody = false;

    for (const auto& raw : splitLines(text)) {
        const std::string line = raw;
        if (trim(line).empty())
            continue;

        if (!inBody) {
            const auto name = line.find("Name");
            const auto version = line.find("Version");
            const auto source = line.find("Source");
            if (name != std::string::npos && version != std::string::npos &&
                source != std::string::npos) {
                nameCol = name;
                versionCol = version;
                sourceCol = source;
                // Binaries is the last column and is often empty, but when it is
                // populated its text would otherwise be read as part of the
                // bucket. Its header position is the boundary.
                const auto binaries = line.find("Binaries");
                binariesCol = binaries != std::string::npos && binaries > source ? binaries
                                                                                : std::string::npos;
                continue;
            }
            if (versionCol != std::string::npos && trim(line).rfind("----", 0) == 0) {
                inBody = true;
                continue;
            }
            continue;
        }

        // A body row: slice by the header's column starts. The last column runs
        // to the end of the line.
        auto slice = [&](std::size_t from, std::size_t to) {
            if (from >= line.size())
                return std::string();
            const std::size_t end = to == std::string::npos ? line.size() : std::min(to, line.size());
            return trim(line.substr(from, end - from));
        };
        const std::string name = slice(nameCol, versionCol);
        const std::string version = slice(versionCol, sourceCol);
        const std::string bucket = slice(sourceCol, binariesCol);
        if (name.empty() || name.find(' ') != std::string::npos)
            continue;   // a package name is one token; anything else is a note

        Package p;
        p.key.manager = Manager::Scoop;
        p.key.source = bucket;
        p.key.id = name;
        p.key.scope = Scope::User;
        p.name = name;
        p.availableVersion = version;
        p.availableKind = version.empty() ? "unknown" : "version";
        p.arch = "unknown";
        p.provenance = Provenance::None;
        p.metadataAt = observedAt;
        p.dependencies = "unknown";
        p.material = bucket.empty() ? "unknown bucket" : "bucket " + bucket;
        p.canInstall = !bucket.empty();
        if (bucket.empty())
            p.limits = "the search output did not name a bucket, so the identity is incomplete";
        snapshot.packages.push_back(std::move(p));
    }
    snapshot.scoop = BackendState::Ready;
    return snapshot;
}

std::vector<std::string> ScoopAdapter::parseHeldApps(const std::string& text)
{
    // `scoop status` prints "Held apps: name1, name2" when anything is held.
    std::vector<std::string> held;
    const auto at = text.find("Held apps:");
    if (at == std::string::npos)
        return held;
    auto rest = text.substr(at + std::string("Held apps:").size());
    const auto newline = rest.find('\n');
    if (newline != std::string::npos)
        rest = rest.substr(0, newline);
    std::string current;
    for (const char c : rest) {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                held.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty())
        held.push_back(current);
    return held;
}

} // namespace pkg
