#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QVariantList>

class AppItem : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QString icon READ icon WRITE setIcon NOTIFY iconChanged)
    Q_PROPERTY(QString execCommand READ execCommand WRITE setExecCommand NOTIFY execCommandChanged)
    Q_PROPERTY(bool isRunning READ isRunning WRITE setIsRunning NOTIFY isRunningChanged)
    Q_PROPERTY(bool isActive READ isActive WRITE setIsActive NOTIFY isActiveChanged)
    Q_PROPERTY(int badgeCount READ badgeCount WRITE setBadgeCount NOTIFY badgeCountChanged)
    Q_PROPERTY(bool isSeparator READ isSeparator CONSTANT)
    Q_PROPERTY(bool dockBreaksBefore READ dockBreaksBefore WRITE setDockBreaksBefore NOTIFY dockBreaksBeforeChanged)
    Q_PROPERTY(bool isPinned READ isPinned WRITE setIsPinned NOTIFY isPinnedChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY windowsChanged)
    Q_PROPERTY(int windowCount READ windowCount NOTIFY windowsChanged)

public:
    explicit AppItem(const QString &id,
                     const QString &title,
                     const QString &icon,
                     const QString &execCommand = QString(),
                     bool dockBreaksBefore = false,
                     bool isSeparator = false,
                     bool isPinned = true,
                     QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString title() const { return m_title; }
    QString icon() const { return m_icon; }
    QString execCommand() const { return m_execCommand; }
    bool isRunning() const { return m_isRunning; }
    bool isActive() const { return m_isActive; }
    int badgeCount() const { return m_badgeCount; }
    bool isSeparator() const { return m_isSeparator; }
    bool dockBreaksBefore() const { return m_dockBreaksBefore; }
    bool isPinned() const { return m_isPinned; }
    QVariantList windows() const { return m_windows; }
    int windowCount() const { return m_windows.size(); }

    void setTitle(const QString &title);
    void setIcon(const QString &icon);
    void setExecCommand(const QString &exec);
    void setIsRunning(bool running);
    void setIsActive(bool active);
    void setBadgeCount(int count);
    void setDockBreaksBefore(bool breaks);
    void setIsPinned(bool pinned);
    void setWindows(const QVariantList &windows);

    QJsonObject toJson() const;
    static AppItem* fromJson(const QJsonObject &json, QObject *parent = nullptr);

signals:
    void titleChanged();
    void iconChanged();
    void execCommandChanged();
    void isRunningChanged();
    void isActiveChanged();
    void badgeCountChanged();
    void dockBreaksBeforeChanged();
    void isPinnedChanged();
    void windowsChanged();

private:
    QString m_id;
    QString m_title;
    QString m_icon;
    QString m_execCommand;
    bool m_isRunning = false;
    bool m_isActive = false;
    int m_badgeCount = 0;
    bool m_isSeparator = false;
    bool m_dockBreaksBefore = false;
    bool m_isPinned = true;
    QVariantList m_windows;
};
