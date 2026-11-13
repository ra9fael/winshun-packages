// window.h — the UI controller the protocol thread talks to.
//
// Threading contract, identical in shape to the other Qt-host plugins here:
//   * the protocol thread (winshun::Plugin::serve) never touches a QObject;
//   * every entry point below marshals to the GUI thread and returns immediately;
//   * a query is read-only by construction, so a request that arrives while a
//     mutation runs is still safe.
//
// Everything that changes the machine goes through the queue, which only accepts
// a confirmed plan.
#pragma once

#include <QObject>
#include <QQuickWindow>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../adapters/adapter.h"
#include "../core/model.h"
#include "../core/plan.h"
#include "../core/queue.h"

namespace pkg::ui {

class PackagesModel;
class TasksModel;

// Settings the host sends, already parsed.
struct UiSettings {
    std::string language = "auto";   // auto | zh-CN | en-US
    bool showObserved = true;
    int refreshMinutes = 30;
    bool debugLogging = false;
};

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString locale READ locale NOTIFY appearanceChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY appearanceChanged)
    Q_PROPERTY(QString tab READ tab NOTIFY tabChanged)
    Q_PROPERTY(QString backendFilter READ backendFilter NOTIFY filterChanged)
    // Single-select manager filter: "all" | "scoop" | "winget". "All" is a real
    // option, not the sum of the other two — as a multi-select toggle, "all" was
    // lit together with both managers, which made the highlight stop meaning
    // "this is what you selected".
    Q_PROPERTY(QString managerFilter READ managerFilter NOTIFY filterChanged)
    // Installed rows come from two different places: packages a manager actually
    // owns, and packages the manager merely observed (ARP/MSIX entries the user
    // installed themselves). They are not the same thing, so the Installed tab can
    // filter between them instead of presenting one undifferentiated list.
    Q_PROPERTY(QString sourceFilter READ sourceFilter NOTIFY filterChanged)
    // Discover is a search, not a second copy of the installed list. Its results
    // can be narrowed to what is not installed yet (the default), everything, or
    // only what is already installed.
    Q_PROPERTY(QString discoverFilter READ discoverFilter NOTIFY filterChanged)
    // Whether machine/global scope actions can run: true only while the process
    // actually holds administrator rights (the host's `elevated` permission).
    Q_PROPERTY(bool elevated READ elevated NOTIFY appearanceChanged)
    Q_PROPERTY(QString searchText READ searchText NOTIFY filterChanged)
    // A local filter for the Installed tab: it narrows the already-loaded rows and
    // never starts a backend query. Discover's box is a real search, so the two
    // must not share state — typing in one used to affect the other.
    Q_PROPERTY(QString installedFilter READ installedFilter NOTIFY filterChanged)
    // True while a search is running. The list shows skeleton rows and the search
    // box shows a spinner, so "working" is visible where the user is looking
    // instead of only in the status bar.
    Q_PROPERTY(bool searching READ searching NOTIFY searchStateChanged)
    Q_PROPERTY(QString statusLine READ statusLine NOTIFY statusChanged)
    Q_PROPERTY(QString scanLine READ scanLine NOTIFY statusChanged)
    Q_PROPERTY(QString backendLine READ backendLine NOTIFY statusChanged)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionSummary READ selectionSummary NOTIFY selectionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(QString statePane READ statePane NOTIFY statePaneChanged)
    Q_PROPERTY(QString stateTitle READ stateTitle NOTIFY statePaneChanged)
    Q_PROPERTY(QString stateBody READ stateBody NOTIFY statePaneChanged)
    Q_PROPERTY(QString stateWhy READ stateWhy NOTIFY statePaneChanged)
    Q_PROPERTY(QString blockedReason READ blockedReason NOTIFY reviewChanged)
    Q_PROPERTY(bool reviewVisible READ reviewVisible NOTIFY reviewChanged)
    Q_PROPERTY(QVariantMap reviewFacts READ reviewFacts NOTIFY reviewChanged)
    Q_PROPERTY(bool reviewConfirmEnabled READ reviewConfirmEnabled NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewGateText READ reviewGateText NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewAction READ reviewAction NOTIFY reviewChanged)
    // Actions the current review may legally take (upgrade/uninstall/install),
    // each as {id,label,why}. The plugin no longer infers one: installed with an
    // update available offers both, and the user picks.
    Q_PROPERTY(QVariantList reviewActions READ reviewActions NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewActionLabel READ reviewActionLabel NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewMethod READ reviewMethod NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewRisk READ reviewRisk NOTIFY reviewChanged)
    Q_PROPERTY(QString sourceAgreementText READ sourceAgreementText NOTIFY reviewChanged)
    Q_PROPERTY(QString installerAgreementText READ installerAgreementText NOTIFY reviewChanged)
    Q_PROPERTY(bool needsSourceAgreement READ needsSourceAgreement NOTIFY reviewChanged)
    Q_PROPERTY(bool uninstallReview READ isUninstallReview NOTIFY reviewChanged)
    Q_PROPERTY(bool sourceAgreed READ sourceAgreed NOTIFY reviewChanged)
    Q_PROPERTY(bool installerAgreed READ installerAgreed NOTIFY reviewChanged)
    Q_PROPERTY(int activeTasks READ activeTasks NOTIFY tasksChanged)
    Q_PROPERTY(QString selectedTaskLabel READ selectedTaskLabel NOTIFY tasksChanged)
    // The backend's own description of the selected package (`winget show`), and
    // whether that lookup is still running. The details pane shows these instead
    // of guessing at a homepage.
    Q_PROPERTY(QVariantMap packageInfo READ packageInfo NOTIFY packageInfoChanged)
    Q_PROPERTY(bool packageInfoBusy READ packageInfoBusy NOTIFY packageInfoChanged)
    Q_PROPERTY(bool queuePaused READ queuePaused NOTIFY tasksChanged)
    Q_PROPERTY(int finishedTasks READ finishedTasks NOTIFY tasksChanged)
    // The result of the last "copy this task's log" action, shown next to the
    // button: a copy with no feedback is indistinguishable from a button that did
    // nothing.
    Q_PROPERTY(QString copyNotice READ copyNotice NOTIFY copyNoticeChanged)
    // The selected row index, so a surface that cannot bind to the ListView's
    // currentIndex (the details pane, a capture without a mouse) still follows the
    // selection. Selection is presentation state and implies no action.
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)

public:
    explicit Controller(QObject* parent = nullptr);
    ~Controller() override;

    // Sets the model objects before any window exists.
    void attachModels(PackagesModel* packages, TasksModel* tasks);
    PackagesModel* packagesModel() const { return m_packages; }
    TasksModel* tasksModel() const { return m_tasks; }

    // The scene's theme object; owned by the caller that created it from QML.
    void setThemeObject(QObject* theme) { m_theme = theme; }
    QObject* themeObject() const { return m_theme; }

    // Called from the protocol thread; each marshals to the GUI thread.
    void applyAppearance(const QString& locale, bool dark, const QVariantMap& nativeLook);
    void applySettings(const UiSettings& settings);
    void openWindow();
    void refreshAll();
    void shutdown();
    // Injects a search result directly. Only the development harness uses it: a
    // --fake run starts no backend, so a fake search would otherwise stay empty and
    // the Discover actions could not be checked.
    void applyFakeSearch(const QString& needle, const Snapshot& wingetSearch, const Snapshot& scoopSearch);
    // Makes the given rows the scan result: both backends Ready, nothing running.
    // Without it a --fake run still reports "Querying…" in the status line and
    // lights the busy indicator, so a screenshot describes a window waiting on a
    // backend this mode deliberately never starts. The Updates snapshot is derived
    // from the rows that actually have a newer target, which is what that tab means.
    void applyFakeInventory(const std::vector<Package>& rows);
    // Installs the harness's deterministic task rows into the queue itself. Feeding
    // only the model left the Tasks header and the status line describing an empty
    // queue above a list of four tasks.
    void applyFakeTasks(const std::vector<Task>& tasks);
    // Stops every backend call for the rest of the process. The harness sets this
    // before opening the window: openWindow() refreshes a stale cache, which is
    // right in the plugin and wrong in a deterministic screen capture.
    void setFakeMode(bool fake) { m_fakeMode = fake; }
    // Holds `searching` true for a while. Only the development harness uses it:
    // a fake run starts no query, so the in-box spinner would otherwise never be
    // visible and the one visual it exists for could not be screenshotted.
    void holdSearching(int ms);

    // --- Q_PROPERTY readers ---------------------------------------------------
    QString locale() const { return m_locale; }
    bool dark() const { return m_dark; }
    QString tab() const { return m_tab; }
    QString backendFilter() const { return m_backendFilter; }
    // Single-select: "all" | "scoop" | "winget". Exactly one segment is lit, and
    // this value is what it is lit for.
    QString managerFilter() const { return m_managerFilter; }
    QString sourceFilter() const { return m_sourceFilter; }
    QString discoverFilter() const { return m_discoverFilter; }
    bool elevated() const { return m_elevated; }
    QString searchText() const { return m_searchText; }
    QString installedFilter() const { return m_installedFilter; }
    bool searching() const { return m_searchInFlight || m_searchPending; }
    QString statusLine() const;
    QString copyNotice() const { return m_copyNotice; }
    int selectedRow() const;
    QString scanLine() const;
    QString backendLine() const;
    int checkedCount() const;
    // What the current selection can actually do, in one line: how many rows have
    // a newer version and how many are installed. The selection bar shows this so
    // the buttons' enabled state is explained rather than guessed.
    QString selectionSummary() const;
    bool busy() const { return m_busy; }
    QString statePane() const { return m_statePane; }
    QString stateTitle() const;
    QString stateBody() const;
    QString stateWhy() const { return m_stateWhy; }
    QString blockedReason() const { return m_blockedReason; }
    bool reviewVisible() const { return m_reviewVisible; }
    QVariantMap reviewFacts() const { return m_reviewFacts; }
    bool reviewConfirmEnabled() const { return m_reviewConfirmEnabled; }
    QString reviewGateText() const;
    QString reviewAction() const { return m_reviewAction; }
    QVariantList reviewActions() const { return m_reviewActions; }
    QString reviewActionLabel() const;
    QString reviewMethod() const;
    QString reviewRisk() const;
    // True while the review is for an uninstall: it has no source agreement and
    // no installer agreement, only an acknowledgement of the impact.
    bool isUninstallReview() const;
    QString sourceAgreementText() const;
    QString installerAgreementText() const;
    bool needsSourceAgreement() const { return m_needsSourceAgreement; }
    bool sourceAgreed() const { return m_sourceAgreed; }
    bool installerAgreed() const { return m_installerAgreed; }
    int activeTasks() const;
    // The task the Tasks page has selected, as "T-03 aura2 · 已完成", or empty.
    // The status line reports it so the selected row is not only indicated by a
    // background tint.
    QString selectedTaskLabel() const;
    // Called from QML when the Tasks page selection changes.
    void setSelectedTask(qulonglong id);
    QVariantMap packageInfo() const { return m_packageInfo; }
    bool packageInfoBusy() const { return m_packageInfoBusy; }
    // Terminal task records, which clearFinishedTasks removes. Drives the
    // "清空已完成（n）" label and its disabled state.
    int finishedTasks() const;
public slots:
    // --- QML actions ---------------------------------------------------------
    void setTab(const QString& tab);
    void setBackendFilter(const QString& filter);
    // Single-select manager filter: "all" | "scoop" | "winget". Replaces the two
    // independent toggles, whose "both on" state doubled as "all" and left three
    // buttons lit at once.
    void setManagerFilter(const QString& filter);
    // "all" | "managed" | "user". A manager-owned row is one the backend records
    // as its own; a "user" row is one it merely observed (ARP/MSIX), which the
    // user installed outside any package manager.
    void setSourceFilter(const QString& filter);
    // "new" (default) | "all" | "installed". Discover shows search results, so
    // repeating the installed inventory there is noise; the default hides what is
    // already installed and the other two values let the user see it on purpose.
    void setDiscoverFilter(const QString& filter);
    void setSearchText(const QString& text);
    // Narrows the Installed list locally. No process runs and nothing is queried:
    // the rows are already in memory, so this is a pure filter and can update on
    // every keystroke.
    void setInstalledFilter(const QString& text);
    void selectRow(int row);
    void toggleRow(int row);
    // Drops every checked row. The selection bar's 清除选择 button needs this;
    // without it the only way to clear was to uncheck rows one at a time.
    void clearSelection();
    // Copies text to the system clipboard. The context menu's "copy exact ID" is
    // the only way to get an identity out of the window without retyping it.
    void copyToClipboard(const QString& text);
    // Opens a URL from package metadata. Only an absolute http(s) URL is opened:
    // every one of these comes from a third-party manifest.
    Q_INVOKABLE void openUrl(const QString& url);
    // Copies the whole `winget show` record as text, so the window's content can
    // leave the window without retyping it.
    Q_INVOKABLE void copyPackageInfo();
    // Copies one task's whole redacted log, so a failure can be pasted into a bug
    // report instead of being read off the screen. The log is already redacted
    // when it is written, so nothing here needs to sanitise it again.
    Q_INVOKABLE void copyTaskLog(qulonglong id);
    // Opens the package's project/home page in the default browser. Read-only and
    // outside any manager: for a package that is not installed yet, looking it up
    // is often what the user wants instead of an immediate install.
    void openHomepage(int row);
    // Reads the backend's own description of a package (`winget show` / `scoop
    // info`) off the GUI thread and stores it for the details pane. This is where
    // the homepage, publisher, license and installer hash come from, so the
    // project-page action can use the real URL instead of a search guess.
    void requestPackageInfo(int row);
    // Opens the review page for one row with a chosen action, in the right order:
    // the row is checked first, then the plan is built, then the action is applied
    // against the actions the plan actually offers. Calling setReviewAction before
    // openReview() found an empty action list, dropped the request, and left the
    // dialog with no action — which is what a details-pane button did.
    void openReviewFor(int row, const QString& actionId);
    void openReview();
    void closeReview();
    void setSourceAgreed(bool agreed);
    void setInstallerAgreed(bool agreed);
    void confirmAndQueue();
    // Choosing the action is the user's decision, not a plugin inference: the
    // same installed package can be upgraded or uninstalled.
    void setReviewAction(const QString& actionId);
    // Per-task operations, all by id. The old versions scanned for "the last
    // one", which let the wrong row be cancelled or removed.
    void cancelTask(qulonglong id);
    void removeTask(qulonglong id);
    void retryTask(qulonglong id);
    void startTask(qulonglong id);
    void clearFinishedTasks();
    void setQueuePaused(bool paused);
    void toggleQueuePaused();
    bool queuePaused() const { return m_queue.paused(); }
    // Kept for compatibility with an older QML build; both now operate on the
    // only sensible target and say so in the status line.
    void removeLastPendingTask();
    void requestCancelLastRunning();
    void retryAction();
signals:
    void appearanceChanged();
    void tabChanged();
    void filterChanged();
    void statusChanged();
    void selectionChanged();
    void copyNoticeChanged();
    void searchStateChanged();
    void packageInfoChanged();
    void statePaneChanged();
    void reviewChanged();
    void tasksChanged();
    // Emitted when the window should be shown or hidden (GUI thread only).
    void windowShouldShow();
    void windowShouldClose();

private:
    void rebuildRows();
    void rebuildDuplicates();
    // True when the installed snapshot already contains this exact identity. Used
    // by the Discover filter, which needs to know whether a search result is
    // something the user already has.
    bool isInstalled(const PackageKey& key) const;
    void startBackendRefresh();
    void startSearchWorker();
    // Own detached-style workers until completion. The host may shut down the
    // plugin while a queued installer is open; the controller signals cancel and
    // joins workers before its QObject lifetime ends, so their queued callbacks
    // can never dereference a destroyed controller.
    void spawnWorker(std::function<void()> body);
    void applySearchResults(const QString& needle, const Snapshot& wingetSearch, const Snapshot& scoopSearch);
    void applyRefreshResults(const ProbeResult& wingetProbe, const ProbeResult& scoopProbe,
                             const Snapshot& wingetInstalled, const Snapshot& scoopInstalled,
                             const Snapshot& wingetUpdates, const Snapshot& scoopUpdates);
    // The mutation executor: picks the next runnable task and runs its commands
    // on a worker thread. confirmAndQueue starts it; it drains until every lane
    // is idle, then refreshes the inventory so the list reflects what really
    // happened.
    void drainQueue();
    void runOneTask(unsigned long long id);
    void onTaskFinished(unsigned long long id);
    // Disk cache: the last successful scan, so a fresh process paints rows
    // instead of re-running winget list before the window can show anything.
    QString cachePath() const;
    void loadCache();
    void saveCache();
    // Recomputes the plan/facts for m_reviewPackage with the given action and
    // republishes the review. Used by openReview and setReviewAction so the two
    // paths can never disagree.
    void buildReview(Action action);
    // Appends one redacted line to a task's log. Every log write goes through
    // here: a task log can carry local paths and backend output, and neither may
    // reach the UI unredacted.
    void logLine(unsigned long long id, const char* phase, const char* kind,
                 const std::string& text, int exitCode = -1);
    void setStatePane(const QString& pane, const QString& why);
    bool chinese() const;
    QString text(const char* zh, const char* en) const;
    void resetReview();
    Adapter& adapterFor(Manager manager);

    PackagesModel* m_packages = nullptr;
    TasksModel* m_tasks = nullptr;

    Snapshot m_installed;
    Snapshot m_updates;
    Snapshot m_scoopInstalled;
    Snapshot m_search;
    ProbeResult m_wingetProbe;
    ProbeResult m_scoopProbe;

    Queue m_queue;
    Plan m_reviewPlan;
    // Set by requestCancelLastRunning; the executor polls it between phases.
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic_bool> done;
    };
    std::atomic_bool m_shuttingDown{false};
    mutable std::mutex m_workerMutex;
    std::vector<Worker> m_workers;
    std::shared_ptr<std::atomic_bool> m_runCancel;

    QString m_locale = QStringLiteral("zh");
    bool m_dark = false;
    QString m_tab = QStringLiteral("installed");
    QString m_backendFilter = QStringLiteral("all");
    QString m_managerFilter = QStringLiteral("all");
    unsigned long long m_selectedTaskId = 0;
    QString m_selectedTaskLabel;
    int m_reviewRow = -1;
    // The backend's own description of one package (homepage, publisher, license,
    // installer hash). Kept as the map the details pane reads, plus the row it
    // belongs to, so a stale answer for a different row is never shown.
    QVariantMap m_packageInfo;
    bool m_packageInfoBusy = false;
    int m_packageInfoRow = -1;
    // The exact id the in-flight or completed lookup belongs to. Row numbers are
    // not a stable identity across a rebuild, so a stale reply is detected by id.
    QString m_packageInfoId;
    QString m_sourceFilter = QStringLiteral("all");
    QString m_installedFilter;
    QString m_discoverFilter = QStringLiteral("all");
    // Checked once at startup: elevation cannot change while the process lives.
    bool m_elevated = false;
    QString m_searchText;
    QString m_copyNotice;
    QString m_statusLine;
    QString m_statePane;
    QString m_stateWhy;
    QString m_blockedReason;
    bool m_busy = false;
    // Development harness only: when true, no backend process is started at all,
    // so the window is fully determined by the injected snapshot.
    bool m_fakeMode = false;

    bool m_reviewVisible = false;
    bool m_reviewConfirmEnabled = false;
    bool m_sourceAgreed = false;
    bool m_installerAgreed = false;
    bool m_needsSourceAgreement = false;
    QString m_reviewAction;
    QVariantList m_reviewActions;
    QVariantMap m_reviewFacts;
    // What the review page was opened on, so switching the action can recompute
    // the plan without re-reading the model.
    Package m_reviewPackage;

    UiSettings m_settings;
    bool m_windowOpen = false;
    bool m_refreshInFlight = false;
    // A search is running on the worker thread / a keystroke is waiting for one.
    bool m_searchInFlight = false;
    bool m_searchPending = false;
    // When the last successful scan finished; openWindow skips the probe within
    // refreshMinutes of it. Invalid until the first successful scan.
    QDateTime m_lastRefresh;
    QVariantMap m_look;
    QObject* m_theme = nullptr;
};

// Creates the window on the GUI thread and loads the QML scene. Returns false and
// fills `error` when the scene cannot be created.
//
// `qmlSourceDir` is empty in the shipped plugin and the scene then loads from the
// qrc resource, exactly as before. A development harness passes a directory so the
// scene loads from disk instead: that is what makes editing a .qml file and seeing
// the result without rebuilding possible. Nothing in the plugin passes it, so the
// delivered binary has one code path.
bool initializeUi(Controller* controller, QQuickWindow*& window, std::string& error,
                  const QString& qmlSourceDir = QString());

} // namespace pkg::ui
