// models.h — Qt list models over the pure core.
//
// The models only reflect state that the controller computed. They never query a
// backend and never run anything, so the UI cannot cause a side effect by
// scrolling, selecting or expanding a row.
#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVector>

#include "../core/model.h"
#include "../core/queue.h"

namespace pkg::ui {

// One row on Installed / Discover / Updates. Field names are the QML roles.
class PackagesModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        ExactIdRole,
        ManagerRole,
        SourceRole,
        ScopeRole,
        ArchRole,
        InstalledRole,
        AvailableRole,
        ProvenanceRole,
        HoldRole,
        UnknownVersionRole,
        DuplicateRole,
        KeyRole,
        SelectedRole,
        ActionableRole,
        LimitsRole,
        // Per-action capability, so a row's context menu offers exactly what that
        // row can do. Deriving it in QML from the version strings would duplicate
        // the rule the plan builder already owns.
        CanUpgradeRole,
        CanUninstallRole,
        CanInstallRole,
    };

    explicit PackagesModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const std::vector<Package>& rows);
    const std::vector<Package>& rows() const { return m_rows; }

    // Selection is presentation state; it never implies an action.
    Q_INVOKABLE void setSelected(int row);
    Q_INVOKABLE int selected() const { return m_selected; }
    Q_INVOKABLE void toggleChecked(int row);
    Q_INVOKABLE void clearChecked();
    Q_INVOKABLE int checkedCount() const;
    Q_INVOKABLE bool isChecked(int row) const;
    // What this row can do, as a map: { canUpgrade, canUninstall, canInstall }.
    // The context menu needs it, and it must come from the same Package fields the
    // plan builder reads — recomputing it in QML from version strings would let the
    // menu and the review page disagree about which actions are legal.
    Q_INVOKABLE QVariantMap capabilitiesAt(int row) const;
    Q_INVOKABLE QString keyAt(int row) const;
    // Named field lookup for QML: role numbers are `Qt::UserRole + n`, which QML
    // cannot recompute reliably, so the C++ side resolves the name instead.
    Q_INVOKABLE QString fieldAt(int row, const QString& field) const;
    // The single checked row, or an empty key when none/many are checked: the
    // review page shows one exact identity at a time.
    Q_INVOKABLE QString singleCheckedKey() const;
    // Index of the only checked row, or -1 when none or more than one is checked.
    Q_INVOKABLE int singleCheckedRow() const;

    // Duplicate hints are computed from the whole snapshot so a row can show
    // "possible duplicate in <manager>" without merging anything.
    void setDuplicateHints(const std::vector<std::vector<std::size_t>>& hints);

signals:
    void checkedCountChanged();

private:
    std::vector<Package> m_rows;
    std::vector<std::vector<std::size_t>> m_duplicates;
    QVector<bool> m_checked;
    int m_selected = -1;
};

// One row on the Tasks page.
class TasksModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        ManagerRole,
        TargetRole,
        ScopeRole,
        ActionRole,
        StateRole,
        StateTextRole,
        NoteRole,
        StageRole,
        CanRemoveRole,
        CanCancelRole,
        CanRetryRole,
        CanStartRole,
        IndeterminateRole,
        LogCountRole,
        LogTextRole,
    };

    explicit TasksModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setTasks(const std::vector<Task>& tasks);
    // The task id at a row, or 0. The Tasks page's selection is reported to the
    // controller by id (the row index changes as the queue drains), and computing
    // the role number in QML would hard-code an enum value.
    Q_INVOKABLE qulonglong idAt(int row) const;

signals:
    void activeCountChanged(int count);

private:
    std::vector<Task> m_tasks;
};

// Localized text for a task state. Kept out of the model so the same string
// table can be used by tests and by the log export.
QString taskStateText(TaskState state, bool chinese);
QString taskNoteText(const Task& task, bool chinese);
QString backendStateText(BackendState state, bool chinese);

} // namespace pkg::ui
