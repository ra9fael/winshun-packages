// models.cpp — see models.h.
#include "models.h"

#include <QLocale>
#include <QStringList>

#include "../core/version.h"

namespace pkg::ui {
namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

QString provenanceText(Provenance p, bool zh)
{
    switch (p) {
    case Provenance::BackendRecorded: return zh ? QStringLiteral("backend 记录") : QStringLiteral("backend-recorded");
    case Provenance::Matched: return zh ? QStringLiteral("目录匹配") : QStringLiteral("matched");
    case Provenance::Observed: return zh ? QStringLiteral("仅枚举") : QStringLiteral("observed");
    case Provenance::None: break;
    }
    return zh ? QStringLiteral("未知") : QStringLiteral("unknown");
}

QString actionText(Action a, bool zh)
{
    switch (a) {
    case Action::Install: return zh ? QStringLiteral("安装") : QStringLiteral("Install");
    case Action::Uninstall: return zh ? QStringLiteral("卸载") : QStringLiteral("Uninstall");
    case Action::Upgrade: return zh ? QStringLiteral("升级") : QStringLiteral("Upgrade");
    case Action::Reinstall: return zh ? QStringLiteral("重装") : QStringLiteral("Reinstall");
    case Action::None: break;
    }
    return {};
}

} // namespace

// ------------------------------------------------------------------ packages

PackagesModel::PackagesModel(QObject* parent) : QAbstractListModel(parent) {}

int PackagesModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant PackagesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return {};
    const auto& p = m_rows[static_cast<std::size_t>(index.row())];
    const int row = index.row();

    switch (role) {
    case NameRole:
        return qs(displayName(p.name, p.key));
    case ExactIdRole:
        return qs(p.key.id);
    case ManagerRole:
        return qs(managerId(p.key.manager));
    case SourceRole:
        return qs(p.key.sourceOrUnknown());
    case ScopeRole:
        return qs(scopeId(p.key.scope));
    case ArchRole:
        return qs(p.arch.empty() ? std::string("unknown") : p.arch);
    case InstalledRole:
        return qs(p.installedVersion);
    case AvailableRole:
        return qs(p.availableVersion.empty() ? std::string("—") : p.availableVersion);
    case ProvenanceRole:
        return provenanceText(p.provenance, QLocale().language() == QLocale::Chinese);
    case HoldRole:
        return qs(p.holdPolicy);
    case UnknownVersionRole:
        return p.availableKind == "unknown";
    case DuplicateRole: {
        if (row >= static_cast<int>(m_duplicates.size()) || m_duplicates[static_cast<std::size_t>(row)].empty())
            return QString();
        const auto& dupRow = m_duplicates[static_cast<std::size_t>(row)].front();
        if (dupRow >= m_rows.size())
            return QString();
        return qs(managerId(m_rows[dupRow].key.manager));
    }
    case KeyRole:
        return qs(p.key.text());
    case SelectedRole:
        return row < m_checked.size() && m_checked[row];
    case ActionableRole:
        return p.canInstall || p.canUninstall || p.canUpgrade;
    case LimitsRole:
        return qs(p.limits);
    case CanUpgradeRole:
        return p.canUpgrade;
    case CanUninstallRole:
        return p.canUninstall;
    case CanInstallRole:
        return p.canInstall;
    default:
        return {};
    }
}

QHash<int, QByteArray> PackagesModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {ExactIdRole, "exactId"},
        {ManagerRole, "manager"},
        {SourceRole, "source"},
        {ScopeRole, "scope"},
        {ArchRole, "arch"},
        {InstalledRole, "installed"},
        {AvailableRole, "available"},
        {ProvenanceRole, "provenance"},
        {HoldRole, "hold"},
        {UnknownVersionRole, "unknownVersion"},
        {DuplicateRole, "duplicateIn"},
        {KeyRole, "packageKey"},
        {SelectedRole, "checked"},
        {ActionableRole, "actionable"},
        {LimitsRole, "limits"},
        {CanUpgradeRole, "canUpgrade"},
        {CanUninstallRole, "canUninstall"},
        {CanInstallRole, "canInstall"},
    };
}

void PackagesModel::setRows(const std::vector<Package>& rows)
{
    beginResetModel();
    m_rows = rows;
    m_checked = QVector<bool>(static_cast<int>(m_rows.size()), false);
    if (m_selected >= static_cast<int>(m_rows.size()))
        m_selected = -1;
    endResetModel();
    emit checkedCountChanged();
}

void PackagesModel::setDuplicateHints(const std::vector<std::vector<std::size_t>>& hints)
{
    m_duplicates = hints;
    if (!m_rows.empty())
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1), {DuplicateRole});
}

void PackagesModel::setSelected(int row)
{
    const int previous = m_selected;
    m_selected = row;
    if (previous >= 0 && previous < rowCount())
        emit dataChanged(index(previous), index(previous), {});
    if (row >= 0 && row < rowCount())
        emit dataChanged(index(row), index(row), {});
}

void PackagesModel::toggleChecked(int row)
{
    if (row < 0 || row >= m_checked.size())
        return;
    m_checked[row] = !m_checked[row];
    emit dataChanged(index(row), index(row), {SelectedRole});
    emit checkedCountChanged();
}

void PackagesModel::clearChecked()
{
    m_checked.fill(false);
    if (!m_rows.empty())
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1), {SelectedRole});
    emit checkedCountChanged();
}

int PackagesModel::checkedCount() const
{
    int n = 0;
    for (const bool checked : m_checked)
        if (checked)
            ++n;
    return n;
}

bool PackagesModel::isChecked(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_checked.size()))
        return false;
    return m_checked[static_cast<std::size_t>(row)];
}

QVariantMap PackagesModel::capabilitiesAt(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        out.insert(QStringLiteral("canUpgrade"), false);
        out.insert(QStringLiteral("canUninstall"), false);
        out.insert(QStringLiteral("canInstall"), false);
        return out;
    }
    const auto& p = m_rows[static_cast<std::size_t>(row)];
    out.insert(QStringLiteral("canUpgrade"), p.canUpgrade);
    out.insert(QStringLiteral("canUninstall"), p.canUninstall);
    out.insert(QStringLiteral("canInstall"), p.canInstall);
    return out;
}

QString PackagesModel::keyAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return {};
    return qs(m_rows[static_cast<std::size_t>(row)].key.text());
}

QString PackagesModel::fieldAt(int row, const QString& field) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return {};
    const auto& p = m_rows[static_cast<std::size_t>(row)];
    if (field == "name")
        return qs(displayName(p.name, p.key));
    if (field == "exactId")
        return qs(p.key.id);
    if (field == "manager")
        return qs(managerId(p.key.manager));
    if (field == "source")
        return qs(p.key.sourceOrUnknown());
    if (field == "scope")
        return qs(scopeId(p.key.scope));
    if (field == "arch")
        return qs(p.arch.empty() ? std::string("unknown") : p.arch);
    if (field == "installed")
        return qs(p.installedVersion);
    if (field == "available")
        return qs(p.availableVersion.empty() ? std::string("—") : p.availableVersion);
    if (field == "provenance")
        return provenanceText(p.provenance, QLocale().language() == QLocale::Chinese);
    if (field == "hold")
        return qs(p.holdPolicy.empty() ? std::string("—") : p.holdPolicy);
    if (field == "dependencies")
        return qs(p.dependencies);
    if (field == "material")
        return qs(p.material);
    if (field == "limits")
        return qs(p.limits);
    if (field == "agreements") {
        QStringList list;
        for (const auto& a : p.agreements)
            list << qs(a);
        return list.isEmpty() ? QStringLiteral("—") : list.join(QStringLiteral(", "));
    }
    if (field == "metadataAt")
        return qs(p.metadataAt);
    if (field == "installedTo")
        return (p.installedVersion.empty() ? QStringLiteral("—") : qs(p.installedVersion)) + QStringLiteral(" → ") +
               (p.availableVersion.empty() ? QStringLiteral("—") : qs(p.availableVersion));
    return {};
}

QString PackagesModel::singleCheckedKey() const
{
    const int row = singleCheckedRow();
    return row < 0 ? QString() : keyAt(row);
}

int PackagesModel::singleCheckedRow() const
{
    int found = -1;
    for (int i = 0; i < m_checked.size(); ++i) {
        if (!m_checked[i])
            continue;
        if (found >= 0)
            return -1;   // more than one: the review page needs one exact identity
        found = i;
    }
    return found;
}

// --------------------------------------------------------------------- tasks

TasksModel::TasksModel(QObject* parent) : QAbstractListModel(parent) {}

int TasksModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_tasks.size());
}

QVariant TasksModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_tasks.size()))
        return {};
    const auto& t = m_tasks[static_cast<std::size_t>(index.row())];
    const bool zh = QLocale().language() == QLocale::Chinese;

    switch (role) {
    case IdRole:
        return static_cast<qulonglong>(t.id);
    case ManagerRole:
        return qs(managerId(t.plan.key.manager));
    case TargetRole:
        return qs(t.plan.key.sourceOrUnknown() + "/" + t.plan.key.id);
    case ScopeRole:
        return qs(scopeId(t.plan.key.scope));
    case ActionRole:
        return actionText(t.plan.action, zh);
    case StateRole:
        return qs(taskStateId(t.state));
    case StateTextRole:
        return taskStateText(t.state, zh);
    case NoteRole:
        return taskNoteText(t, zh);
    case StageRole:
        return qs(t.phase);
    case CanRemoveRole:
        // Any record can be dropped except one that is running: that record is
        // the only sign that a process is still touching the machine.
        return t.state != TaskState::Running && t.state != TaskState::InteractivePending &&
               t.state != TaskState::CancelRequested;
    case CanCancelRole:
        return t.state == TaskState::Running || t.state == TaskState::InteractivePending;
    case CanRetryRole:
        return isTerminal(t.state) || t.state == TaskState::NeedsReview || t.state == TaskState::Blocked;
    case CanStartRole:
        return t.state == TaskState::Queued;
    case IndeterminateRole:
        // No reliable total: an indeterminate bar, never a made-up percentage.
        return t.state == TaskState::Running || t.state == TaskState::InteractivePending;
    case LogCountRole:
        return static_cast<int>(t.log.size());
    case LogTextRole: {
        // One string per line, "at\tphase\tkind\ttext"\t[-1|exit]: QML cannot
        // iterate a C++ vector from a role, so the log travels as lines and the
        // delegate splits them. Bounded because the queue caps the entries.
        QString out;
        for (const auto& e : t.log) {
            if (!out.isEmpty())
                out += QLatin1Char('\n');
            out += qs(e.at) + QLatin1Char('\t') + qs(e.phase) + QLatin1Char('\t') + qs(e.kind) +
                   QLatin1Char('\t') + qs(e.text) + QLatin1Char('\t') + QString::number(e.exitCode);
        }
        return out;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> TasksModel::roleNames() const
{
    return {
        {IdRole, "taskId"},
        {ManagerRole, "manager"},
        {TargetRole, "target"},
        {ScopeRole, "scope"},
        {ActionRole, "action"},
        {StateRole, "state"},
        {StateTextRole, "stateText"},
        {NoteRole, "note"},
        {StageRole, "stage"},
        {CanRemoveRole, "canRemove"},
        {CanCancelRole, "canCancel"},
        {CanRetryRole, "canRetry"},
        {CanStartRole, "canStart"},
        {IndeterminateRole, "indeterminate"},
        {LogCountRole, "logCount"},
        {LogTextRole, "logText"},
    };
}

qulonglong TasksModel::idAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_tasks.size()))
        return 0;
    return m_tasks[static_cast<std::size_t>(row)].id;
}

void TasksModel::setTasks(const std::vector<Task>& tasks)
{
    beginResetModel();
    m_tasks = tasks;
    endResetModel();
    int active = 0;
    for (const auto& t : m_tasks)
        if (isActive(t.state))
            ++active;
    emit activeCountChanged(active);
}

// --------------------------------------------------------------------- texts

QString taskStateText(TaskState state, bool zh)
{
    switch (state) {
    case TaskState::Queued: return zh ? QStringLiteral("排队中") : QStringLiteral("Queued");
    case TaskState::Preflight: return zh ? QStringLiteral("预检中") : QStringLiteral("Preflight");
    case TaskState::NeedsReview: return zh ? QStringLiteral("需要重新审阅") : QStringLiteral("Needs review");
    case TaskState::Blocked: return zh ? QStringLiteral("阻塞") : QStringLiteral("Blocked");
    case TaskState::Running: return zh ? QStringLiteral("运行中") : QStringLiteral("Running");
    case TaskState::InteractivePending:
        return zh ? QStringLiteral("等待交互安装器") : QStringLiteral("Interactive pending");
    case TaskState::CancelRequested:
        return zh ? QStringLiteral("已请求取消") : QStringLiteral("Cancel requested");
    case TaskState::RebootRequired:
        return zh ? QStringLiteral("需要重启") : QStringLiteral("Reboot required");
    case TaskState::Succeeded: return zh ? QStringLiteral("成功") : QStringLiteral("Succeeded");
    case TaskState::Failed: return zh ? QStringLiteral("失败") : QStringLiteral("Failed");
    case TaskState::PartialFailure:
        return zh ? QStringLiteral("部分失败") : QStringLiteral("Partial failure");
    case TaskState::Cancelled: return zh ? QStringLiteral("已取消") : QStringLiteral("Cancelled");
    case TaskState::Interrupted:
        return zh ? QStringLiteral("中断 / 未知") : QStringLiteral("Interrupted / unknown");
    }
    return {};
}

QString taskNoteText(const Task& task, bool zh)
{
    // A partial failure from a reinstall is the one case where the package may
    // now be absent: say so, and never present it as a clean cancellation.
    if (task.state == TaskState::PartialFailure) {
        return zh ? QStringLiteral("已成功卸载但安装失败；软件可能已不存在，数据可能已受影响。已保留目标版本、材料与诊断。")
                  : QStringLiteral("Removed successfully but the install failed; the package may be absent and data "
                                   "may be affected. Target version, materials and diagnostics are kept.");
    }
    if (task.state == TaskState::Interrupted) {
        return zh ? QStringLiteral("宿主退出或插件被禁用导致中断；实际状态未知，不会自动重放。")
                  : QStringLiteral("Interrupted by host exit or plugin disable; the real state is unknown and nothing "
                                   "is replayed.");
    }
    if (task.state == TaskState::CancelRequested) {
        return zh ? QStringLiteral("已请求取消：可能只停止等待，安装器未必结束，也不保证回滚。")
                  : QStringLiteral("Cancel requested: this may only stop waiting; the installer may keep running and "
                                   "nothing is rolled back.");
    }
    if (task.state == TaskState::InteractivePending) {
        return zh ? QStringLiteral("安装器已启动但没有输出；超时不判成功，也不会重复启动。")
                  : QStringLiteral("The installer started with no output; a timeout is not success and it is not "
                                   "relaunched.");
    }
    if (!task.blockedReason.empty())
        return qs(task.blockedReason);
    return qs(task.detail);
}

QString backendStateText(BackendState state, bool zh)
{
    switch (state) {
    case BackendState::Missing: return zh ? QStringLiteral("未找到") : QStringLiteral("not found");
    case BackendState::Denied: return zh ? QStringLiteral("拒绝 / 受限") : QStringLiteral("denied");
    case BackendState::Unsupported: return zh ? QStringLiteral("不支持的输出") : QStringLiteral("unsupported");
    case BackendState::Unavailable: return zh ? QStringLiteral("不可用") : QStringLiteral("unavailable");
    case BackendState::Ready: return zh ? QStringLiteral("可用") : QStringLiteral("ready");
    case BackendState::Unknown: break;
    }
    return zh ? QStringLiteral("未检测") : QStringLiteral("unknown");
}

} // namespace pkg::ui
