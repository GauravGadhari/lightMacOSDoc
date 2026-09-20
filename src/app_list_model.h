#pragma once

#include <QAbstractListModel>
#include <QList>
#include "app_item.h"

class AppListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        ModelDataRole = Qt::UserRole + 1,
        AppDataRole
    };

    explicit AppListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(AppItem *item);
    void insert(int index, AppItem *item);
    void removeAt(int index);
    void move(int from, int to);
    void clear();

    AppItem* at(int index) const;
    int size() const { return m_items.size(); }
    bool isEmpty() const { return m_items.isEmpty(); }
    const QList<AppItem*>& items() const { return m_items; }
    int indexOf(AppItem *item) const { return m_items.indexOf(item); }

private:
    QList<AppItem*> m_items;
};
