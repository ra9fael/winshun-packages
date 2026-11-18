// window.cpp — the UI controller.
//
// Threading: every method here runs on the GUI thread. The protocol thread calls
// the marshalling wrappers at the bottom through QMetaObject::invokeMethod, so no
// QObject is ever touched from two threads.
#include "window.h"

#include <QDesktopServices>
#include <QDir>
#include <QStringList>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QUrl>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <limits>

#include "../adapters/scoop_adapter.h"
#include "../adapters/winget_adapter.h"
#include "../adapters/winget_table.h"
#include "../core/cache.h"
#include "../core/search_cache.h"
#include "../core/pins_cache.h"
#include "../core/probe_cache.h"
#include "../core/redact.h"
#include "../platform/process.h"
#include "models.h"

namespace pkg::ui {
namespace {

// The two adapters are the only backends this plugin knows. They are created once
// and reused so a probe result is not thrown away between keystrokes.
WingetAdapter& wingetAdapter()
{
    static WingetAdapter adapter;
    return adapter;
}

ScoopAdapter& scoopAdapter()
{
    static ScoopAdapter adapter;
    return adapter;
}

std::string toString(const QString& q) { return q.toStdString(); }

// Case-insensitive exact-id matching for selection restore and the info cache.
std::string lowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// The last `maxBytes` of a process's output, trimmed at a line boundary. A task
// log shows the tail because that is where the error is; the head is the banner.
std::string tailOf(const std::string& text, std::size_t maxBytes)
{
    if (text.empty())
        return {};
    if (text.size() <= maxBytes)
        return text;
    std::size_t start = text.size() - maxBytes;
    const std::size_t nl = text.find('\n', start);
    if (nl != std::string::npos && nl + 1 < text.size())
        start = nl + 1;
    return text.substr(start);
}

// Trims trailing whitespace and collapses a trailing newline, so a log line is
// one line in the UI.
std::string trimBlock(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

} // namespace

Controller::Controller(QObject* parent) : QObject(parent)
{
    // Elevation cannot change for the life of the process, so it is read once.
    m_elevated = platform::isElevated();
    // The previous scan is read before the window exists, so the first paint
    // already has rows: a resident plugin is restarted by the host at any time,
    // and re-running winget list on every launch is what made it feel like
    // nothing was ever cached.
    loadCache();
    loadInfoCache();
    // After loadCache, so the window still paints even when there is no verdict.
    // A seeded verdict must be installed before the first probeCached() call,
    // which happens on the first refresh.
    loadProbeCache();
    loadPinsCache();
}

Controller::~Controller()
{
    // Every worker is joined, not detached. A detached worker that posted a
    // callback after the controller was destroyed dereferenced freed memory, and
    // an installer window that outlived the host made that likely rather than
    // theoretical. Cancellation is signalled first so a waiting installer does not
    // hold shutdown for its full timeout.
    m_shuttingDown.store(true);
    if (m_runCancel)
        m_runCancel->store(true);
    std::lock_guard<std::mutex> lock(m_workerMutex);
    for (auto& worker : m_workers) {
        if (worker.thread.joinable())
            worker.thread.join();
    }
    m_workers.clear();
}

void Controller::spawnWorker(std::function<void()> body)
{
    if (m_shuttingDown.load())
        return;
    std::lock_guard<std::mutex> lock(m_workerMutex);
    // Reap finished workers before adding another: a resident plugin runs for days,
    // and joinable threads that are never joined accumulate handles.
    for (auto it = m_workers.begin(); it != m_workers.end();) {
        if (it->done->load()) {
            if (it->thread.joinable())
                it->thread.join();
            it = m_workers.erase(it);
        } else {
            ++it;
        }
    }
    auto done = std::make_shared<std::atomic_bool>(false);
    std::thread thread([this, body = std::move(body), done] {
        body();
        done->store(true);
    });
    m_workers.push_back(Worker{std::move(thread), done});
}

void Controller::attachModels(PackagesModel* packages, TasksModel* tasks)
{
    m_packages = packages;
    m_tasks = tasks;
    // The cache is loaded in the constructor, before any model exists, so
    // rebuildRows() there has nothing to write to. Without this second pass a
    // launch that finds a fresh cache paints an empty list and only the refresh
    // button brings rows back — which is the opposite of what the cache is for.
    if (m_packages && !m_installed.packages.empty())
        rebuildRows();
}

bool Controller::chinese() const
{
    if (m_settings.language == "zh-CN")
        return true;
    if (m_settings.language == "en-US")
        return false;
    return m_locale.startsWith("zh");
}

QString Controller::text(const char* zh, const char* en) const
{
    return chinese() ? QString::fromUtf8(zh) : QString::fromUtf8(en);
}

void Controller::applyAppearance(const QString& locale, bool dark, const QVariantMap& look)
{
    // The controller keeps only what it needs; the full map goes to Theme.qml
    // through the themeObject, which the window sets as a context property.
    m_locale = locale;
    m_dark = dark;
    m_look = look;
    if (m_theme)
        m_theme->setProperty("look", m_look);
    emit appearanceChanged();
}

void Controller::applySettings(const UiSettings& settings)
{
    m_settings = settings;
    // Refresh the settings controls when the host pushes settingsChanged.
    emit settingsApplied();
}

void Controller::openWindow(OpenMode mode)
{
    m_windowOpen = true;
    emit windowShouldShow();
    // Opening the window probes only when at least one backend's own answer is
    // stale: each backend carries its own timestamp, so a fresh WinGet list is not
    // re-read just because Scoop's part is old. Nothing runs before the user asked
    // for the window at all.

    // "Open without scanning" is a promise to the user: the window shows what
    // was cached and no backend is queried because of opening. It does not stop
    // a scan the user started earlier — that one is already running.
    if (mode == OpenMode::NoScan)
        return;
    if (mode == OpenMode::ForceRefresh) {
        // An explicit refresh is a request to look again, including at whether
        // the backend still answers: drop a verdict seeded from disk.
        wingetAdapter().invalidateProbe();
        scoopAdapter().invalidateProbe();
        startBackendRefresh(true);
        return;
    }

    // The policy decides: "never" never scans, "always" always does, and
    // "stale" scans only when the cache has expired.
    const std::string policy = m_settings.scanOnOpen.empty() ? "stale" : m_settings.scanOnOpen;
    if (policy == "never")
        return;
    const bool anyStale =
        backendNeedsScan(m_wingetInstalled.wingetObservedAt, m_installed.winget) ||
        backendNeedsScan(m_scoopInstalled.scoopObservedAt, m_installed.scoop);
    if (policy == "always" || anyStale)
        // "stale" refreshes only what is stale; "always" refreshes both.
        startBackendRefresh(policy == "always");
}

void Controller::openSettings()
{
    openWindow(OpenMode::NoScan);
    QTimer::singleShot(0, this, [this] { emit settingsRequested(); });
}

void Controller::runStartupScan()
{
    // The user asked for a scan when WinShun starts. It must not open a window:
    // a background refresh that pops the window up would be worse than no scan.
    if (!m_settings.scanOnStartup || m_fakeMode)
        return;
    // Scanning under the wrong rights can produce a different answer than the
    // user's normal session, so a plugin that is required to be elevated does
    // not scan quietly with less rights — it waits for the elevated copy.
    if (m_settings.requireAdminForScan && !platform::isElevated()) {
        m_scanPolicyNote = text("需要管理员权限的扫描尚未运行：当前进程没有管理员权限。",
                                "The scan that needs administrator rights did not run: this process does not "
                                "have administrator rights.");
        emit scanPolicyNoteChanged();
        return;
    }
    startBackendRefresh();
}

QString Controller::cachePath() const
{
    const std::string dir = platform::pluginDataDir();
    return dir.empty() ? QString() : QString::fromStdString(dir + "\\cache.json");
}

QString Controller::infoPath() const
{
    const std::string dir = platform::pluginDataDir();
    return dir.empty() ? QString() : QString::fromStdString(dir + "\\info.json");
}

void Controller::loadInfoCache()
{
    // Same story as loadCache: the process may end between window opens, and a
    // cached `winget show` answer saves a multi-second process per selection.
    const QString path = infoPath();
    if (path.isEmpty())
        return;
    std::string text;
    if (!platform::readTextFile(toString(path), text))
        return;
    std::string error;
    info_cache::Store store;
    if (!info_cache::parse(text, store, error))
        return;   // unreadable: ignore, the next fetch rebuilds it
    m_infoStore = std::move(store);
}

void Controller::saveInfoCache()
{
    const QString path = infoPath();
    if (path.isEmpty())
        return;
    platform::writeTextFile(toString(path), info_cache::serialize(m_infoStore));
}

bool Controller::infoFresh(const std::string& observedAt) const
{
    if (observedAt.empty() || m_settings.refreshMinutes <= 0)
        return false;
    const QDateTime fetched = QDateTime::fromString(QString::fromStdString(observedAt), Qt::ISODate);
    if (!fetched.isValid())
        return false;
    return fetched.msecsTo(QDateTime::currentDateTime()) <
           static_cast<qint64>(m_settings.refreshMinutes) * 60 * 1000;
}

QString Controller::searchCachePath(const QString& needle) const
{
    const std::string dir = platform::pluginDataDir();
    if (dir.empty())
        return {};
    return QString::fromStdString(search_cache::pathFor(dir, toString(needle)));
}

bool Controller::applyCachedSearch(const QString& needle)
{
    const QString path = searchCachePath(needle);
    if (path.isEmpty())
        return false;
    std::string text;
    if (!platform::readTextFile(toString(path), text))
        return false;
    Snapshot cached;
    std::string error;
    if (!search_cache::parse(text, cached, error))
        return false;
    // Same freshness rule as every other cache: an unparsable or expired stamp
    // means the answer must be asked again, never shown as if it were current.
    if (!infoFresh(cached.observedAt))
        return false;

    m_search = cached;
    m_search.scanScope = "search";
    m_searchInFlight = false;
    m_searchPending = false;
    rebuildRows();
    emit searchStateChanged();
    emit statusChanged();
    return true;
}

void Controller::saveSearchCache(const QString& needle, const Snapshot& snapshot)
{
    // The harness's deterministic rows must never become a cache entry the next
    // real run would serve.
    if (m_fakeMode)
        return;
    if (!search_cache::worthWriting(snapshot))
        return;
    const std::string dir = platform::pluginDataDir();
    if (dir.empty())
        return;
    const std::string folder = dir + "\\search";
    if (!platform::ensureDirectory(folder))
        return;
    platform::writeTextFile(search_cache::pathFor(dir, toString(needle)), search_cache::serialize(snapshot));
    // Keep the directory bounded: an old query is worth less than a recent one.
    platform::pruneOldFiles(folder, search_cache::kMaxEntries);
}

void Controller::clearSearchCache()
{
    const std::string dir = platform::pluginDataDir();
    if (dir.empty())
        return;
    platform::pruneOldFiles(dir + "\\search", 0);
}

QString Controller::probePath() const
{
    const std::string dir = platform::pluginDataDir();
    return dir.empty() ? QString() : QString::fromStdString(dir + "\\probe.json");
}

void Controller::loadProbeCache()
{
    // The harness injects its own snapshot; a remembered verdict would make its
    // screenshots depend on this machine's last real probe.
    if (m_fakeMode)
        return;
    const QString path = probePath();
    if (path.isEmpty())
        return;
    std::string contents;
    if (!platform::readTextFile(toString(path), contents))
        return;
    probe_cache::Store store;
    std::string error;
    if (!probe_cache::parse(contents, store, error))
        return;

    // The stamp is what makes reuse honest: same file, same size, same write
    // time. A WinGet update changes them and the next launch probes for real.
    const auto seed = [this](const probe_cache::Record& record, Adapter& adapter, ProbeResult& known) {
        std::string current;
        if (!platform::fileStamp(adapter.probeStampPath(), current))
            return;
        if (!probe_cache::reusable(record, current))
            return;
        ProbeResult probe;
        probe.state = record.state;
        probe.detail = record.detail;
        probe.version = record.version;
        probe.programPath = record.program;
        adapter.seedProbe(probe);
        // The window's own copy too: loadCache() derived it from the snapshot,
        // but a seeded verdict is more specific (it carries the version).
        known = probe;
    };
    if (store.hasWinget)
        seed(store.winget, wingetAdapter(), m_wingetProbe);
    if (store.hasScoop)
        seed(store.scoop, scoopAdapter(), m_scoopProbe);

    // Shown in the cache summary: "did this launch really skip the probe?" is
    // otherwise invisible, and the answer is the whole point of the file.
    QStringList reused;
    if (wingetAdapter().isProbeSeeded())
        reused << text("WinGet", "WinGet") + QStringLiteral(" ") + QString::fromStdString(store.winget.version);
    if (scoopAdapter().isProbeSeeded())
        reused << text("Scoop", "Scoop") + QStringLiteral(" ") + QString::fromStdString(store.scoop.version);
    m_probeNote = reused.isEmpty()
        ? text("探针：本次启动会重新检测", "Probe: re-checking on this launch")
        : text("探针：复用上次结果（", "Probe: reused from disk (") + reused.join(QStringLiteral("、")) + QStringLiteral(")");
}

void Controller::saveProbeCache(){
    if (m_fakeMode)
        return;
    const QString path = probePath();
    if (path.isEmpty())
        return;

    probe_cache::Store store;
    const auto fill = [](const ProbeResult& probe, const std::string& stampPath,
                         probe_cache::Record& record, bool& has) {
        // Only a verdict that can be built on is worth remembering; a refusal is
        // often transient (a lock, a missing permission) and must be retried.
        if (probe.state != BackendState::Ready && probe.state != BackendState::Unsupported)
            return;
        std::string stamp;
        if (!platform::fileStamp(stampPath, stamp))
            return;
        record.state = probe.state;
        record.detail = probe.detail;
        record.version = probe.version;
        record.program = stampPath;
        record.stamp = stamp;
        record.observedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
        has = true;
    };
    fill(m_wingetProbe, wingetAdapter().probeStampPath(), store.winget, store.hasWinget);
    fill(m_scoopProbe, scoopAdapter().probeStampPath(), store.scoop, store.hasScoop);
    // A scan where both backends failed must not erase a good verdict.
    if (!store.hasWinget && !store.hasScoop)
        return;
    platform::writeTextFile(toString(path), probe_cache::serialize(store));
}

QString Controller::pinsPath() const
{
    const std::string dir = platform::pluginDataDir();
    return dir.empty() ? QString() : QString::fromStdString(dir + "\\pins.json");
}

void Controller::loadPinsCache()
{
    if (m_fakeMode)
        return;
    const QString path = pinsPath();
    if (path.isEmpty())
        return;
    std::string contents;
    if (!platform::readTextFile(toString(path), contents))
        return;
    pins_cache::Store store;
    std::string error;
    if (!pins_cache::parse(contents, store, error))
        return;
    // Same freshness rule as the other caches: an old list is worth one process
    // to re-read, because a pin the user removed must stop blocking upgrades.
    if (!infoFresh(store.observedAt))
        return;
    wingetAdapter().seedPins(std::move(store.entries));
}

void Controller::savePinsCache()
{
    if (m_fakeMode || !wingetAdapter().pinsKnown())
        return;
    const QString path = pinsPath();
    if (path.isEmpty())
        return;
    pins_cache::Store store;
    store.observedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    store.entries = wingetAdapter().cachedPins();
    platform::writeTextFile(toString(path), pins_cache::serialize(store));
}

void Controller::clearPinsCache()
{
    wingetAdapter().invalidatePins();
    const QString path = pinsPath();
    if (!path.isEmpty())
        QFile::remove(path);
}

void Controller::showCachedInfo(const info_cache::Entry& entry, int row, const std::string& id)
{
    m_packageInfoRow = row;
    m_packageInfoId = QString::fromStdString(id);
    m_packageInfoBusy = false;
    m_packageInfo.clear();
    for (const auto& [key, value] : entry.fields)
        m_packageInfo.insert(QString::fromStdString(key), QString::fromStdString(value));
    emit packageInfoChanged();
    emit statusChanged();
}

QVariantMap Controller::settings() const
{
    return {{QStringLiteral("uiLanguage"), QString::fromStdString(m_settings.language.empty() ? "auto" : m_settings.language)},
            {QStringLiteral("showObserved"), m_settings.showObserved},
            {QStringLiteral("refreshMinutes"), m_settings.refreshMinutes},
            {QStringLiteral("autoFetchInfo"), m_settings.autoFetchInfo},
            {QStringLiteral("scanOnOpen"), QString::fromStdString(m_settings.scanOnOpen)},
            {QStringLiteral("scanOnStartup"), m_settings.scanOnStartup},
            {QStringLiteral("debugLogging"), m_settings.debugLogging}};
}

void Controller::updateSetting(const QString& key, const QVariant& value)
{
    if (key == QStringLiteral("uiLanguage")) {
        const QString language = value.toString();
        if (language != QStringLiteral("auto") && language != QStringLiteral("zh-CN") &&
            language != QStringLiteral("en-US"))
            return;
        m_settings.language = language.toStdString();
    } else if (key == QStringLiteral("showObserved")) {
        m_settings.showObserved = value.toBool();
    } else if (key == QStringLiteral("refreshMinutes")) {
        const int minutes = value.toInt();
        if (minutes < 0 || minutes > 1440)
            return;
        m_settings.refreshMinutes = minutes;
    } else if (key == QStringLiteral("autoFetchInfo")) {
        m_settings.autoFetchInfo = value.toBool();
    } else if (key == QStringLiteral("scanOnOpen")) {
        const QString policy = value.toString();
        if (policy != QStringLiteral("stale") && policy != QStringLiteral("always") &&
            policy != QStringLiteral("never"))
            return;
        m_settings.scanOnOpen = policy.toStdString();
    } else if (key == QStringLiteral("scanOnStartup")) {
        m_settings.scanOnStartup = value.toBool();
    } else if (key == QStringLiteral("debugLogging")) {
        m_settings.debugLogging = value.toBool();
    } else {
        return;
    }
    emit settingsApplied();
    if (key == QStringLiteral("uiLanguage"))
        emit statusChanged();
    if (m_settingsUpdater)
        m_settingsUpdater({{key, value}});
}

QString Controller::cacheSummary() const
{
    const std::string dir = platform::pluginDataDir();
    if (dir.empty())
        return text("数据文件夹不可用。", "The data folder is unavailable.");
    QString line = QString::fromStdString(dir);
    const auto sizeLine = [this](const QFileInfo& file) {
        return file.exists() ? text(" · ", " · ") + QString::number(file.size()) + " B" : QString();
    };
    line += text("\n清单缓存", "\nInventory cache") + sizeLine(QFileInfo(cachePath()));
    if (m_lastRefresh.isValid())
        line += text(" · 扫描于 ", " · scanned ") + m_lastRefresh.toString("yyyy-MM-dd hh:mm");
    line += text("\n详情缓存", "\nInfo cache") + sizeLine(QFileInfo(infoPath())) +
            text(" · ", " · ") + QString::number(static_cast<qsizetype>(m_infoStore.entries.size())) +
            text(" 条", " entries");
    const QString searchFolder = QString::fromStdString(dir) + QStringLiteral("\\search");
    const int searches = QDir(searchFolder).entryList({QStringLiteral("*.json")}, QDir::Files).size();
    line += text("\n搜索缓存", "\nSearch cache") + text(" · ", " · ") + QString::number(searches) +
            text(" 条", " queries");
    if (!m_probeNote.isEmpty())
        line += QStringLiteral("\n") + m_probeNote;
    return line;
}

QString Controller::dataPath() const
{
    return QString::fromStdString(platform::pluginDataDir());
}

void Controller::copyDataPath()
{
    const QString path = dataPath();
    if (path.isEmpty()) {
        m_copyNotice = text("数据文件夹不可用。", "The data folder is unavailable.");
    } else {
        copyUsingHost(path, text("已复制数据文件夹路径。", "Copied the data-folder path."));
        return;
    }
    emit copyNoticeChanged();
}

void Controller::openDataFolder()
{
    const QString path = dataPath();
    if (path.isEmpty()) {
        m_copyNotice = text("数据文件夹不可用。", "The data folder is unavailable.");
        emit copyNoticeChanged();
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        m_copyNotice = text("无法打开数据文件夹。", "Could not open the data folder.");
        emit copyNoticeChanged();
    }
}

void Controller::clearCache()
{
    const std::string dir = platform::pluginDataDir();
    if (!dir.empty()) {
        QFile::remove(QString::fromStdString(dir + "\\cache.json"));
        QFile::remove(QString::fromStdString(dir + "\\info.json"));
        QFile::remove(QString::fromStdString(dir + "\\probe.json"));
        QFile::remove(QString::fromStdString(dir + "\\pins.json"));
        clearSearchCache();
    }
    m_installed = Snapshot{};
    m_updates = Snapshot{};
    m_wingetInstalled = Snapshot{};
    m_scoopInstalled = Snapshot{};
    m_wingetUpdates = Snapshot{};
    m_scoopUpdates = Snapshot{};
    m_search = Snapshot{};
    m_lastRefresh = QDateTime();
    m_infoStore = info_cache::Store{};
    // "Clear cache" also means "forget what we concluded about the backends":
    // the next scan should ask them rather than reuse a verdict.
    wingetAdapter().invalidateProbe();
    scoopAdapter().invalidateProbe();
    wingetAdapter().invalidatePins();
    // Empty snapshots show the honest "not scanned yet" state pane; the refresh
    // button (or the next window open) rebuilds everything.
    rebuildRows();
    emit statusChanged();
}

void Controller::loadCache()
{
    // A resident plugin is not a long-lived process: the host may end it between
    // window opens. Reading the last successful scan from the data folder is
    // what makes the first window paint rows instead of an empty list, and what
    // lets openWindow skip the probe at all.
    const QString path = cachePath();
    if (path.isEmpty())
        return;
    std::string text;
    if (!platform::readTextFile(toString(path), text))
        return;
    Snapshot cached;
    std::string error;
    Snapshot cachedUpdates;
    if (!cache::parse(text, cached, error, &cachedUpdates)) {
        // A cache this build cannot read is not an error worth showing: it is
        // simply ignored and the next refresh replaces it.
        return;
    }
    m_installed = cached;
    // Split the stored rows back into per-backend parts: a partial refresh must
    // keep the rows of the backend it did not query, and the cached timestamps
    // say which of the two is actually old.
    const auto partOf = [](const Snapshot& whole, Manager manager) {
        Snapshot part;
        part.winget = whole.winget;
        part.wingetDetail = whole.wingetDetail;
        part.scoop = whole.scoop;
        part.scoopDetail = whole.scoopDetail;
        part.observedAt = whole.observedAt;
        part.wingetObservedAt = whole.wingetObservedAt;
        part.scoopObservedAt = whole.scoopObservedAt;
        part.scanScope = whole.scanScope;
        for (const auto& p : whole.packages)
            if (p.key.manager == manager)
                part.packages.push_back(p);
        return part;
    };
    m_wingetInstalled = partOf(cached, Manager::Winget);
    m_scoopInstalled = partOf(cached, Manager::Scoop);
    m_wingetInstalled.wingetObservedAt = cached.wingetObservedAt;
    m_scoopInstalled.scoopObservedAt = cached.scoopObservedAt;
    // The update listing is restored too: it is the most expensive query in the
    // scan, and without it every restart showed an empty Updates tab that had to
    // be waited for again before it could be used.
    if (!cachedUpdates.observedAt.empty()) {
        m_updates = cachedUpdates;
        m_wingetUpdates = partOf(cachedUpdates, Manager::Winget);
        m_scoopUpdates = partOf(cachedUpdates, Manager::Scoop);
    }
    // The cache records each backend's state, so the probe is known without
    // running one. Without this a warm launch leaves both probes Unknown, and a
    // search — which refuses while the probe is Unknown — would be deferred
    // forever, because openWindow() skips the scan when the cache is fresh.
    m_wingetProbe.state = cached.winget;
    m_wingetProbe.detail = cached.wingetDetail;
    m_scoopProbe.state = cached.scoop;
    m_scoopProbe.detail = cached.scoopDetail;
    // The stored time is what decides freshness; without it the cache would look
    // fresh forever.
    m_lastRefresh = QDateTime::fromString(QString::fromStdString(cached.observedAt), Qt::ISODate);
    if (!m_lastRefresh.isValid())
        m_lastRefresh = QDateTime();
    rebuildRows();
    emit statusChanged();
}

void Controller::saveCache()
{
    if (!cache::worthWriting(m_installed))
        return;
    const QString path = cachePath();
    if (path.isEmpty())
        return;
    // The update listing is written in the same file: both snapshots come from one
    // scan, and a second file could be left behind by a crash between the two writes.
    platform::writeTextFile(toString(path), cache::serialize(m_installed, &m_updates));
}

void Controller::shutdown()
{
    // Nothing is replayed. Anything active is recorded as interrupted so the next
    // session shows the truth instead of a stuck "running" row.
    m_queue.markInterrupted();
    if (m_tasks)
        m_tasks->setTasks(m_queue.tasks());
    emit tasksChanged();
    m_windowOpen = false;
    emit windowShouldClose();
}

void Controller::refreshAll() { startBackendRefresh(true); }

bool Controller::backendNeedsScan(const std::string& observedAt, BackendState state) const
{
    // A backend that did not end Ready is always retried: that answer depends on
    // the environment of this process, and keeping it for refreshMinutes would
    // show a failure the current process might not repeat.
    if (state != BackendState::Ready)
        return true;
    if (observedAt.empty())
        return true;
    return !infoFresh(observedAt);
}

void Controller::startBackendRefresh(bool force)
{
    // The harness's deterministic mode must not start a backend: the whole point is
    // that the window's content comes from an injected snapshot, and a real scan
    // would replace it (and report "Querying…" over the surface being captured).
    if (m_fakeMode)
        return;
    // Backend calls block for seconds (winget list alone can take tens of
    // seconds), so they must never run on the GUI thread: a refresh here would
    // freeze the window and delay shutdown past the host's timeout. The queries
    // touch no QObject, so a plain worker thread is safe; results are posted back
    // for the GUI thread to apply.
    if (m_refreshInFlight)
        return;

    // Only the backends that are actually stale are queried. A failing Scoop must
    // not force a full WinGet rescan (its upgrade listing is the most expensive
    // query here), and vice versa.
    const bool scanWinget = force ||
        backendNeedsScan(m_wingetInstalled.wingetObservedAt, m_installed.winget);
    const bool scanScoop = force ||
        backendNeedsScan(m_scoopInstalled.scoopObservedAt, m_installed.scoop);
    if (!scanWinget && !scanScoop)
        return;

    m_refreshInFlight = true;
    m_busy = true;
    m_scanStage.clear();
    emit statusChanged();

    // A finished mutation may have changed the scoop inventory; the per-scan
    // caches are dropped here so the parallel queries within one scan share a
    // fetch but nothing is reused across scans. Pins and probes survive: they
    // describe policy and the binary, not this scan's rows.
    wingetAdapter().invalidateCaches();
    scoopAdapter().invalidateCaches();

    auto* self = this;
    // Stage announcements ride the same marshal-back path as the results: a
    // QString written from the worker while the GUI reads it would be a race.
    const auto stage = [self](const char* zh, const char* en) {
        QMetaObject::invokeMethod(self, [self, zh, en] {
            self->m_scanStage = self->text(zh, en);
            emit self->statusChanged();
        }, Qt::QueuedConnection);
    };
    spawnWorker([self, stage, scanWinget, scanScoop] {
        // The two backends are independent: winget's list takes seconds on its
        // own, and running scoop after it doubles the wait for no benefit. Each
        // runs on its own thread, and the results are joined before marshalling
        // back to the GUI thread. A backend that is not being refreshed leaves
        // empty results behind, and applyRefreshResults keeps its previous part.
        Snapshot wingetInstalled, scoopInstalled, wingetUpdates, scoopUpdates;
        ProbeResult wingetProbe, scoopProbe;
        std::thread wingetThread([&] {
            if (!scanWinget)
                return;
            stage("WinGet 已安装列表", "WinGet installed list");
            wingetProbe = wingetAdapter().probeCached();
            if (wingetProbe.state == BackendState::Ready) {
                wingetInstalled = wingetAdapter().readInstalled(wingetProbe, self->m_settings.showObserved);
                stage("WinGet 更新列表", "WinGet update list");
                wingetUpdates = wingetAdapter().readUpdates(wingetProbe);
            }
        });
        std::thread scoopThread([&] {
            if (!scanScoop)
                return;
            stage("Scoop 已安装列表", "Scoop installed list");
            scoopProbe = scoopAdapter().probeCached();
            if (scoopProbe.state == BackendState::Ready)
                scoopInstalled = scoopAdapter().readInstalled(scoopProbe, self->m_settings.showObserved);
        });
        // The scoop update listing reads bucket manifests; it runs on this
        // thread so it does not wait behind the export call of the thread above.
        wingetThread.join();
        scoopThread.join();
        if (scanScoop && scoopProbe.state == BackendState::Ready) {
            stage("Scoop 更新列表", "Scoop update list");
            scoopUpdates = scoopAdapter().readUpdates(scoopProbe);
        }

        // The GUI thread owns the controller; marshal the results.
        QMetaObject::invokeMethod(
            self,
            [self, wingetProbe, scoopProbe, wingetInstalled, scoopInstalled, wingetUpdates, scoopUpdates,
             scanWinget, scanScoop] {
                self->applyRefreshResults(wingetProbe, scoopProbe, wingetInstalled, scoopInstalled,
                                          wingetUpdates, scoopUpdates, scanWinget, scanScoop);
            },
            Qt::QueuedConnection);
    });
}

void Controller::applyRefreshResults(const ProbeResult& wingetProbe, const ProbeResult& scoopProbe,
                                     const Snapshot& wingetInstalled, const Snapshot& scoopInstalled,
                                     const Snapshot& wingetUpdates, const Snapshot& scoopUpdates,
                                     bool scannedWinget, bool scannedScoop)
{
    m_refreshInFlight = false;
    m_scanStage.clear();

    const std::string now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    if (scannedWinget) {
        m_wingetProbe = wingetProbe;
        m_wingetInstalled = wingetInstalled;
        m_wingetUpdates = wingetUpdates;
        // Only a scan that actually saw a ready backend counts as fresh: a denied
        // probe must not suppress the next attempt.
        if (wingetInstalled.winget == BackendState::Ready)
            m_wingetInstalled.wingetObservedAt = now;
        m_wingetInstalled.winget = wingetProbe.state;
        if (m_wingetInstalled.wingetDetail.empty())
            m_wingetInstalled.wingetDetail = wingetProbe.detail;
        m_wingetUpdates.wingetObservedAt = m_wingetInstalled.wingetObservedAt;
        m_wingetUpdates.winget = wingetProbe.state;
        if (m_wingetUpdates.wingetDetail.empty())
            m_wingetUpdates.wingetDetail = wingetProbe.detail;
    }
    if (scannedScoop) {
        m_scoopProbe = scoopProbe;
        m_scoopInstalled = scoopInstalled;
        m_scoopUpdates = scoopUpdates;
        if (scoopInstalled.scoop == BackendState::Ready)
            m_scoopInstalled.scoopObservedAt = now;
        m_scoopInstalled.scoop = scoopProbe.state;
        if (m_scoopInstalled.scoopDetail.empty())
            m_scoopInstalled.scoopDetail = scoopProbe.detail;
        m_scoopUpdates.scoopObservedAt = m_scoopInstalled.scoopObservedAt;
        m_scoopUpdates.scoop = scoopProbe.state;
        if (m_scoopUpdates.scoopDetail.empty())
            m_scoopUpdates.scoopDetail = scoopProbe.detail;
    }

    if (m_wingetInstalled.winget == BackendState::Ready || m_scoopInstalled.scoop == BackendState::Ready)
        m_lastRefresh = QDateTime::currentDateTime();

    publishInventory();
}

void Controller::publishInventory()
{
    // The two inventories are concatenated, never merged: identical names in
    // different managers stay separate rows.
    Snapshot combined;
    combined.winget = m_wingetInstalled.winget;
    combined.wingetDetail = m_wingetInstalled.wingetDetail;
    combined.scoop = m_scoopInstalled.scoop;
    combined.scoopDetail = m_scoopInstalled.scoopDetail;
    combined.wingetObservedAt = m_wingetInstalled.wingetObservedAt;
    combined.scoopObservedAt = m_scoopInstalled.scoopObservedAt;
    combined.observedAt = m_wingetInstalled.wingetObservedAt.empty()
                              ? m_scoopInstalled.scoopObservedAt
                              : m_wingetInstalled.wingetObservedAt;
    combined.scanScope = "installed";
    combined.packages.insert(combined.packages.end(), m_wingetInstalled.packages.begin(),
                             m_wingetInstalled.packages.end());
    combined.packages.insert(combined.packages.end(), m_scoopInstalled.packages.begin(),
                             m_scoopInstalled.packages.end());
    m_installed = combined;

    // `winget list` and `scoop export` carry no target version, so an installed
    // row alone can never offer an upgrade. The update listing does know it, so
    // its target is copied onto the matching installed row by exact identity.
    // Without this an installed row is removable but never upgradable, and the
    // checkbox on it stays disabled whenever the row has no other action.
    for (auto& installed : m_installed.packages) {
        for (const auto& updated : m_wingetUpdates.packages) {
            if (installed.key == updated.key && !updated.availableVersion.empty()) {
                installed.availableVersion = updated.availableVersion;
                installed.availableKind = updated.availableKind;
                installed.canUpgrade = updated.canUpgrade;
            }
        }
        for (const auto& updated : m_scoopUpdates.packages) {
            if (installed.key == updated.key && !updated.availableVersion.empty()) {
                installed.availableVersion = updated.availableVersion;
                installed.availableKind = updated.availableKind;
                installed.canUpgrade = updated.canUpgrade;
            }
        }
    }

    // The Updates tab gets its own snapshot: rows there carry a target version
    // the installed listing does not know (winget's upgrade list, scoop's bucket
    // manifests), so reusing m_installed would show nothing.
    Snapshot updates;
    updates.winget = m_wingetUpdates.winget;
    updates.wingetDetail = m_wingetUpdates.wingetDetail;
    updates.scoop = m_scoopUpdates.scoop;
    updates.scoopDetail = m_scoopUpdates.scoopDetail;
    updates.wingetObservedAt = m_wingetUpdates.wingetObservedAt;
    updates.scoopObservedAt = m_scoopUpdates.scoopObservedAt;
    updates.observedAt = m_wingetUpdates.observedAt.empty() ? m_scoopUpdates.observedAt : m_wingetUpdates.observedAt;
    updates.scanScope = "updates";
    updates.packages.insert(updates.packages.end(), m_wingetUpdates.packages.begin(), m_wingetUpdates.packages.end());
    updates.packages.insert(updates.packages.end(), m_scoopUpdates.packages.begin(), m_scoopUpdates.packages.end());
    m_updates = updates;

    // Entries for packages that are no longer installed describe a machine
    // that no longer exists; drop them so the cache cannot outlive the
    // inventory it was built from. (Entries for packages whose version moved
    // are dropped by onTaskFinished, the moment the mutation ends.)
    if (!m_infoStore.entries.empty()) {
        std::vector<std::string> installedLower;
        installedLower.reserve(m_installed.packages.size());
        for (const auto& p : m_installed.packages)
            installedLower.push_back(lowerAscii(p.key.id));
        bool changed = false;
        for (auto it = m_infoStore.entries.begin(); it != m_infoStore.entries.end();) {
            const bool stillThere =
                std::find(installedLower.begin(), installedLower.end(), lowerAscii(it->id)) !=
                installedLower.end();
            if (stillThere) {
                ++it;
                continue;
            }
            it = m_infoStore.entries.erase(it);
            changed = true;
        }
        if (changed)
            saveInfoCache();
    }

    // Capture the selection by exact id before the rows are replaced; the
    // restore right after rebuildRows puts it back where it still exists.
    m_checkedIdsBeforeRefresh.clear();
    m_selectedIdBeforeRefresh.clear();
    if (m_packages) {
        const auto& rows = m_packages->rows();
        const int selected = m_packages->selected();
        if (selected >= 0 && selected < static_cast<int>(rows.size()))
            m_selectedIdBeforeRefresh = lowerAscii(rows[static_cast<std::size_t>(selected)].key.id);
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            if (m_packages->isChecked(i))
                m_checkedIdsBeforeRefresh.push_back(lowerAscii(rows[static_cast<std::size_t>(i)].key.id));
        }
    }

    m_busy = false;
    rebuildRows();
    // A refresh replaces every row, but the user's selection should survive it:
    // restore the selected row and the checked ones by exact id (case
    // insensitively — the backends may recase an id between scans). Rows that
    // vanished are simply not restored.
    if (m_packages && (!m_checkedIdsBeforeRefresh.empty() || !m_selectedIdBeforeRefresh.empty())) {
        const auto& rows = m_packages->rows();
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            const std::string id = lowerAscii(rows[static_cast<std::size_t>(i)].key.id);
            const bool wanted =
                std::find(m_checkedIdsBeforeRefresh.begin(), m_checkedIdsBeforeRefresh.end(), id) !=
                m_checkedIdsBeforeRefresh.end();
            if (wanted)
                m_packages->toggleChecked(i);
            if (id == m_selectedIdBeforeRefresh)
                m_packages->setSelected(i);
        }
        emit selectionChanged();
    }
    m_checkedIdsBeforeRefresh.clear();
    m_selectedIdBeforeRefresh.clear();
    // The scan is now the newest truth on this machine: persist it so the next
    // process launch can paint immediately instead of re-querying.
    saveCache();
    // The verdict about the binaries is what the next launch would otherwise
    // spend two processes re-learning.
    saveProbeCache();
    // The pin list, if this scan had to read it.
    savePinsCache();
    emit statusChanged();

    // A search typed before the first scan finished was deferred because the
    // probe was still Unknown; both adapters refuse in that state, so the result
    // would have been empty with nothing to retry it. The probe is known now.
    if (m_tab == "discover" && !m_searchText.isEmpty() && m_search.packages.empty() &&
        !m_searchInFlight)
        startSearchWorker();
}

bool Controller::isInstalled(const PackageKey& key) const
{
    // Matched by manager + exact id, case-insensitively, and deliberately NOT by
    // source or scope: `winget list` and `winget search` fill those differently
    // (list leaves the source empty for an ARP entry and always reports scope
    // "unknown"), so a full-key comparison reported every installed package as
    // absent and "Not installed" showed everything. The id is what identifies a
    // package; the source is metadata about where it came from.
    const QString wantedId = QString::fromStdString(key.id).toLower();
    for (const auto& p : m_installed.packages) {
        if (p.key.manager != key.manager)
            continue;
        if (QString::fromStdString(p.key.id).toLower() == wantedId)
            return true;
    }
    return false;
}

void Controller::rebuildRows()
{    if (!m_packages)
        return;
    Tab tab = Tab::Installed;
    if (m_tab == "discover")
        tab = Tab::Discover;
    else if (m_tab == "updates")
        tab = Tab::Updates;

    // Each tab has its own data source and its own filter semantics. Sharing one
    // snapshot made the search text bleed into the other tabs: the installed and
    // updates lists would be filtered by a query that belongs to Discover.
    const Snapshot* source = &m_installed;
    std::string filter;
    if (tab == Tab::Discover) {
        // Only Discover is a search: its snapshot is the search result and the
        // typed words narrow it further.
        if (!m_search.packages.empty()) {
            source = &m_search;
            filter = toString(m_searchText);
        } else {
            // No query yet: Discover has nothing to show. It used to fall back to
            // the installed inventory, which made the tab look like a duplicate of
            // Installed and hid the fact that no search had run.
            source = &m_search;
        }
    } else if (tab == Tab::Updates) {
        // The update listing already carries the target version; filtering it by
        // the search box would hide rows for a query the user typed elsewhere.
        source = &m_updates;
    }
    std::vector<Package> rows = rowsForTab(*source, tab, filter, Manager::Unknown);
    // Search results are separate from the installed inventory, but the same
    // package can appear in both (for example Neovim is returned by `winget
    // search` and `winget list`). Decorate each Discover row with the installed
    // facts by manager + case-insensitive exact id. Keep the search row's source
    // and key: they identify the install candidate; only installed state and
    // capabilities come from the local inventory.
    if (tab == Tab::Discover) {
        for (auto& row : rows) {
            const auto installed = std::find_if(m_installed.packages.begin(), m_installed.packages.end(),
                                                [&row](const Package& candidate) {
                                                    return candidate.key.manager == row.key.manager &&
                                                           QString::fromStdString(candidate.key.id).compare(
                                                               QString::fromStdString(row.key.id),
                                                               Qt::CaseInsensitive) == 0;
                                                });
            if (installed == m_installed.packages.end())
                continue;
            row.installedVersion = installed->installedVersion;
            row.provenance = installed->provenance;
            row.canInstall = false;
            row.canUninstall = installed->canUninstall;
            row.availableVersion = installed->availableVersion;
            row.availableKind = installed->availableKind;
            row.canUpgrade = installed->canUpgrade;
            row.limits = installed->limits;
        }
    }
    // One manager selection: "all" keeps both, otherwise only that one.
    if (m_managerFilter == QStringLiteral("scoop") || m_managerFilter == QStringLiteral("winget")) {
        const Manager wanted = m_managerFilter == QStringLiteral("scoop") ? Manager::Scoop
                                                                        : Manager::Winget;
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [wanted](const Package& p) { return p.key.manager != wanted; }),
                   rows.end());
    }
    // Installed rows are not all the same kind of thing: a manager-owned package
    // is one the backend records as its own, while an observed ARP/MSIX entry is
    // software the user installed outside any package manager. Filtering between
    // them is what makes "which of these can this plugin actually manage" a
    // question the list can answer.
    if (tab == Tab::Installed && m_sourceFilter != QStringLiteral("all")) {
        const bool wantManaged = m_sourceFilter == QStringLiteral("managed");
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [wantManaged](const Package& p) {
                                      const bool managed = p.provenance == Provenance::BackendRecorded;
                                      return managed != wantManaged;
                                  }),
                   rows.end());
    }
    // Discover results can be narrowed by whether the package is already
    // installed. The default hides installed ones: the point of searching is to
    // find something new, and a result list that repeats the installed inventory
    // makes the tab look broken.
    if (tab == Tab::Discover && m_discoverFilter != QStringLiteral("all")) {
        const bool wantInstalled = m_discoverFilter == QStringLiteral("installed");
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [this, wantInstalled](const Package& p) {
                                      const bool installed = isInstalled(p.key);
                                      return installed != wantInstalled;
                                  }),
                   rows.end());
    }
    // The Installed tab's own box narrows what is already loaded. Applied here so
    // it works on the snapshot rather than on a second copy of the rows.
    if (tab == Tab::Installed && !m_installedFilter.isEmpty()) {
        const QString needle = m_installedFilter.toLower();
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [&needle](const Package& p) {
                                      const QString name = QString::fromStdString(p.name).toLower();
                                      const QString id = QString::fromStdString(p.key.id).toLower();
                                      return !name.contains(needle) && !id.contains(needle);
                                  }),
                   rows.end());
    }
    m_packages->setRows(rows);
    rebuildDuplicates();

    // A backend that is not Ready gets its own state pane rather than an empty
    // list that would read as "nothing installed". Both filters off is its own
    // state too: the user hid the managers, nothing is wrong with them.
    if (m_installed.winget == BackendState::Missing && m_installed.scoop == BackendState::Missing) {
        setStatePane("backend-missing", text("两个包管理器都不可用。本插件不会下载或安装包管理器。",
                                            "Neither package manager is available. This plugin never downloads or "
                                            "installs one."));
    } else if (m_installed.winget == BackendState::Unsupported || m_installed.scoop == BackendState::Unsupported) {
        setStatePane("unsupported", QString::fromStdString(m_installed.winget == BackendState::Unsupported
                                                              ? m_installed.wingetDetail
                                                              : m_installed.scoopDetail));
    } else if (tab == Tab::Discover && m_searchText.isEmpty()) {
        // Discover is a search. Saying so is better than showing an empty list
        // that reads as "nothing found".
        setStatePane("discover-idle",
                     text("输入关键词开始搜索。发现页只显示搜索结果，不重复已安装清单。",
                          "Type a keyword to search. Discover shows search results only; it does not repeat "
                          "the installed inventory."));
    } else if (tab == Tab::Discover && m_discoverFilter == QStringLiteral("new") &&
               m_packages && m_packages->rowCount() == 0 && !m_search.packages.empty()) {
        setStatePane("discover-all-installed",
                     text("所有匹配的包都已安装。把「结果」切到「全部」可以看到它们。",
                          "Every matching package is already installed. Switch 结果/Result to 全部/All to "
                          "see them."));
    } else {
        setStatePane(QString(), QString());
    }
}

void Controller::rebuildDuplicates()
{
    if (!m_packages)
        return;
    // Build a snapshot that mirrors exactly the rows the model holds, so a hint's
    // index refers to the same row the user is looking at.
    Snapshot view;
    view.packages = m_packages->rows();
    std::vector<std::vector<std::size_t>> hints(view.packages.size());
    for (std::size_t i = 0; i < view.packages.size(); ++i)
        hints[i] = duplicateHints(view, i);
    m_packages->setDuplicateHints(hints);
}

void Controller::setStatePane(const QString& pane, const QString& why)
{
    m_statePane = pane;
    m_stateWhy = why;
    emit statePaneChanged();
}

QString Controller::stateTitle() const
{
    if (m_statePane == "backend-missing")
        return text("未找到包管理器", "No package manager found");
    if (m_statePane == "unsupported")
        return text("不支持的输出格式", "Unsupported output");
    return {};
}

QString Controller::stateBody() const
{
    if (m_statePane == "backend-missing") {
        return text("另一个管理器不可用的部分会单独说明。插件不会下载包管理器，也不会自动安装 PowerShell 模块。",
                    "The part of the other manager that is unavailable is reported separately. The plugin never "
                    "downloads a package manager or installs PowerShell modules.");
    }
    if (m_statePane == "unsupported")
        return m_stateWhy;
    return {};
}

QString Controller::statusLine() const
{
    // A search is what the user just did, so it outranks the background scan: the
    // scan may still be running from the window opening, and reporting "Querying…"
    // while the search box spins describes the wrong operation.
    if (m_tab == "discover" && (m_searchPending || m_searchInFlight))
        return text("正在搜索…（Scoop 与 WinGet 并行查询）",
                    "Searching… (Scoop and WinGet in parallel)");
    if (m_busy) {
        // A scan runs four queries; the stage says which one is running so
        // tens of seconds of work never look stuck.
        return m_scanStage.isEmpty() ? text("正在查询…", "Querying…")
                                     : text("正在刷新 · ", "Refreshing · ") + m_scanStage + "…";
    }
    // The tasks page reports the queue, not a query: "read-only query complete"
    // there describes the wrong thing.
    if (m_tab == "tasks") {
        const int active = activeTasks();
        // "Queue running" next to the header's "idle · 0 active" claimed two
        // different things about one page. The queue state word is only meaningful
        // when there is something in it.
        QString line = m_queue.paused() ? text("队列已暂停", "Queue paused")
                                        : text("队列空闲", "Queue idle");
        if (active > 0)
            line = m_queue.paused() ? text("队列已暂停", "Queue paused") : text("队列运行中", "Queue running");
        line += QStringLiteral(" · ") + QString::number(active) + text(" 个进行中", " active");
        const int done = finishedTasks();
        if (done > 0)
            line += QStringLiteral(" · ") + QString::number(done) + text(" 个已结束", " finished");
        // Exit code 0 is never the verdict; the footer repeats the rule where a
        // result is being read.
        line += text(" · 退出码 0 不等于成功", " · exit code 0 is not success");
        if (!m_selectedTaskLabel.isEmpty())
            line += text(" · 已选 ", " · selected ") + m_selectedTaskLabel;
        return line;
    }
    // Only a pane that reports a failed query means the query is limited.
    // "discover-idle" and "discover-all-installed" are ordinary answers, and
    // reporting them as 查询受限/Query limited described a healthy window as a
    // broken one — which is what made a restarted window look limited.
    if (m_statePane == "backend-missing" || m_statePane == "unsupported")
        return text("查询受限", "Query limited");
    if (m_tab == "discover" && m_searchText.isEmpty())
        return text("尚未搜索", "Not searched yet");
    if (m_tab == "discover" && m_statePane == "discover-all-installed")
        return text("搜索完成 · 匹配项全部已安装", "Search finished · every match is installed");
    if (m_tab == "discover") {
        const int n = m_packages ? m_packages->rowCount() : 0;
        return n == 0 ? text("搜索完成 · 0 个结果", "Search finished · 0 results")
                      : text("搜索完成 · ", "Search finished · ") + QString::number(n) +
                            text(" 个结果", " results");
    }
    return text("只读查询完成", "Read-only query complete");
}

QString Controller::scanLine() const
{
    // The Updates tab has its own listing with its own timestamp; showing the
    // installed scan time there would claim the update list was younger or older
    // than it is.
    const Snapshot& snapshot = m_tab == "updates" ? m_updates : m_installed;
    if (snapshot.observedAt.empty())
        return text("尚未扫描", "not scanned yet");
    return text("扫描时间 ", "Scan ") + QString::fromStdString(snapshot.observedAt);
}

QString Controller::backendLine() const
{
    const auto part = [&](const char* label, BackendState state, const std::string& detail) {
        QString out = QString::fromUtf8(label) + " " + backendStateText(state, chinese());
        if (state != BackendState::Ready && !detail.empty())
            out += " (" + QString::fromStdString(detail) + ")";
        return out;
    };
    return part("WinGet", m_installed.winget, m_installed.wingetDetail) + QStringLiteral("  ·  ") +
           part("Scoop", m_installed.scoop, m_installed.scoopDetail);
}

int Controller::checkedCount() const { return m_packages ? m_packages->checkedCount() : 0; }

QString Controller::selectionSummary() const
{
    if (!m_packages)
        return QString();
    int upgradable = 0;
    int installed = 0;
    int installable = 0;
    const auto& rows = m_packages->rows();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (!m_packages->isChecked(i))
            continue;
        const auto& p = rows[static_cast<std::size_t>(i)];
        if (!p.installedVersion.empty())
            ++installed;
        else if (p.canInstall)
            ++installable;
        if (!p.installedVersion.empty() && p.canUpgrade && !p.availableVersion.empty() &&
            p.availableVersion != p.installedVersion)
            ++upgradable;
    }
    // Only the parts that apply. A selection of search results is all "installable",
    // and reporting "0 upgradable · 0 installed" for it reads as a broken counter.
    QStringList parts;
    if (upgradable > 0)
        parts << text("%1 个可升级", "%1 upgradable").arg(upgradable);
    if (installed > 0)
        parts << text("%1 个已安装（可卸载）", "%1 installed (removable)").arg(installed);
    if (installable > 0)
        parts << text("%1 个可安装", "%1 installable").arg(installable);
    return parts.isEmpty() ? text("选中的项没有可用操作", "Nothing can be done with the selection")
                           : parts.join(QStringLiteral(" · "));
}

int Controller::activeTasks() const
{
    int n = 0;
    for (const auto& t : m_queue.tasks())
        if (isActive(t.state))
            ++n;
    return n;
}

QString Controller::selectedTaskLabel() const
{
    return m_selectedTaskLabel;
}

void Controller::setSelectedTask(qulonglong id)
{
    m_selectedTaskId = id;
    m_selectedTaskLabel.clear();
    if (id != 0) {
        for (const auto& t : m_queue.tasks()) {
            if (t.id == id) {
                m_selectedTaskLabel = QString::fromStdString(t.plan.key.id);
                break;
            }
        }
    }
    emit tasksChanged();
    emit statusChanged();
}

int Controller::finishedTasks() const
{
    int n = 0;
    for (const auto& t : m_queue.tasks())
        if (isTerminal(t.state))
            ++n;
    return n;
}

QString Controller::reviewGateText() const
{
    if (reviewBatchCount() > 1) {
        // The batch gate speaks about the list: what the confirm will do, and
        // what it will not.
        int ready = 0, blocked = 0;
        for (const auto& plan : m_reviewPlans)
            plan.state == PlanState::Ready ? ++ready : ++blocked;
        if (ready == 0)
            return text("全部阻塞 → 确认按钮保持禁用", "Every row is blocked → confirm stays disabled");
        if (!m_reviewConfirmEnabled)
            return m_needsSourceAgreement && !m_sourceAgreed
                       ? text("源协议未同意 → 确认按钮保持禁用",
                              "Source agreement not accepted → confirm stays disabled")
                       : text("影响未确认 → 确认按钮保持禁用",
                              "Impact not acknowledged → confirm stays disabled");
        return text("将逐行入队 %1 个任务（%2 行阻塞，不会执行）",
                    "Will queue %1 tasks one by one (%2 blocked rows are not run)")
            .arg(ready)
            .arg(blocked);
    }
    if (m_reviewPlan.state == PlanState::Blocked)
        return text("阻塞", "BLOCKED");
    if (m_reviewConfirmEnabled)
        return text("预检通过，协议已逐项明确勾选", "Preflight passed; agreements explicitly checked one by one");
    if (m_needsSourceAgreement && !m_sourceAgreed)
        return text("源协议未同意 → 确认按钮保持禁用", "Source agreement not accepted → confirm stays disabled");
    if (!m_installerAgreed) {
        return isUninstallReview()
                   ? text("未确认影响 → 确认按钮保持禁用", "Impact not acknowledged → confirm stays disabled")
                   : text("安装器协议未同意 → 确认按钮保持禁用",
                          "Installer agreement not accepted → confirm stays disabled");
    }
    return text("预检未完成 → 确认按钮保持禁用", "Preflight incomplete → confirm stays disabled");
}

bool Controller::isUninstallReview() const
{
    return m_reviewAction.compare(QStringLiteral("UNINSTALL"), Qt::CaseInsensitive) == 0;
}

QString Controller::sourceAgreementText() const
{
    return text("源协议：只对当前源生效；拒绝则阻塞该源的请求，且不会记作对所有源同意。",
                "Source agreement: applies to this source only; declining blocks requests for it and is never "
                "recorded as consent for every source.");
}

QString Controller::installerAgreementText() const
{
    // Uninstall has no installer to agree with: the only thing to acknowledge is
    // what removing the package does.
    if (isUninstallReview())
        return text("卸载会移除本机软件，可能删除用户数据，且不会自动重装。",
                    "Uninstall removes local software and may delete user data; nothing is reinstalled "
                    "automatically.");
    return text("安装器协议：由安装器自己呈现；拒绝则不执行安装，与源协议分开同意。",
                "Installer agreement: presented by the installer itself; declining means no install, and it is "
                "agreed separately from the source agreement.");
}

void Controller::setTab(const QString& tab)
{
    if (m_tab == tab)
        return;
    m_tab = tab;
    // Each tab owns its own query state. Leaving Discover must not clear its
    // search text or result snapshot: returning to it should restore the same
    // results, and an in-flight query can still finish into the cached snapshot.
    // setSearchText("") remains the explicit way to clear Discover's query.
    emit tabChanged();
    // The status line's content depends on the tab (the tasks page reports the
    // queue), so switching tabs must re-emit it or the footer keeps describing
    // the page that is no longer shown.
    emit statusChanged();
    emit searchStateChanged();
    // The selection belongs to the rows of the tab it was made on. Keeping it
    // across a tab switch left the details pane describing a row that is no
    // longer in the list.
    if (m_packages) {
        m_packages->setSelected(-1);
        m_packages->clearChecked();
        emit selectionChanged();
    }
    rebuildRows();
}

void Controller::setBackendFilter(const QString& filter)
{
    setManagerFilter(filter);
}

void Controller::setManagerFilter(const QString& filter)
{
    // One of exactly three values. "all" is a real selection, not "both toggles
    // happen to be on": with the previous multi-select, "全部" was lit together
    // with Scoop and WinGet, so the highlight no longer said which filter was
    // active, and clicking 全部 could turn everything off.
    const QString normalized = (filter == "scoop" || filter == "winget") ? filter
                                                                        : QStringLiteral("all");
    if (m_managerFilter == normalized) {
        emit filterChanged();
        rebuildRows();
        return;
    }
    m_managerFilter = normalized;
    m_backendFilter = normalized;
    emit filterChanged();
    rebuildRows();
}

void Controller::setSourceFilter(const QString& filter)
{
    if (m_sourceFilter == filter)
        return;
    m_sourceFilter = filter;
    emit filterChanged();
    rebuildRows();
}

void Controller::setDiscoverFilter(const QString& filter)
{
    if (m_discoverFilter == filter)
        return;
    m_discoverFilter = filter;
    emit filterChanged();
    rebuildRows();
}

void Controller::setInstalledFilter(const QString& text)
{
    if (m_installedFilter == text)
        return;
    m_installedFilter = text;
    emit filterChanged();
    rebuildRows();
}

void Controller::setSearchText(const QString& value)
{
    m_searchText = value;
    emit filterChanged();
    // Searching is a query: it never installs and never touches a plan. It also
    // takes seconds per keystroke (a winget search spawns a process), so it runs
    // on a worker: the GUI thread only records the text and rebuilds what the
    // installed cache can already show.
    if (m_tab == "discover" && !value.isEmpty()) {
        m_searchPending = true;
        // The rows about to be replaced are not the rows the details pane is
        // describing, so the selection is dropped rather than left pointing at a
        // package that is no longer in the list.
        if (m_packages) {
            m_packages->setSelected(-1);
            m_packages->clearChecked();
            emit selectionChanged();
        }
        emit statusChanged();
        emit searchStateChanged();
        startSearchWorker();
    } else if (value.isEmpty()) {
        m_searchPending = false;
        m_search = Snapshot{};
        emit searchStateChanged();
    }
    rebuildRows();
}

void Controller::applyFakeSearch(const QString& needle, const Snapshot& wingetSearch,
                                 const Snapshot& scoopSearch)
{
    applySearchResults(needle, wingetSearch, scoopSearch);
}

void Controller::holdSearching(int ms)
{
    if (ms <= 0)
        return;
    m_searchPending = true;
    emit searchStateChanged();
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(ms);
    QObject::connect(timer, &QTimer::timeout, this, [this, timer] {
        m_searchPending = false;
        emit searchStateChanged();
        timer->deleteLater();
    });
    timer->start();
}

void Controller::applyFakeTasks(const std::vector<Task>& tasks)
{
    m_queue.setTasks(tasks);
    if (m_tasks)
        m_tasks->setTasks(m_queue.tasks());
    emit tasksChanged();
    emit statusChanged();
}

void Controller::applyFakeInventory(const std::vector<Package>& rows)
{
    Snapshot installed;
    installed.observedAt = toString(QDateTime::currentDateTime().toString(Qt::ISODate));
    installed.scanScope = "development harness (fake inventory)";
    installed.winget = BackendState::Ready;
    installed.scoop = BackendState::Ready;
    installed.packages = rows;

    // The Updates tab means "installed with a newer target", so derive it from the
    // rows instead of duplicating the list: a fake run that put every row there
    // would make the tab claim upgrades for packages that have none.
    Snapshot updates = installed;
    updates.packages.clear();
    for (const auto& p : rows) {
        if (p.canUpgrade)
            updates.packages.push_back(p);
    }
    updates.observedAt = installed.observedAt;

    m_installed = installed;
    m_updates = updates;
    m_wingetProbe.state = BackendState::Ready;
    m_scoopProbe.state = BackendState::Ready;
    m_wingetProbe.detail.clear();
    m_scoopProbe.detail.clear();
    m_lastRefresh = QDateTime::currentDateTime();
    m_refreshInFlight = false;
    m_busy = false;
    rebuildRows();
    emit statusChanged();
    emit filterChanged();
}
void Controller::startSearchWorker()
{
    if (m_fakeMode)
        return;
    if (m_searchInFlight)
        return;
    const QString needle = m_searchText;
    // A fresh answer for this exact query is already on disk: use it instead of
    // spending two processes. This runs before the probe check because a cached
    // answer needs no backend at all.
    if (applyCachedSearch(needle))
        return;
    // A search needs a probe that has already run: both adapters refuse while the
    // backend state is still Unknown, so starting one before the first scan
    // finishes produces an empty result that nothing retries. Defer instead, and
    // applyRefreshResults() starts it once the probe is known.
    if (m_wingetProbe.state == BackendState::Unknown &&
        m_scoopProbe.state == BackendState::Unknown) {
        m_searchPending = true;
        return;
    }
    m_searchInFlight = true;
    emit searchStateChanged();

    auto* self = this;
    spawnWorker([self, needle] {
        // Both searches are read-only and touch no QObject on this thread.
        Snapshot wingetSearch = wingetAdapter().search(self->m_wingetProbe, toString(needle), nullptr);
        Snapshot scoopSearch = scoopAdapter().search(self->m_scoopProbe, toString(needle), nullptr);
        QMetaObject::invokeMethod(
            self,
            [self, needle, wingetSearch, scoopSearch] {
                self->applySearchResults(needle, wingetSearch, scoopSearch);
            },
            Qt::QueuedConnection);
    });
}

void Controller::applySearchResults(const QString& needle, const Snapshot& wingetSearch,
                                    const Snapshot& scoopSearch)
{
    m_searchInFlight = false;
    // A newer keystroke replaced this one while the search ran: its result is
    // stale for the box the user sees, so drop it and start the newest query.
    if (needle != m_searchText) {
        if (m_searchPending && m_tab == "discover") {
            emit searchStateChanged();
            startSearchWorker();
        } else {
            // Nothing to restart, so this is the last state change. Leaving
            // m_searchPending set here was why the spinner kept turning after a
            // search had already finished: the flag drives `searching` and nothing
            // cleared it on this path.
            m_searchPending = false;
            emit searchStateChanged();
        }
        return;
    }
    // The user may switch tabs while the query is running. Keep this result in
    // the Discover snapshot even when another tab is visible; rebuildRows() uses
    // the active tab's snapshot, and returning to Discover then restores it.
    m_searchPending = false;
    emit searchStateChanged();
    m_search = Snapshot{};
    m_search.observedAt = wingetSearch.observedAt.empty() ? scoopSearch.observedAt : wingetSearch.observedAt;
    m_search.scanScope = "search";
    m_search.winget = wingetSearch.winget;
    m_search.wingetDetail = wingetSearch.wingetDetail;
    m_search.scoop = scoopSearch.scoop;
    m_search.scoopDetail = scoopSearch.scoopDetail;
    m_search.packages.insert(m_search.packages.end(), wingetSearch.packages.begin(), wingetSearch.packages.end());
    m_search.packages.insert(m_search.packages.end(), scoopSearch.packages.begin(), scoopSearch.packages.end());
    // Persist the answer so the same query next time is a file read.
    saveSearchCache(needle, m_search);
    rebuildRows();
    emit statusChanged();
}

void Controller::selectRow(int row)
{
    if (m_packages)
        m_packages->setSelected(row);
    emit selectionChanged();
    // Selecting a new package invalidates any old reply, even if its request is
    // still in flight. requestPackageInfo is keyed by exact id, so it can safely
    // replace the older lookup rather than leave this pane waiting for it.
    // With autoFetchInfo off the fetch waits for an explicit button; the cache
    // still serves whatever a previous fetch stored (see requestPackageInfo).
    if (row >= 0 && m_packageInfoRow != row && m_settings.autoFetchInfo)
        requestPackageInfo(row);
}

void Controller::toggleRow(int row)
{
    if (!m_packages)
        return;
    m_packages->toggleChecked(row);
    emit selectionChanged();
}

void Controller::clearSelection()
{
    if (!m_packages)
        return;
    m_packages->clearChecked();
    emit selectionChanged();
}

void Controller::copyToClipboard(const QString& value)
{
    if (!value.isEmpty())
        copyUsingHost(value, text("已复制。", "Copied."));
}

void Controller::copyUsingHost(const QString& value, const QString& successMessage)
{
    if (!m_clipboardWriter) {
        m_copyNotice = text("宿主剪贴板服务不可用。", "The host clipboard service is unavailable.");
        emit copyNoticeChanged();
        return;
    }
    m_copyNotice = text("正在复制…", "Copying…");
    emit copyNoticeChanged();
    m_clipboardWriter(value, [this, successMessage](bool ok, const QString& error) {
        QMetaObject::invokeMethod(this, [this, ok, successMessage, error] {
            m_copyNotice = ok ? successMessage : text("复制失败：", "Copy failed: ") + error;
            emit copyNoticeChanged();
            emit statusChanged();
        }, Qt::QueuedConnection);
    });
}

int Controller::selectedRow() const
{
    return m_packages ? m_packages->selected() : -1;
}

void Controller::openUrl(const QString& url)
{
    // The same rule the homepage action follows: metadata is third-party data, so
    // only an absolute http(s) address is opened. A file: or javascript: URL that
    // a manifest carried would otherwise be launched by this click.
    const QUrl parsed = QUrl::fromUserInput(url.trimmed());
    const QString scheme = parsed.scheme().toLower();
    if (parsed.isEmpty() || !parsed.isValid() || (scheme != QStringLiteral("http") &&
                                                  scheme != QStringLiteral("https"))) {
        m_copyNotice = text("这个地址不是 http(s)，没有打开。",
                            "That address is not http(s), so it was not opened.");
        emit copyNoticeChanged();
        return;
    }
    QDesktopServices::openUrl(parsed);
    emit statusChanged();
}

void Controller::copyPackageInfo()
{
    if (m_packageInfo.isEmpty() && m_packageInfoId.isEmpty()) {
        m_copyNotice = text("还没有可复制的包信息。", "There is no package metadata to copy yet.");
        emit copyNoticeChanged();
        return;
    }
    // Ordered so the paste reads like the window: identity first, then publisher,
    // then installer internals. The map itself has no order.
    static const char* const order[] = {
        "version",     "publisher", "publisherUrl", "publisherSupportUrl", "author",
        "moniker",     "homepage",  "license",      "licenseUrl",           "copyright",
        "privacyUrl",  "installerType", "installerLocale", "installerArchitecture",
        "installerUrl", "installerSha256", "releaseDate",
    };
    QString out = m_packageInfoId.isEmpty() ? QString() : QStringLiteral("id: ") + m_packageInfoId + QStringLiteral("\n");
    auto append = [&out](const QString& key, const QString& value) {
        if (!value.isEmpty())
            out += key + QStringLiteral(": ") + value + QStringLiteral("\n");
    };
    for (const char* key : order)
        append(QString::fromUtf8(key), m_packageInfo.value(QString::fromUtf8(key)).toString());
    append(QStringLiteral("description"), m_packageInfo.value(QStringLiteral("description")).toString());
    append(QStringLiteral("tags"), m_packageInfo.value(QStringLiteral("tags")).toString());
    append(QStringLiteral("releaseNotes"), m_packageInfo.value(QStringLiteral("releaseNotes")).toString());
    copyUsingHost(out, text("已复制该包的元数据。", "Copied the package metadata."));
}

void Controller::copyTaskLog(qulonglong id)
{
    const Task* t = m_queue.find(static_cast<unsigned long long>(id));
    if (!t || t->log.empty()) {
        m_copyNotice = text("该任务没有可复制的日志。", "This task has no log to copy.");
        emit copyNoticeChanged();
        return;
    }
    // Plain text, one line per entry, so it survives a paste into a terminal, an
    // issue or a chat window. Every line was already redacted before it was
    // stored, so the clipboard cannot leak a local path the log itself hid.
    QString out;
    out += QStringLiteral("task #%1  %2  %3 %4\n")
               .arg(id)
               .arg(QString::fromStdString(managerId(t->plan.key.manager)))
               .arg(QString::fromStdString(actionId(t->plan.action)))
               .arg(QString::fromStdString(t->plan.key.id));
    for (const auto& e : t->log) {
        out += QString::fromStdString(e.at) + QStringLiteral("\t")
             + QString::fromStdString(e.phase) + QStringLiteral("\t")
             + QString::fromStdString(e.kind) + QStringLiteral("\t")
             + QString::fromStdString(e.text);
        if (e.exitCode != -1) {
            const quint32 raw = static_cast<quint32>(e.exitCode);
            out += QStringLiteral("\t[exit %1 / 0x%2]")
                       .arg(e.exitCode)
                       .arg(raw, 8, 16, QLatin1Char('0'));
        }
        out += QStringLiteral("\n");
    }
    copyUsingHost(out, text("已复制该任务的日志（", "Copied this task's log (") +
                           QString::number(t->log.size()) + text(" 行）。", " lines)."));
}

void Controller::openHomepage(int row)
{
    if (!m_packages)
        return;
    const auto& rows = m_packages->rows();
    if (row < 0 || row >= static_cast<int>(rows.size()))
        return;
    const auto& p = rows[static_cast<std::size_t>(row)];
    const QString id = QString::fromStdString(p.key.id);
    if (id.isEmpty())
        return;

    // The backend's own `show` output carries the real homepage. When it is known
    // for this row, open that instead of a search guess: it is the page the author
    // declared in the manifest, and it is the same URL `winget show` prints.
    if (m_packageInfoId == QString::fromStdString(p.key.id)) {
        const QString known = m_packageInfo.value(QStringLiteral("homepage")).toString();
        if (!known.isEmpty()) {
            QDesktopServices::openUrl(QUrl(known));
            emit statusChanged();
            return;
        }
    }

    // No `show` result yet: start the lookup and tell the user, rather than open a
    // search page that is only a guess. The pane shows the URL once it arrives and
    // the button is pressed again.
    requestPackageInfo(row);
    emit statusChanged();
}

void Controller::requestPackageInfo(int row)
{
    if (!m_packages)
        return;
    const auto& rows = m_packages->rows();
    if (row < 0 || row >= static_cast<int>(rows.size()))
        return;
    const Package p = rows[static_cast<std::size_t>(row)];
    // winget list reports no scope, so the stored row cannot name one. `show` is
    // keyed by id alone, which is exactly what winget accepts.
    if (p.key.manager != Manager::Winget || !winget::isWingetPackageId(p.key.id)) {
        // Scoop has no equivalent structured output worth a second process here,
        // and an ARP/MSIX entry is not a winget package at all.
        m_packageInfoRow = row;
        m_packageInfoId = QString::fromStdString(p.key.id);
        m_packageInfo.clear();
        m_packageInfo.insert(QStringLiteral("note"),
                             text("该行没有可查询的包信息（Scoop 包或系统安装记录）。",
                                  "No package information is available for this row (a Scoop package or an "
                                  "installation record)."));
        m_packageInfoBusy = false;
        emit packageInfoChanged();
        return;
    }

    // A cached answer fills the pane without a multi-second process. It is good
    // for as long as it describes this package: an entry fetched for the version
    // still installed does not expire by age, only by the package changing. Rows
    // that are not installed (search results) keep the time-based TTL.
    info_cache::Entry cached;
    if (info_cache::find(m_infoStore, p.key.id, cached) &&
        (info_cache::stillMatches(cached, p.installedVersion) || infoFresh(cached.observedAt))) {
        showCachedInfo(cached, row, p.key.id);
        return;
    }

    m_packageInfoRow = row;
    m_packageInfoId = QString::fromStdString(p.key.id);
    m_packageInfoBusy = true;
    m_packageInfo.clear();
    emit packageInfoChanged();
    emit statusChanged();

    const std::string id = p.key.id;
    // Use the adapter's resolved winget path. A bare "winget.exe" does start
    // through the WindowsApps alias in some processes but not under the host's
    // environment, where it failed with "the system cannot find the file
    // specified" — and every other query in this plugin already uses the path the
    // probe resolved.
    const std::string executable = wingetAdapter().executablePath();
    auto* self = this;
    // The answer is keyed by the exact id, not the row index: a tab switch reuses
    // row numbers for different packages, and a reply arriving after that would
    // otherwise describe the package that used to be at that index.
    spawnWorker([self, id, row, executable, version = p.installedVersion] {
        std::map<std::string, std::string> fields;
        std::string error;
        {
            platform::RunOptions options;
            options.application = executable;
            // `show` is read-only and needs no elevation. Interactivity is off so
            // it can never wait for a prompt.
            options.arguments = {"show", "--id", id, "--exact", "--disable-interactivity"};
            options.timeoutMs = 30000;
            const auto run = platform::run(options);
            if (!run.launched) {
                error = run.errorText.empty() ? "winget show could not be started" : run.errorText;
            } else {
                fields = pkg::winget::parseShowOutput(run.output);
                if (fields.empty())
                    error = "winget show printed nothing usable for this id";
            }
        }
        QMetaObject::invokeMethod(
            self,
            [self, row, id, fields, error, version] {
                // Cache before the staleness check: the answer is valid for
                // this id even when the pane has already moved to another row.
                if (error.empty() && !fields.empty()) {
                    info_cache::Entry entry;
                    entry.id = id;
                    entry.observedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
                    entry.version = version;
                    entry.fields = fields;
                    info_cache::upsert(self->m_infoStore, std::move(entry));
                    self->saveInfoCache();
                }
                if (self->m_packageInfoId != QString::fromStdString(id))
                    return;   // the selection moved on; this answer is stale
                self->m_packageInfoRow = row;
                self->m_packageInfoBusy = false;
                self->m_packageInfo.clear();
                if (!error.empty()) {
                    self->m_packageInfo.insert(QStringLiteral("note"), QString::fromStdString(error));
                    emit self->packageInfoChanged();
                    return;
                }
                // Only the fields the pane shows. The parsed map can hold a dozen
                // entries, most of them installer internals.
                auto put = [&](const char* key, const char* source) {
                    const auto it = fields.find(source);
                    if (it != fields.end() && !it->second.empty())
                        self->m_packageInfo.insert(QString::fromUtf8(key),
                                                   QString::fromStdString(it->second));
                };
                put("homepage", "homepage");
                put("publisher", "publisher");
                put("publisherUrl", "publisherurl");
                put("publisherSupportUrl", "publishersupporturl");
                put("author", "author");
                put("license", "license");
                put("licenseUrl", "licenseurl");
                put("copyright", "copyright");
                put("privacyUrl", "privacyurl");
                put("description", "description");
                put("tags", "tags");
                put("installerType", "installertype");
                put("installerLocale", "installerlocale");
                put("installerArchitecture", "installerarchitecture");
                put("installerUrl", "installerurl");
                put("installerSha256", "installersha256");
                put("releaseDate", "releasedate");
                put("releaseNotes", "releasenotes");
                put("moniker", "moniker");
                put("version", "version");
                emit self->packageInfoChanged();
                emit self->statusChanged();
            },
            Qt::QueuedConnection);
    });
}

void Controller::resetReview()
{
    m_reviewVisible = false;
    m_reviewConfirmEnabled = false;
    m_sourceAgreed = false;
    m_installerAgreed = false;
    m_needsSourceAgreement = false;
    m_reviewFacts.clear();
    m_reviewActions = QVariantList();
    m_reviewRows.clear();
    m_reviewPlans.clear();
    m_reviewBatch.clear();
    m_blockedReason.clear();
    emit reviewChanged();
}

void Controller::openReviewFor(int row, const QString& actionId){
    if (!m_packages)
        return;
    const auto& rows = m_packages->rows();
    if (row < 0 || row >= static_cast<int>(rows.size()))
        return;
    // The review page is driven by the checked row, so a request that names a row
    // must make that row the single checked one. This is the step the details-pane
    // buttons skipped: they only selected the row, so openReview() found nothing
    // checked and opened the "select exactly one package" state instead.
    m_packages->clearChecked();
    m_packages->toggleChecked(row);
    emit selectionChanged();

    openReview();   // builds the action list for this row
    // Only now is there a legal action list to validate against. An unknown id is
    // ignored by setReviewAction, which leaves the default action in place rather
    // than an empty one.
    if (!actionId.isEmpty())
        setReviewAction(actionId);
}

void Controller::openReview()
{
    if (!m_packages)
        return;
    const auto& rows = m_packages->rows();
    // The review is a batch: every checked row is one enumerated, independently
    // authorised change. One row is the common case and renders exactly as
    // before; several rows list each package's own plan, and a row that cannot
    // take the chosen action stays visible with its blocker instead of being
    // dropped silently — the confirm then enqueues only the runnable ones.
    std::vector<Package> batch;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (m_packages->isChecked(i))
            batch.push_back(rows[static_cast<std::size_t>(i)]);
    }
    if (batch.empty()) {
        m_reviewVisible = true;
        m_reviewConfirmEnabled = false;
        m_reviewAction = QString();
        m_reviewActions = QVariantList();
        m_reviewFacts.clear();
        m_blockedReason = text("请先勾选要操作的包：每一项变更是独立授权的。",
                               "Check the packages to operate on first: each change is authorised separately.");
        emit reviewChanged();
        return;
    }

    // The checked rows are copied, not referenced: rows are replaced on every
    // refresh and every tab switch, and keeping copies is what makes the page
    // safe to leave open across a background refresh.
    m_reviewBatch = std::move(batch);
    m_reviewPackage = m_reviewBatch.front();
    m_reviewRow = -1;

    // Legal actions for the batch: the union over the rows. A batch upgrade is
    // the common case from the Updates tab, so it leads when any row offers
    // it; for a single row the list is exactly what that row offered before.
    m_reviewActions = QVariantList();
    int upgradable = 0, installedCount = 0, absent = 0;
    for (const auto& p : m_reviewBatch) {
        if (p.installedVersion.empty())
            ++absent;
        else
            ++installedCount;
        if (!p.installedVersion.empty() && !p.availableVersion.empty() &&
            p.availableVersion != p.installedVersion)
            ++upgradable;
    }
    if (upgradable > 0) {
        m_reviewActions.append(QVariantMap{
            {"id", QStringLiteral("upgrade")},
            {"label", text("升级", "Upgrade")},
            {"why", m_reviewBatch.size() == 1
                        ? text("检测到更新可用，可升级到 %1", "An update is available; upgrade to %1")
                              .arg(QString::fromStdString(m_reviewBatch.front().availableVersion))
                        : text("%1 行检测到更新，可一并升级", "%1 rows have updates and can be upgraded together")
                              .arg(upgradable)},
        });
    }
    if (installedCount > 0) {
        m_reviewActions.append(QVariantMap{
            {"id", QStringLiteral("uninstall")},
            {"label", text("卸载", "Uninstall")},
            {"why", text("卸载会移除本机软件，与是否可升级无关",
                         "Uninstall removes the local software, whether or not an update exists")},
        });
    }
    if (absent > 0) {
        m_reviewActions.append(QVariantMap{
            {"id", QStringLiteral("install")},
            {"label", text("安装", "Install")},
            {"why", text("该行尚未安装，只能安装", "Nothing is installed for this row, so install is the only action")},
        });
    }

    // Default to the first legal action: upgrade when any row has an update,
    // otherwise uninstall (installed rows) or install (absent ones). The user
    // can change it.
    const QString first = m_reviewActions.isEmpty()
                              ? QStringLiteral("install")
                              : m_reviewActions.first().toMap().value("id").toString();
    buildReview(actionFromId(first.toStdString()));
    m_reviewVisible = true;
    emit reviewChanged();
}

void Controller::buildReview(Action action)
{
    // One plan per checked package, each with its own preflight, facts and
    // blockers. m_reviewFacts stays the single-row view; the batch list is what
    // a multi-row review shows.
    m_reviewPlans.clear();
    m_reviewRows.clear();
    bool anyNeedsSource = false;
    int ready = 0;
    const bool needsMaterial = action == Action::Install || action == Action::Upgrade ||
                               action == Action::Reinstall;
    const std::string materialNote =
        needsMaterial
            ? text("安装材料由 backend 自行下载：插件不做预取，也不做本地校验；来源与版本已在上方核对。",
                   "The install material is downloaded by the backend itself: the plugin neither prefetches "
                   "nor verifies it locally; source and version are checked above.")
                  .toStdString()
            : std::string();
    for (const auto& p : m_reviewBatch) {
        Plan plan = makePlan(p, action, m_elevated);
        if (plan.state != PlanState::Blocked)
            applyPreflight(plan, true, needsMaterial ? "backend-fetched" : "not required", materialNote);
        if (plan.state == PlanState::Ready)
            ++ready;
        if (std::find(p.agreements.begin(), p.agreements.end(), "source") != p.agreements.end())
            anyNeedsSource = true;
        m_reviewPlans.push_back(plan);
        m_reviewRows.append(QVariantMap{
            {"id", QString::fromStdString(p.key.id)},
            {"manager", QString::fromStdString(managerId(p.key.manager))},
            {"source", QString::fromStdString(p.key.sourceOrUnknown())},
            {"installed", QString::fromStdString(p.installedVersion)},
            {"target", action == Action::Uninstall
                           ? text("（移除）", "(remove)")
                           : QString::fromStdString(p.availableVersion)},
            {"method", reviewMethodFor(p, action)},
            {"policy", QString::fromStdString(p.holdPolicy.empty() ? std::string("—") : p.holdPolicy)},
            {"blocked", plan.state == PlanState::Blocked},
            {"reason", QString::fromStdString(plan.blockedReason)},
        });
    }

    // Uninstall has no source agreement to read; install/upgrade union the
    // rows' needs — one checkbox covers the batch, and the executor still
    // re-checks each task's own plan when it runs.
    m_needsSourceAgreement = action != Action::Uninstall && anyNeedsSource;
    m_sourceAgreed = !m_needsSourceAgreement;
    m_installerAgreed = false;

    m_reviewAction = QString::fromUtf8(actionId(action)).toUpper();
    m_reviewPackage = m_reviewBatch.front();
    if (m_reviewBatch.size() == 1) {
        m_reviewPlan = m_reviewPlans.front();
        m_blockedReason = action == Action::Install &&
                                  m_reviewPackage.key.manager == Manager::Winget &&
                                  m_reviewPackage.key.scope == Scope::Unknown
                              ? text("请先选择安装作用域：当前用户或所有用户；WinGet 搜索结果不提供作用域。",
                                     "Choose Current user or All users first; WinGet search does not report install scope.")
                              : QString::fromStdString(m_reviewPlan.blockedReason);
        const auto& p = m_reviewPackage;
        m_reviewFacts = QVariantMap{
            {"manager", QString::fromStdString(managerId(p.key.manager))},
            {"source", QString::fromStdString(p.key.sourceOrUnknown())},
            {"id", QString::fromStdString(p.key.id)},
            {"scope", QString::fromStdString(scopeId(p.key.scope))},
            {"arch", QString::fromStdString(p.arch)},
            {"installed", QString::fromStdString(p.installedVersion)},
            {"target", action == Action::Uninstall
                           ? text("（移除本机版本）", "(removes the local version)")
                           : QString::fromStdString(p.availableVersion)},
            {"method", reviewMethod()},
            {"material", action == Action::Uninstall
                             ? text("无需下载材料（卸载只读本机状态）",
                                    "No material is downloaded; uninstall only reads local state")
                             : QString::fromStdString(p.material)},
            {"dependencies", QString::fromStdString(p.dependencies)},
            {"policy", QString::fromStdString(p.holdPolicy.empty() ? std::string("—") : p.holdPolicy)},
            {"verify", action == Action::Uninstall
                           ? text("执行后重新读取已装列表；不再在列表中即为卸载成功",
                                  "The installed list is re-read afterwards; the package being gone means the "
                                  "uninstall succeeded")
                           : text("执行后重新读取已装版本；退出码 0 不等于成功",
                                  "The installed version is re-read after the run; exit code 0 is not success")},
            {"metadataAt", QString::fromStdString(p.metadataAt)},
            {"risk", reviewRisk()},
            {"interactive", action == Action::Upgrade || action == Action::Install
                                ? text("安装器可能弹出自己的卸载/安装窗口；插件会等它结束，不会替你点击。",
                                       "The installer may show its own uninstall/install window; the plugin waits for it and will not click for you.")
                                : QString()},
        };
    } else {
        // A batch has no single plan: the rows carry the facts. The risk line is
        // action-level and stays; the gate text reports what confirm will do.
        m_reviewPlan = Plan{};
        m_reviewFacts = QVariantMap{{"risk", reviewRisk()}};
        m_blockedReason = ready == 0
                              ? text("所选的行都不能执行该动作；各行原因见下方列表。",
                                     "None of the selected rows can perform this action; each row's reason is "
                                     "listed below.")
                              : QString();
    }
    m_reviewConfirmEnabled = false;
}

void Controller::setInstallScope(const QString& scope)
{
    if (m_reviewBatch.empty() || m_reviewAction != "INSTALL")
        return;
    if (scope != "user" && scope != "machine")
        return;
    for (auto& package : m_reviewBatch) {
        if (package.key.manager == Manager::Winget && package.installedVersion.empty())
        {
            package.key.scope = scope == "user" ? Scope::User : Scope::Machine;
            package.canInstall = true;
        }
    }
    buildReview(Action::Install);
    emit reviewChanged();
}

QString Controller::reviewActionLabel() const
{
    const QString id = m_reviewAction.toLower();
    if (id == QStringLiteral("UNINSTALL") || id == QStringLiteral("uninstall"))
        return text("卸载", "Uninstall");
    if (id == QStringLiteral("UPGRADE") || id == QStringLiteral("upgrade"))
        return text("升级", "Upgrade");
    if (id == QStringLiteral("REINSTALL") || id == QStringLiteral("reinstall"))
        return text("重装", "Reinstall");
    return text("安装", "Install");
}

// The method line for one package under one action, shared by the single-row
// facts and the batch list (a batch may mix managers, so the per-row text
// cannot come from m_reviewPackage alone).
QString Controller::reviewMethodFor(const Package& p, Action action) const
{
    const bool scoop = p.key.manager == Manager::Scoop;
    if (action == Action::Uninstall)
        return scoop ? QStringLiteral("scoop uninstall <name>")
                     : QStringLiteral("winget uninstall --id … --exact");
    if (action == Action::Upgrade)
        return scoop ? QStringLiteral("scoop update <name>（固定 shim 入口）")
                     : QStringLiteral("winget upgrade --id … --exact --source …");
    if (action == Action::Reinstall)
        return text("显式卸载，再安装（非原子）", "explicit uninstall, then install (NOT ATOMIC)");
    return scoop ? QStringLiteral("scoop install <name>")
                 : QStringLiteral("winget install --id … --exact --source …");
}

QString Controller::reviewMethod() const
{
    return reviewMethodFor(m_reviewPackage, actionFromId(m_reviewAction.toLower().toStdString()));
}

QString Controller::reviewRisk() const
{
    const QString id = m_reviewAction.toLower();
    if (id == QStringLiteral("uninstall"))
        return text("卸载会移除本机软件，可能删除用户数据；不会自动重装，也不做本地备份。",
                    "Uninstall removes local software and may delete user data; nothing is reinstalled "
                    "automatically and no local backup is taken.");
    if (id == QStringLiteral("upgrade"))
        return text("升级会修改本机软件；材料由 backend 自行下载，插件不做本地校验；队列按管理器串行执行，"
                    "失败即停，不会静默重试。",
                    "An upgrade modifies local software; the material is downloaded by the backend and is not "
                    "verified locally; the queue runs one task per manager, stops on failure and never retries "
                    "silently.");
    return text("安装会向本机写入新软件；材料由 backend 自行下载，插件不做本地校验；失败即停，不会静默重试。",
                "An install writes new software to this machine; the material is downloaded by the backend and is "
                "not verified locally; it stops on failure and never retries silently.");
}

void Controller::setReviewAction(const QString& actionIdIn)
{
    const QString wanted = actionIdIn.toLower();
    // Only legal actions are accepted: a selector entry that the plan would
    // refuse must not be reachable from QML either.
    bool legal = false;
    for (const auto& v : m_reviewActions) {
        if (v.toMap().value("id").toString() == wanted) {
            legal = true;
            break;
        }
    }
    if (!legal)
        return;
    buildReview(actionFromId(wanted.toStdString()));
    emit reviewChanged();
}

void Controller::closeReview() { resetReview(); }

void Controller::setSourceAgreed(bool agreed)
{
    m_sourceAgreed = agreed;
    if (!agreed && m_needsSourceAgreement) {
        m_reviewConfirmEnabled = false;
    }
    emit reviewChanged();
}

void Controller::setInstallerAgreed(bool agreed)
{
    m_installerAgreed = agreed;
    // Confirmation needs every outstanding agreement explicitly checked AND at
    // least one runnable plan: a batch of blocked rows confirms nothing.
    int ready = 0;
    for (const auto& plan : m_reviewPlans)
        if (plan.state == PlanState::Ready)
            ++ready;
    m_reviewConfirmEnabled = ready > 0 && m_installerAgreed &&
                             (!m_needsSourceAgreement || m_sourceAgreed);
    emit reviewChanged();
}

void Controller::confirmAndQueue()
{
    if (!m_reviewConfirmEnabled)
        return;
    // Consent is recorded per agreement, in the order the plans expect, and
    // stored on each task itself. The executor hands exactly these to
    // buildMutation, and nothing else ever adds an agreement flag.
    std::vector<std::string> accepted;
    if (m_sourceAgreed && m_needsSourceAgreement)
        accepted.push_back("source");
    if (m_installerAgreed)
        accepted.push_back("installer");

    // The batch confirm enqueues every runnable plan — one task each, still
    // enumerated item by item, each re-checked by the executor's own preflight
    // and verified independently after its run. Rows whose plan is blocked
    // stay in the review with their reason; they are never silently covered.
    unsigned long long queued = 0;
    QStringList refusals;
    for (Plan plan : m_reviewPlans) {
        if (plan.state != PlanState::Ready)
            continue;
        if (!confirmPlan(plan, "now")) {
            refusals << QString::fromStdString(plan.key.id) + ": " +
                             text("确认失败：计划未通过预检或仍有未处理的协议。",
                                  "confirmation failed: preflight did not pass or an agreement is outstanding");
            continue;
        }
        const auto result = m_queue.enqueue(plan, accepted);
        if (result.accepted) {
            ++queued;
        } else {
            refusals << QString::fromStdString(plan.key.id) + ": " + QString::fromStdString(result.reason);
        }
    }
    if (queued == 0) {
        m_blockedReason = refusals.isEmpty()
                              ? text("没有可入队的任务。", "No task was queueable.")
                              : text("入队失败：", "Not queued: ") + refusals.join(text("；", "; "));
        emit reviewChanged();
        return;
    }
    if (m_packages)
        m_packages->clearChecked();
    if (m_tasks)
        m_tasks->setTasks(m_queue.tasks());
    resetReview();
    emit selectionChanged();
    emit tasksChanged();
    setTab(QStringLiteral("tasks"));
    // Enqueueing is not running: the executor picks the tasks up and drives
    // them through their phases on a worker thread.
    drainQueue();
}

void Controller::removeLastPendingTask()
{
    // Kept for an older QML build. Per-row removal is removeTask(id).
    for (auto it = m_queue.tasks().rbegin(); it != m_queue.tasks().rend(); ++it) {
        if (m_queue.removePending(it->id)) {
            if (m_tasks)
                m_tasks->setTasks(m_queue.tasks());
            emit tasksChanged();
            return;
        }
    }
}

void Controller::removeTask(qulonglong id)
{
    if (!m_queue.removeFinished(static_cast<unsigned long long>(id)))
        return;
    if (m_tasks)
        m_tasks->setTasks(m_queue.tasks());
    emit tasksChanged();
    emit statusChanged();
}

void Controller::cancelTask(qulonglong id)
{
    const auto taskId = static_cast<unsigned long long>(id);
    const Task* t = m_queue.find(taskId);
    if (!t)
        return;
    if (t->state == TaskState::Queued || t->state == TaskState::Preflight ||
        t->state == TaskState::NeedsReview || t->state == TaskState::Blocked) {
        // Not started yet: cancelling is just dropping it, nothing runs.
        m_queue.requestCancel(taskId);
        logLine(taskId, t->phase.empty() ? "queue" : t->phase.c_str(), "warning",
                          text("已取消：任务尚未开始，没有进程被启动。",
                               "Cancelled before it started; no process was launched.")
                              .toStdString());
    } else if (m_queue.requestCancel(taskId)) {
        // The executor polls this flag between phases and the platform layer
        // reads it inside run(), so the installer process is terminated.
        if (m_runCancel)
            m_runCancel->store(true, std::memory_order_relaxed);
        logLine(taskId, "cancel", "warning",
                          text("已请求取消：终止子进程树，不承诺回滚。",
                               "Cancel requested: the process tree is terminated; no rollback is promised.")
                              .toStdString());
    } else {
        return;
    }
    if (m_tasks)
        m_tasks->setTasks(m_queue.tasks());
    emit tasksChanged();
    emit statusChanged();
}

void Controller::startTask(qulonglong id)
{
    const auto taskId = static_cast<unsigned long long>(id);
    const Task* t = m_queue.find(taskId);
    if (!t)
        return;
    if (t->state == TaskState::NeedsReview || t->state == TaskState::Blocked) {
        // A task parked for review must be re-confirmed, never started directly:
        // the facts it was confirmed on may have changed.
        retryTask(id);
        return;
    }
    if (t->state != TaskState::Queued)
        return;
    logLine(taskId, "queue", "info",
                      text("用户手动开始。", "Started manually by the user.").toStdString());
    if (m_tasks)
        m_tasks->setTasks(m_queue.tasks());
    emit tasksChanged();
    drainQueue();
}

void Controller::retryTask(qulonglong id)
{
    const auto taskId = static_cast<unsigned long long>(id);
    const Task* t = m_queue.find(taskId);
    if (!t)
        return;
    if (!isTerminal(t->state) && t->state != TaskState::NeedsReview && t->state != TaskState::Blocked)
        return;
    // Never replays the old task: the plan's facts may have changed, so the
    // review reopens for the same exact identity and the user confirms again.
    const PackageKey key = t->plan.key;
    if (m_packages) {
        const auto& rows = m_packages->rows();
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            if (rows[static_cast<std::size_t>(i)].key == key) {
                m_packages->clearChecked();
                m_packages->toggleChecked(i);
                break;
            }
        }
    }
    resetReview();
    openReview();
    emit selectionChanged();
    emit tasksChanged();
}

void Controller::clearFinishedTasks()
{
    if (m_queue.clearFinished() <= 0)
        return;
    if (m_tasks)
        m_tasks->setTasks(m_queue.tasks());
    emit tasksChanged();
    emit statusChanged();
}

void Controller::setQueuePaused(bool pausedIn)
{
    if (pausedIn)
        m_queue.pause();
    else
        m_queue.resume();
    emit tasksChanged();
    emit statusChanged();
    // Resuming picks up whatever was waiting behind the pause.
    if (!pausedIn)
        drainQueue();
}

void Controller::toggleQueuePaused() { setQueuePaused(!m_queue.paused()); }

void Controller::requestCancelLastRunning()
{
    for (auto it = m_queue.tasks().rbegin(); it != m_queue.tasks().rend(); ++it) {
        if (it->state == TaskState::Running || it->state == TaskState::InteractivePending) {
            m_queue.requestCancel(it->id);
            // The executor polls this between phases; the platform layer reads
            // it inside run() as well, so an installer process is terminated.
            if (m_runCancel)
                m_runCancel->store(true, std::memory_order_relaxed);
            if (m_tasks)
                m_tasks->setTasks(m_queue.tasks());
            emit tasksChanged();
            emit statusChanged();
            return;
        }
    }
}

Adapter& Controller::adapterFor(Manager manager)
{
    return manager == Manager::Scoop ? static_cast<Adapter&>(scoopAdapter())
                                     : static_cast<Adapter&>(wingetAdapter());
}

namespace {

// The redaction root: a task log can carry absolute paths, and the user's
// profile is the one that appears in every backend's output.
std::string userProfilePath()
{
    const QString profile = qEnvironmentVariable("USERPROFILE");
    return profile.toStdString();
}

} // namespace

void Controller::logLine(unsigned long long id, const char* phase, const char* kind,
                         const std::string& text, int exitCode)
{
    // The executor calls this from its worker thread; m_queue is GUI-thread
    // state, so a cross-thread call is re-posted rather than written directly.
    if (QThread::currentThread() != thread()) {
        const std::string phaseCopy = phase ? phase : "";
        const std::string kindCopy = kind ? kind : "info";
        const std::string textCopy = text;
        QMetaObject::invokeMethod(this, [this, id, phaseCopy, kindCopy, textCopy, exitCode] {
            logLine(id, phaseCopy.c_str(), kindCopy.c_str(), textCopy, exitCode);
        }, Qt::QueuedConnection);
        return;
    }
    if (text.empty()) {
        m_queue.appendLog(id, phase, kind, text, exitCode);
        return;
    }
    const RedactRules rules = defaultRules(userProfilePath());
    // Full evidence, not fragments: the task log is the only record of what the
    // backend was told and what it answered, and the user reads it to judge a
    // result. Redaction still applies (paths, ANSI escapes, secrets); the
    // bounds are generous — 2000 lines, 4000 characters per line — instead of
    // the old one-line fragment that hid everything but the first line.
    RedactRules logRules = rules;
    logRules.maxLineLength = 4000;
    m_queue.appendLog(id, phase, kind, trimBlock(redactBlock(text, logRules, 2000)), exitCode);
}

void Controller::drainQueue()
{
    // One mutation at a time per lane is the queue's rule; one task at a time
    // overall is enough here, because the two lanes exist to keep a winget and a
    // scoop mutation from racing — and the user confirmed each of them one by
    // one, so running them strictly in order is the honest reading of that
    // consent.
    const auto next = m_queue.nextRunnable();
    if (!next.found) {
        // Every lane is idle. A run may have changed what is installed, so the
        // inventory refreshes once, in the background; the exit code alone is
        // never trusted to describe the machine. Forced, because both parts may
        // still look fresh while the package the task touched is stale.
        if (m_lastRefresh.isValid() && !m_queue.paused())
            startBackendRefresh(true);
        return;
    }
    if (!m_queue.start(next.id)) {
        // The head is waiting on its lane (a two-phase reinstall occupies it).
        // This cannot recur endlessly: the running task finishes and the finish
        // path drains again.
        return;
    }
    if (m_tasks)
        m_tasks->setTasks(m_queue.tasks());
    emit tasksChanged();
    runOneTask(next.id);
}

void Controller::runOneTask(unsigned long long id)
{
    const Task* task = m_queue.find(id);
    if (!task)
        return;
    const Plan plan = task->plan;
    const Manager manager = plan.key.manager;
    // Consent is copied per task. The executor used to read the controller's single
    // "latest review" vector from a worker thread while the GUI thread could still
    // be rewriting it for the next review: a read of a vector being mutated is
    // undefined behaviour, and it also meant a queued task could run with the
    // agreements of a review that came after it.
    const std::vector<std::string> accepted = task->agreements;

    auto* self = this;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    // requestCancelLastRunning writes the current one; the run reads it.
    m_runCancel = cancel;

    spawnWorker([self, id, plan, manager, cancel, accepted] {
        Adapter& adapter = self->adapterFor(manager);

        const CommandSpec spec = adapter.buildMutation(plan, accepted);
        {
            const std::string planned =
                std::string("计划：") + actionId(plan.action) + " " + plan.key.id +
                "（mgr=" + managerId(plan.key.manager) + " scope=" + scopeId(plan.key.scope) + "）";
            const std::string plannedEn =
                std::string("plan: ") + actionId(plan.action) + " " + plan.key.id +
                " (mgr=" + managerId(plan.key.manager) + " scope=" + scopeId(plan.key.scope) + ")";
            const std::string line = self->chinese() ? planned : plannedEn;
            QMetaObject::invokeMethod(self, [self, id, line] {
                self->logLine(id, "plan", "ok", line);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(self, [self, id] {
            if (self->m_tasks)
                self->m_tasks->setTasks(self->m_queue.tasks());
            emit self->tasksChanged();
        }, Qt::QueuedConnection);
        if (!spec.ok) {
            QMetaObject::invokeMethod(self, [self, id, refusal = spec.refusal] {
                self->m_queue.setBlocked(id, refusal);
                self->logLine(id, "plan", "error", refusal);
                if (self->m_tasks)
                    self->m_tasks->setTasks(self->m_queue.tasks());
                emit self->tasksChanged();
                self->onTaskFinished(id);
            }, Qt::QueuedConnection);
            return;
        }

        // One run per phase of the plan: a two-phase reinstall runs uninstall
        // and install as separate processes, in the plan's order, and a cancel
        // between phases stops before the next one.
        int exitCode = -1;
        bool ran = false;
        for (const std::string& step : plan.steps) {
            if (cancel->load(std::memory_order_relaxed))
                break;
            QMetaObject::invokeMethod(self, [self, id, step] {
                self->m_queue.setPhase(id, step);
                const std::string line = self->chinese() ? ("阶段：" + step) : ("phase: " + step);
                self->logLine(id, step.c_str(), "info", line);
                if (self->m_tasks)
                    self->m_tasks->setTasks(self->m_queue.tasks());
                emit self->tasksChanged();
            }, Qt::QueuedConnection);

            platform::RunOptions options = spec.options;
            options.cancelAtomic = cancel.get();
            if (options.interactiveAllowed) {
                // Upgrade is allowed to hand over to the backend's own uninstaller
                // window. Mark this before blocking in run(), not after it returns:
                // the host window stays themed and the task row explains the wait.
                QMetaObject::invokeMethod(self, [self, id, step] {
                    self->m_queue.setInteractivePending(id, true);
                    const std::string line = self->chinese()
                        ? "等待 WinGet 的卸载/安装界面完成。按提示操作；插件不会替你点击。"
                        : "Waiting for WinGet's uninstall/install UI. Follow its prompts; the plugin will not click for you.";
                    self->logLine(id, step.c_str(), "info", line);
                    if (self->m_tasks)
                        self->m_tasks->setTasks(self->m_queue.tasks());
                    emit self->tasksChanged();
                }, Qt::QueuedConnection);
            }
            const auto result = platform::run(options);
            exitCode = result.exitCode;
            ran = true;
            if (options.interactiveAllowed) {
                QMetaObject::invokeMethod(self, [self, id] {
                    self->m_queue.setInteractivePending(id, false);
                    if (self->m_tasks)
                        self->m_tasks->setTasks(self->m_queue.tasks());
                    emit self->tasksChanged();
                }, Qt::QueuedConnection);
            }
            {
                // The command line and its full output are the only evidence of
                // what happened, so they go to the log uncut — redacted as
                // always (paths, ANSI, secrets), bounded by the redaction
                // layer's generous caps instead of a hand-picked fragment.
                std::string detail = std::string(self->chinese() ? "命令：" : "argv: ") + options.application;
                for (const auto& a : options.arguments)
                    detail += " " + a;
                QMetaObject::invokeMethod(self, [self, id, step, detail, output = result.output,
                                                 code = result.exitCode, timedOut = result.timedOut] {
                    self->m_queue.setExitCode(id, code);
                    self->logLine(id, step.c_str(), "info", detail);
                    if (!output.empty())
                        self->logLine(id, step.c_str(), code == 0 ? "info" : "error", output, code);
                    if (static_cast<quint32>(code) == 0x8a15007du) {
                        self->logLine(id, step.c_str(), "error",
                            self->text("WinGet 拒绝在管理员进程中卸载当前用户安装的包。请在普通权限的 Win顺 中重新审阅卸载；插件不会自动降权或重试。",
                                       "WinGet refused to uninstall a user-scope package from an elevated process. Review the uninstall again in a non-elevated WinShun; no automatic privilege change or retry was attempted.").toStdString(), code);
                    }
                    if (static_cast<quint32>(code) == 0x87af0008u) {
                        self->logLine(id, step.c_str(), "error",
                            self->text("WinGet 用户数据库当前不可写（0x87AF0008）。本次没有卸载软件；后台刷新与卸载并发访问已被修复，请关闭其他 WinGet 操作后重试。",
                                       "WinGet's user database was not writable (0x87AF0008). Nothing was uninstalled; concurrent refresh and mutation access has been fixed. Close other WinGet operations and retry.").toStdString(), code);
                    }
                    if (timedOut)
                        self->logLine(id, step.c_str(), "error",
                                                self->text("进程超时，已终止。", "The process timed out and was "
                                                                               "terminated.").toStdString());
                    if (self->m_tasks)
                        self->m_tasks->setTasks(self->m_queue.tasks());
                    emit self->tasksChanged();
                }, Qt::QueuedConnection);
            }
            if (result.timedOut || cancel->load(std::memory_order_relaxed))
                break;   // the remaining phases would run on a machine we no longer understand
        }

        // A cancelled run does not reach the verify step: the state on disk is
        // whatever the installer left, and the next refresh reads that.
        VerifyResult verify;
        verify.outcome = VerifyOutcome::Unknown;
        if (ran && !cancel->load(std::memory_order_relaxed)) {
            QMetaObject::invokeMethod(self, [self, id] {
                self->m_queue.setPhase(id, "verify");
                if (self->m_tasks)
                    self->m_tasks->setTasks(self->m_queue.tasks());
                emit self->tasksChanged();
            }, Qt::QueuedConnection);
            const ProbeResult probe = manager == Manager::Scoop ? scoopAdapter().probeCached()
                                                                : wingetAdapter().probeCached();
            if (manager == Manager::Winget && plan.action == Action::Upgrade &&
                !plan.facts.targetVersion.empty()) {
                // Exit code 0 is not the verdict. A WinGet upgrade can report
                // success and leave the old version in place (a DBX upgrade did
                // exactly that), so the promised target version is checked.
                verify = wingetAdapter().verifyReachedTarget(plan.key, plan.facts.targetVersion,
                                                            adapter.verifyInstalled(probe, plan.key));
            } else {
                verify = adapter.verifyInstalled(probe, plan.key);
                if (plan.action == Action::Uninstall) {
                    // The uninstall's verdict is the ABSENCE of the package, and
                    // only a complete answer proves it. The old code inverted
                    // "not found", which also matched a query that had failed —
                    // so a failed query read as "no longer listed" and the task
                    // looked uninstalled.
                    if (verify.outcome == VerifyOutcome::Present) {
                        verify.outcome = VerifyOutcome::PresentWrongVersion;
                        verify.detail = "still listed after the uninstall" +
                                        (verify.installedVersion.empty() ? std::string()
                                                                          : " at " + verify.installedVersion);
                    } else if (verify.outcome == VerifyOutcome::Absent) {
                        verify.detail += ": the uninstall is confirmed";
                    }
                }
            }
            const bool ok = verify.outcome == VerifyOutcome::Present ||
                            verify.outcome == VerifyOutcome::Absent;
            QMetaObject::invokeMethod(self, [self, id, ok, verify] {
                self->m_queue.setVerified(id, ok);
                if (!verify.detail.empty()) {
                    const char* kind = ok ? "ok" : "warning";
                    self->logLine(id, "verify", kind, verify.detail);
                }
                if (self->m_tasks)
                    self->m_tasks->setTasks(self->m_queue.tasks());
                emit self->tasksChanged();
            }, Qt::QueuedConnection);
        }

        QMetaObject::invokeMethod(self, [self, id, verify, exitCode,
                                         cancelled = cancel->load(std::memory_order_relaxed)] {
            if (cancelled) {
                self->m_queue.finish(id, false, exitCode);   // cancel is never success
            } else if (verify.outcome == VerifyOutcome::Unknown && exitCode == 0) {
                // The run ended cleanly but the verification query could not
                // answer (the backend's own list refused to see the package for
                // a while, or timed out). "The machine state is unknown" is its
                // own outcome — a failed verdict would be a guess, and the
                // inventory rescan below shows the truth either way.
                self->m_queue.setNeedsReview(
                    id, self->text("运行已结束，但核对查询无法给出答案；机器的真实状态未知，"
                                   "请对照下方刷新后的列表确认。",
                                   "The run ended cleanly but the verification query could not answer; the real "
                                   "machine state is unknown — check the refreshed list below.")
                         .toStdString());
            } else {
                const bool ok = verify.outcome == VerifyOutcome::Present ||
                                verify.outcome == VerifyOutcome::Absent;
                self->m_queue.finish(id, ok, exitCode);
            }
            const Task* t = self->m_queue.find(id);
            if (t) {
                const char* kind = t->state == TaskState::Succeeded ? "ok"
                                 : isTerminal(t->state)               ? "error"
                                                                      : "info";
                self->logLine(id, "finish", kind, t->detail, exitCode);
            }
            if (self->m_tasks)
                self->m_tasks->setTasks(self->m_queue.tasks());
            emit self->tasksChanged();
            emit self->statusChanged();
            self->onTaskFinished(id);
        }, Qt::QueuedConnection);
    });
}

void Controller::onTaskFinished(unsigned long long id)
{
    // A mutation changed this package on disk: its cached `winget show` answer
    // describes the old state (old version, old installer hash), so drop it and
    // let the next selection refetch. The plan is gone when the task was
    // refused before enqueueing — nothing to invalidate then.
    if (const Task* t = m_queue.find(id); t && t->plan.action != Action::None) {
        if (info_cache::drop(m_infoStore, t->plan.key.id))
            saveInfoCache();
        // A search answer older than this mutation would still offer the package
        // that was just installed (or list one that was just removed), so the
        // whole search cache goes rather than being trusted for its TTL.
        clearSearchCache();
        // The pin list is cheap to re-read and the mutation's own review may have
        // depended on it.
        clearPinsCache();
    }
    // The finished task vacated its lane: whatever is next runs now, and when
    // nothing is, drainQueue refreshes the inventory once.
    m_runCancel.reset();
    drainQueue();
}

void Controller::retryAction()
{
    // "Replan" never replays: it re-opens the review for the same identity.
    resetReview();
    openReview();
}

} // namespace pkg::ui
