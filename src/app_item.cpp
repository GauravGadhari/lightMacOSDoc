#include "app_item.h"

AppItem::AppItem(const QString &id,
                 const QString &title,
                 const QString &icon,
                 const QString &execCommand,
                 bool dockBreaksBefore,
                 bool isSeparator,
                 QObject *parent)
    : QObject(parent),
      m_id(id),
      m_title(title),
      m_icon(icon),
      m_execCommand(execCommand),
      m_isSeparator(isSeparator),
      m_dockBreaksBefore(dockBreaksBefore) {}

void AppItem::setTitle(const QString &title) {
    if (m_title != title) {
        m_title = title;
        emit titleChanged();
    }
}

void AppItem::setIcon(const QString &icon) {
    if (m_icon != icon) {
        m_icon = icon;
        emit iconChanged();
    }
}

void AppItem::setExecCommand(const QString &exec) {
    if (m_execCommand != exec) {
        m_execCommand = exec;
        emit execCommandChanged();
    }
}

void AppItem::setIsRunning(bool running) {
    if (m_isRunning != running) {
        m_isRunning = running;
        emit isRunningChanged();
    }
}

void AppItem::setIsActive(bool active) {
    if (m_isActive != active) {
        m_isActive = active;
        emit isActiveChanged();
    }
}

void AppItem::setBadgeCount(int count) {
    if (m_badgeCount != count) {
        m_badgeCount = count;
        emit badgeCountChanged();
    }
}

void AppItem::setDockBreaksBefore(bool breaks) {
    if (m_dockBreaksBefore != breaks) {
        m_dockBreaksBefore = breaks;
        emit dockBreaksBeforeChanged();
    }
}

QJsonObject AppItem::toJson() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["title"] = m_title;
    obj["icon"] = m_icon;
    obj["execCommand"] = m_execCommand;
    obj["dockBreaksBefore"] = m_dockBreaksBefore;
    obj["isSeparator"] = m_isSeparator;
    return obj;
}

AppItem* AppItem::fromJson(const QJsonObject &json, QObject *parent) {
    QString id = json["id"].toString();
    QString title = json["title"].toString();
    QString icon = json["icon"].toString();
    QString exec = json["execCommand"].toString();
    bool breaks = json["dockBreaksBefore"].toBool();
    bool isSep = json["isSeparator"].toBool();

    if (id.isEmpty()) return nullptr;
    return new AppItem(id, title, icon, exec, breaks, isSep, parent);
}
