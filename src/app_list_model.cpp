#include "app_list_model.h"

AppListModel::AppListModel(QObject *parent)
    : QAbstractListModel(parent) {}

int AppListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant AppListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    if (role == ModelDataRole || role == AppDataRole) {
        return QVariant::fromValue(static_cast<QObject*>(m_items.at(index.row())));
    }
    return QVariant();
}

QHash<int, QByteArray> AppListModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[ModelDataRole] = "modelData";
    roles[AppDataRole] = "appData";
    return roles;
}

void AppListModel::append(AppItem *item) {
    if (!item) return;
    int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.append(item);
    endInsertRows();
}

void AppListModel::insert(int index, AppItem *item) {
    if (!item) return;
    if (index < 0 || index > m_items.size()) index = m_items.size();
    beginInsertRows(QModelIndex(), index, index);
    m_items.insert(index, item);
    endInsertRows();
}

void AppListModel::removeAt(int index) {
    if (index < 0 || index >= m_items.size()) return;
    beginRemoveRows(QModelIndex(), index, index);
    m_items.removeAt(index);
    endRemoveRows();
}

void AppListModel::move(int from, int to) {
    if (from < 0 || from >= m_items.size() || to < 0 || to >= m_items.size() || from == to) return;
    int destChild = (to > from) ? to + 1 : to;
    if (beginMoveRows(QModelIndex(), from, from, QModelIndex(), destChild)) {
        m_items.move(from, to);
        endMoveRows();
    }
}

void AppListModel::clear() {
    if (m_items.isEmpty()) return;
    beginResetModel();
    qDeleteAll(m_items);
    m_items.clear();
    endResetModel();
}

AppItem* AppListModel::at(int index) const {
    if (index < 0 || index >= m_items.size()) return nullptr;
    return m_items.at(index);
}
