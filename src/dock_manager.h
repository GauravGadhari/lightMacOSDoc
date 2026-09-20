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
#include <QDBusConnection>
#include <QTemporaryFile>
#include <QVariantList>
#include <QSet>
#include "app_item.h"
#include "app_list_model.h"

class DockManager : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.MacOSDock")

    Q_PROPERTY(QAbstractItemModel* appsModel READ appsModel CONSTANT)
    Q_PROPERTY(QList<QObject*> apps READ apps NOTIFY appsChanged)
    Q_PROPERTY(int appCount READ appCount NOTIFY appsChanged)
    Q_PROPERTY(bool isDarkTheme READ isDarkTheme WRITE setIsDarkTheme NOTIFY isDarkThemeChanged)
    Q_PROPERTY(bool isAutostartEnabled READ isAutostartEnabled WRITE setAutostartEnabled NOTIFY autostartEnabledChanged)
    Q_PROPERTY(double baseIconWidth READ baseIconWidth WRITE setBaseIconWidth NOTIFY baseIconWidthChanged)
    Q_PROPERTY(double maxMagnification READ maxMagnification WRITE setMaxMagnification NOTIFY maxMagnificationChanged)
    Q_PROPERTY(bool isMenuOpen READ isMenuOpen WRITE setIsMenuOpen NOTIFY isMenuOpenChanged)
    Q_PROPERTY(bool hasLaunchingApp READ hasLaunchingApp NOTIFY hasLaunchingAppChanged)

public:
    explicit DockManager(QObject *parent = nullptr);
    ~DockManager() override;

    QAbstractItemModel* appsModel() const { return m_appModel; }
    QList<QObject*> apps() const;
    int appCount() const;
    bool isDarkTheme() const { return m_isDarkTheme; }
    bool isAutostartEnabled() const;
    double baseIconWidth() const { return m_baseIconWidth; }
    double maxMagnification() const { return m_maxMagnification; }
    bool isMenuOpen() const { return m_isMenuOpen; }
    bool hasLaunchingApp() const { return !m_launchingAppIds.isEmpty(); }

    void setIsDarkTheme(bool isDark);
    void setAutostartEnabled(bool enabled);
    void setBaseIconWidth(double width);
    void setMaxMagnification(double mag);
    Q_INVOKABLE void setIsMenuOpen(bool open);

    Q_INVOKABLE void toggleAutostart();

    Q_INVOKABLE void setWindow(QQuickWindow *win);
    Q_INVOKABLE void updateMask(int x, int y, int width, int height);
    Q_INVOKABLE void resetMask();
    Q_INVOKABLE void setAutoHidden(bool hidden);

    // ── Customization & Management ──
    Q_INVOKABLE void moveApp(int fromIndex, int toIndex);
    Q_INVOKABLE void removeApp(int index);
    Q_INVOKABLE void removeAppById(const QString &id);
    Q_INVOKABLE void addApp(const QString &id, const QString &title, const QString &icon, const QString &execCommand, bool dockBreaksBefore = false);
    Q_INVOKABLE void addAppsFromUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void addAppFromText(const QString &text);
    Q_INVOKABLE void toggleDividerBefore(const QString &id);
    Q_INVOKABLE void resetToDefaultApps();
    Q_INVOKABLE void cleanupKWinWindowTracker();
    Q_INVOKABLE void finalizeRemoveApp(const QString &id);

public Q_SLOTS:
    void updateWindows(const QString &json);
    void launchOrToggleApp(const QString &id);
    void launchNewInstance(const QString &id);
    void minimizeApp(const QString &id);
    void closeApp(const QString &id);
    void activateWindow(const QString &windowId);
    void closeWindowById(const QString &windowId);
    void pinApp(const QString &id);
    void unpinApp(const QString &id);
    void showAllWindows(const QString &id);
    void dismissAllMenus();
    void launchCommand(const QString &command);
    void quitDock();
    void refreshRunningStatus();

signals:
    void appsChanged();
    void isDarkThemeChanged();
    void autostartEnabledChanged();
    void baseIconWidthChanged();
    void maxMagnificationChanged();
    void isMenuOpenChanged();
    void dismissPopupsRequested();
    void appLaunched(const QString &id);
    void appLaunchStarted(const QString &id);
    void appSwitched(const QString &id);
    void appLaunchFinished(const QString &id);
    void hasLaunchingAppChanged();
    void mouseLeftWindow();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initDefaultApps();
    void saveApps();
    bool loadApps();
    QString configFilePath() const;
    QString parseDesktopFile(const QString &path, QString &title, QString &icon, QString &exec);
    QString resolveSystemIcon(const QString &iconName);
    QString getAppQuery(const QString &id);
    void setupKWinWindowTracker();
    void checkTrackerHealth();
    void matchWindowsToApps(const QJsonArray &windowList);
    bool executeKWinAction(const QString &scriptCode);
    bool runKWinScript(const QString &scriptCode, QString *outResult = nullptr);

    AppListModel *m_appModel = nullptr;
    QSet<QString> m_launchingAppIds;
    QTimer *m_pollTimer = nullptr;
    QQuickWindow *m_window = nullptr;
    int m_kwinTrackerScriptId = -1;
    bool m_isDarkTheme = true;
    bool m_isAutoHidden = false;
    bool m_isMenuOpen = false;
    double m_baseIconWidth = 57.6;
    double m_maxMagnification = 2.0;
};
