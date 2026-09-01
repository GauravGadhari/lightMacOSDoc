#pragma once

#include <QObject>
#include <QList>
#include <QTimer>
#include <QProcess>
#include <QQuickWindow>
#include <QRegion>
#include <QUrl>
#include <QDBusInterface>
#include <QDBusReply>
#include <QTemporaryFile>
#include "app_item.h"

class DockManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<QObject*> apps READ apps NOTIFY appsChanged)
    Q_PROPERTY(bool isDarkTheme READ isDarkTheme WRITE setIsDarkTheme NOTIFY isDarkThemeChanged)
    Q_PROPERTY(double baseIconWidth READ baseIconWidth WRITE setBaseIconWidth NOTIFY baseIconWidthChanged)
    Q_PROPERTY(double maxMagnification READ maxMagnification WRITE setMaxMagnification NOTIFY maxMagnificationChanged)
    Q_PROPERTY(bool isOnDesktop READ isOnDesktop NOTIFY isOnDesktopChanged)
    Q_PROPERTY(bool isMenuOpen READ isMenuOpen WRITE setIsMenuOpen NOTIFY isMenuOpenChanged)

public:
    explicit DockManager(QObject *parent = nullptr);
    ~DockManager() override;

    QList<QObject*> apps() const { return m_apps; }
    bool isDarkTheme() const { return m_isDarkTheme; }
    double baseIconWidth() const { return m_baseIconWidth; }
    double maxMagnification() const { return m_maxMagnification; }
    bool isOnDesktop() const { return m_isOnDesktop; }
    bool isMenuOpen() const { return m_isMenuOpen; }

    void setIsDarkTheme(bool isDark);
    void setBaseIconWidth(double width);
    void setMaxMagnification(double mag);
    void setIsMenuOpen(bool open);

    Q_INVOKABLE void setWindow(QQuickWindow *win);
    Q_INVOKABLE void updateMask(int x, int y, int width, int height);
    Q_INVOKABLE void resetMask();
    Q_INVOKABLE void setAutoHidden(bool hidden);

    Q_INVOKABLE void launchOrToggleApp(const QString &id);
    Q_INVOKABLE void launchNewInstance(const QString &id);
    Q_INVOKABLE void minimizeApp(const QString &id);
    Q_INVOKABLE void closeApp(const QString &id);
    Q_INVOKABLE void launchCommand(const QString &command);
    Q_INVOKABLE void quitDock();
    Q_INVOKABLE void refreshRunningStatus();
    Q_INVOKABLE void checkDesktopState();

    // ── Customization & Management ──
    Q_INVOKABLE void moveApp(int fromIndex, int toIndex);
    Q_INVOKABLE void removeApp(int index);
    Q_INVOKABLE void removeAppById(const QString &id);
    Q_INVOKABLE void addApp(const QString &id, const QString &title, const QString &icon, const QString &execCommand, bool dockBreaksBefore = false);
    Q_INVOKABLE void addAppsFromUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void addAppFromText(const QString &text);
    Q_INVOKABLE void toggleDividerBefore(const QString &id);
    Q_INVOKABLE void resetToDefaultApps();

signals:
    void appsChanged();
    void isDarkThemeChanged();
    void baseIconWidthChanged();
    void maxMagnificationChanged();
    void isOnDesktopChanged();
    void isMenuOpenChanged();
    void appLaunched(const QString &id);

private:
    void initDefaultApps();
    void saveApps();
    bool loadApps();
    QString configFilePath() const;
    QString parseDesktopFile(const QString &path, QString &title, QString &icon, QString &exec);
    QString resolveSystemIcon(const QString &iconName);
    QString getAppQuery(const QString &id);
    bool runKWinScript(const QString &scriptCode, QString *outResult = nullptr);

    QList<QObject*> m_apps;
    QTimer *m_pollTimer = nullptr;
    QQuickWindow *m_window = nullptr;
    bool m_isDarkTheme = true;
    bool m_isAutoHidden = false;
    bool m_isOnDesktop = true;
    bool m_isMenuOpen = false;
    double m_baseIconWidth = 57.6;
    double m_maxMagnification = 2.0;
};
