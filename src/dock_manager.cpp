#include "dock_manager.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>

DockManager::DockManager(QObject *parent) : QObject(parent) {
    if (!loadApps()) {
        initDefaultApps();
        saveApps();
    }

    // Register D-Bus Service for KWin Scripting Bridge
    QDBusConnection::sessionBus().registerService("org.kde.MacOSDock");
    QDBusConnection::sessionBus().registerObject("/WindowTracker", this, QDBusConnection::ExportAllSlots);

    setupKWinWindowTracker();

    // Query windows shortly after startup to immediately populate running status & dots
    QTimer::singleShot(400, this, &DockManager::refreshRunningStatus);

    // Health check timer to verify KWin tracker is alive
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &DockManager::checkTrackerHealth);
    m_pollTimer->start(2000);
}

DockManager::~DockManager() {
    cleanupKWinWindowTracker();
    qDeleteAll(m_apps);
    m_apps.clear();
}

void DockManager::setWindow(QQuickWindow *win) {
    m_window = win;
}

void DockManager::setIsMenuOpen(bool open) {
    if (m_isMenuOpen != open) {
        m_isMenuOpen = open;
        emit isMenuOpenChanged();
    }
}

void DockManager::dismissAllMenus() {
    if (m_isMenuOpen) {
        m_isMenuOpen = false;
        emit isMenuOpenChanged();
    }
    emit dismissPopupsRequested();
}

void DockManager::setAutoHidden(bool hidden) {
    m_isAutoHidden = hidden;
    qDebug() << "[DockManager] setAutoHidden called:" << hidden;
}

void DockManager::updateMask(int x, int y, int width, int height) {
    if (!m_window) return;

    if (m_isAutoHidden) {
        return;
    }

    int winH = m_window->height();
    int winW = m_window->width();

    int maskX = qMax(0, x - 100);
    int maskW = qMin(winW - maskX, width + 200);
    int maskY = 0;
    int maskH = winH;

    QRegion dockRegion(maskX, maskY, maskW, maskH);
    QRegion triggerStrip(0, winH - 6, winW, 6);

    m_window->setMask(dockRegion.united(triggerStrip));
}

void DockManager::resetMask() {
    if (!m_window) return;

    int winW = m_window->width();
    m_window->setMask(QRegion(0, 0, winW, 6));
}

QString DockManager::configFilePath() const {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/macos-dock";
    QDir().mkpath(configDir);
    return configDir + "/apps.json";
}

void DockManager::saveApps() {
    QJsonArray array;
    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        // ONLY persist pinned apps
        if (item && item->isPinned()) {
            array.append(item->toJson());
        }
    }

    QJsonDocument doc(array);
    QFile file(configFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

bool DockManager::loadApps() {
    QFile file(configFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) return false;

    QJsonArray array = doc.array();
    if (array.isEmpty()) return false;

    qDeleteAll(m_apps);
    m_apps.clear();

    for (const QJsonValue &val : array) {
        if (val.isObject()) {
            AppItem *item = AppItem::fromJson(val.toObject(), this);
            if (item) {
                m_apps.append(item);
            }
        }
    }

    emit appsChanged();
    return !m_apps.isEmpty();
}

void DockManager::resetToDefaultApps() {
    initDefaultApps();
    saveApps();
}

void DockManager::moveApp(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= m_apps.size() || toIndex < 0 || toIndex >= m_apps.size() || fromIndex == toIndex) {
        return;
    }

    m_apps.move(fromIndex, toIndex);
    emit appsChanged();
    saveApps();
}

void DockManager::removeApp(int index) {
    if (index < 0 || index >= m_apps.size()) return;

    QObject *obj = m_apps.takeAt(index);
    emit appsChanged();
    obj->deleteLater();
    saveApps();
}

void DockManager::removeAppById(const QString &id) {
    for (int i = 0; i < m_apps.size(); ++i) {
        auto *item = qobject_cast<AppItem*>(m_apps.at(i));
        if (item && item->id() == id) {
            removeApp(i);
            return;
        }
    }
}

void DockManager::pinApp(const QString &id) {
    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (item && item->id() == id) {
            item->setIsPinned(true);
            saveApps();
            emit appsChanged();
            return;
        }
    }
}

void DockManager::unpinApp(const QString &id) {
    for (int i = 0; i < m_apps.size(); ++i) {
        auto *item = qobject_cast<AppItem*>(m_apps.at(i));
        if (item && item->id() == id) {
            if (item->windowCount() > 0) {
                item->setIsPinned(false);
                saveApps();
                emit appsChanged();
            } else {
                removeApp(i);
            }
            return;
        }
    }
}

void DockManager::showAllWindows(const QString &id) {
    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (item && item->id() == id && item->windowCount() > 0) {
            QVariantMap win = item->windows().first().toMap();
            activateWindow(win.value("id").toString());
            break;
        }
    }

    QDBusInterface accel("org.kde.kglobalaccel", "/component/kwin", "org.kde.kglobalaccel.Component", QDBusConnection::sessionBus());
    if (accel.isValid()) {
        accel.call("invokeShortcut", "ExposeClass");
    }
}

void DockManager::toggleDividerBefore(const QString &id) {
    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (item && item->id() == id) {
            item->setDockBreaksBefore(!item->dockBreaksBefore());
            saveApps();
            return;
        }
    }
}

void DockManager::addApp(const QString &id, const QString &title, const QString &icon, const QString &execCommand, bool dockBreaksBefore) {
    QString uniqueId = id;
    int counter = 1;
    bool exists = true;
    while (exists) {
        exists = false;
        for (QObject *obj : m_apps) {
            auto *item = qobject_cast<AppItem*>(obj);
            if (item && item->id() == uniqueId) {
                exists = true;
                uniqueId = QString("%1_%2").arg(id).arg(counter++);
                break;
            }
        }
    }

    auto *item = new AppItem(uniqueId, title, icon, execCommand, dockBreaksBefore, false, true, this);
    m_apps.append(item);
    emit appsChanged();
    saveApps();
}

QString DockManager::resolveSystemIcon(const QString &iconName) {
    if (iconName.isEmpty()) return "qrc:/icons/launchpad/256.png";
    if (iconName.startsWith("/") || iconName.startsWith("file://") || iconName.startsWith("qrc:/")) {
        return iconName.startsWith("/") ? ("file://" + iconName) : iconName;
    }

    QString cleanName = iconName;
    if (cleanName.endsWith(".png") || cleanName.endsWith(".svg") || cleanName.endsWith(".xpm")) {
        cleanName = QFileInfo(cleanName).completeBaseName();
    }

    // Special macOS Dock bundled overrides
    if (cleanName == "spectacle" || cleanName == "org.kde.spectacle" || cleanName == "accessories-screenshot") {
        return "qrc:/icons/spectacle/256.png";
    }

    QString home = QDir::homePath();
    QStringList searchPaths = {
        home + "/.local/share/icons/MacTahoe/apps/scalable/",
        home + "/.local/share/icons/MacTahoe-dark/apps/scalable/",
        home + "/.local/share/icons/WhiteSur/apps/scalable/",
        home + "/.local/share/icons/MacTahoe/apps/256x256/",
        home + "/.local/share/icons/MacTahoe-dark/apps/256x256/",
        home + "/.local/share/icons/WhiteSur/apps/256x256/",
        "/usr/share/icons/hicolor/256x256/apps/",
        "/usr/share/icons/hicolor/128x128/apps/",
        "/usr/share/icons/hicolor/scalable/apps/",
        "/usr/share/icons/hicolor/64x64/apps/",
        "/usr/share/icons/hicolor/48x48/apps/",
        "/usr/share/icons/breeze/apps/48/",
        "/usr/share/pixmaps/",
        home + "/.local/share/icons/hicolor/256x256/apps/",
        home + "/.local/share/icons/hicolor/scalable/apps/"
    };

    QStringList extensions = {".png", ".svg", ".xpm"};

    for (const QString &dir : searchPaths) {
        for (const QString &ext : extensions) {
            QString fullPath = dir + cleanName + ext;
            if (QFile::exists(fullPath)) {
                return "file://" + fullPath;
            }
        }
    }

    return "qrc:/icons/launchpad/256.png";
}

QString DockManager::parseDesktopFile(const QString &path, QString &title, QString &icon, QString &exec) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();

    bool inDesktopEntry = false;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line == "[Desktop Entry]") {
            inDesktopEntry = true;
            continue;
        } else if (line.startsWith("[") && line.endsWith("]")) {
            inDesktopEntry = false;
        }

        if (inDesktopEntry) {
            if (line.startsWith("Name=") && title.isEmpty()) {
                title = line.mid(5).trimmed();
            } else if (line.startsWith("Exec=") && exec.isEmpty()) {
                exec = line.mid(5).trimmed();
                exec.remove(QRegularExpression("%[fFuUickvm]"));
                exec = exec.trimmed();
            } else if (line.startsWith("Icon=") && icon.isEmpty()) {
                QString rawIcon = line.mid(5).trimmed();
                icon = resolveSystemIcon(rawIcon);
            }
        }
    }
    file.close();

    if (title.isEmpty()) {
        title = QFileInfo(path).baseName();
    }
    if (icon.isEmpty()) {
        icon = "qrc:/icons/launchpad/256.png";
    }
    if (exec.isEmpty()) {
        exec = "gtk-launch " + QFileInfo(path).fileName() + " || xdg-open " + path;
    }

    return QFileInfo(path).baseName().toLower();
}

void DockManager::addAppsFromUrls(const QList<QUrl> &urls) {
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            QString localPath = url.toLocalFile();
            QFileInfo fi(localPath);
            if (localPath.endsWith(".desktop", Qt::CaseInsensitive)) {
                QString title, icon, exec;
                QString id = parseDesktopFile(localPath, title, icon, exec);
                if (!title.isEmpty()) {
                    addApp(id, title, icon, exec, false);
                }
            } else if (fi.isExecutable() && !fi.isDir()) {
                QString title = fi.fileName();
                QString exec = QString("nohup \"%1\" >/dev/null 2>&1 &").arg(localPath);
                QString icon = "qrc:/icons/terminal/256.png";
                addApp(title.toLower(), title, icon, exec, false);
            } else if (fi.exists()) {
                QString title = fi.fileName();
                QString exec = QString("xdg-open \"%1\"").arg(localPath);
                QString icon = fi.isDir() ? "qrc:/icons/finder/256.png" : "qrc:/icons/launchpad/256.png";
                addApp(title.toLower(), title, icon, exec, false);
            }
        } else {
            addAppFromText(url.toString());
        }
    }
}

void DockManager::addAppFromText(const QString &text) {
    QString trimmed = text.trimmed();
    if (trimmed.startsWith("http://", Qt::CaseInsensitive) || trimmed.startsWith("https://", Qt::CaseInsensitive)) {
        QUrl url(trimmed);
        QString host = url.host();
        if (host.startsWith("www.")) host = host.mid(4);

        QString title = host.isEmpty() ? "Web Link" : host;
        if (!title.isEmpty()) {
            title[0] = title[0].toUpper();
        }

        QString id = QString("web_%1").arg(QDateTime::currentMSecsSinceEpoch());
        QString exec = QString("google-chrome --app=\"%1\" || xdg-open \"%1\"").arg(trimmed);
        QString icon = "qrc:/icons/safari/256.png";

        addApp(id, title, icon, exec, false);
    }
}

QString DockManager::getAppQuery(const QString &id) {
    if (id == "finder") return "dolphin|nautilus|nemo|thunar";
    if (id == "safari") return "chrome|firefox|brave|chromium|edge";
    if (id == "messages") return "whatsapp";
    if (id == "terminal") return "konsole|terminal|alacritty|kitty|wezterm|tilix|foot|xterm";
    if (id == "antigravity") return "antigravity";
    if (id == "system-preferences") return "systemsettings|control-center|settings";
    if (id == "calculator") return "kcalc|calculator|galculator";
    if (id == "music") return "spotify|elisa|rhythmbox|clementine|audacious";
    if (id == "mail") return "thunderbird|kmail|mail.google.com|gmail";
    if (id == "maps") return "google maps|maps.google.com|maps";
    if (id == "contacts") return "kaddressbook|gnome-contacts|contacts.google.com|contacts";
    if (id == "notes") return "kate|knotes|gedit|xed";
    if (id == "photos") return "gwenview|eog|shotwell|loupe|ristretto";
    if (id == "appstore") return "plasma-discover|discover|software";
    if (id == "tv") return "sonyliv|sony liv|vlc|mpv";
    if (id == "spectacle" || id == "org.kde.spectacle") return "spectacle";
    return id;
}

bool DockManager::executeKWinAction(const QString &scriptCode) {
    QTemporaryFile tempFile;
    if (!tempFile.open()) return false;
    tempFile.write(scriptCode.toUtf8());
    tempFile.flush();
    QString filePath = tempFile.fileName();

    QDBusInterface scriptingInterface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (!scriptingInterface.isValid()) {
        return false;
    }

    // Always unload any previous action script to guarantee zero accumulation
    scriptingInterface.call("unloadScript", "dock_action");

    QDBusReply<int> reply = scriptingInterface.call("loadScript", filePath, "dock_action");
    if (!reply.isValid() || reply.value() < 0) {
        return false;
    }

    int scriptId = reply.value();
    QString scriptPath = QString("/Scripting/Script%1").arg(scriptId);

    QDBusInterface scriptInterface("org.kde.KWin", scriptPath, "org.kde.kwin.Script", QDBusConnection::sessionBus());
    scriptInterface.call("run");

    // Immediately unload to ensure zero script leak
    scriptingInterface.call("unloadScript", "dock_action");
    return true;
}

bool DockManager::runKWinScript(const QString &scriptCode, QString *outResult) {
    Q_UNUSED(outResult);
    return executeKWinAction(scriptCode);
}

void DockManager::setupKWinWindowTracker() {
    QDBusInterface scriptingInterface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (!scriptingInterface.isValid()) {
        qWarning() << "[DockManager] KWin Scripting interface is not available on D-Bus";
        return;
    }

    // Always unload any existing tracker script first to prevent duplicates
    scriptingInterface.call("unloadScript", "macos_dock_tracker");

    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(configDir);
    QString scriptFilePath = configDir + "/macos_dock_tracker.js";

    qint64 dockPid = QCoreApplication::applicationPid();

    QString scriptCode = QString(R"(
        var dockPid = %1;
        var syncTimer = null;
        var retryTimer = null;

        function isIgnored(c) {
            if (!c) return true;
            if (!c.normalWindow && !c.dialog) return true;
            if (c.pid && c.pid === dockPid) return true;
            var rClass = (c.resourceClass || "").toLowerCase();
            var rName = (c.resourceName || "").toLowerCase();
            var dFile = (c.desktopFileName || "").toLowerCase();
            if (rName.indexOf("macos-dock") !== -1 || rClass.indexOf("macos dock") !== -1 || rClass.indexOf("macos-dock") !== -1) return true;
            if (dFile.indexOf("videobridge") !== -1 || rClass.indexOf("videobridge") !== -1 || rName.indexOf("videobridge") !== -1) return true;
            if (rClass === "plasmashell" || rClass === "krunner") return true;
            return false;
        }

        function sendUpdate() {
            try {
                var list = [];
                var clients = workspace.windowList();
                for (var i = 0; i < clients.length; i++) {
                    var c = clients[i];
                    try {
                        if (isIgnored(c)) continue;
                        list.push({
                            "id": c.internalId ? c.internalId.toString() : ("win_" + i),
                            "desktopFile": c.desktopFileName ? c.desktopFileName : "",
                            "resourceClass": c.resourceClass ? c.resourceClass : "",
                            "resourceName": c.resourceName ? c.resourceName : "",
                            "caption": c.caption ? c.caption : "",
                            "minimized": c.minimized ? true : false,
                            "active": (workspace.activeWindow === c),
                            "pid": c.pid ? c.pid : 0
                        });
                    } catch(innerErr) {}
                }
                callDBus("org.kde.MacOSDock", "/WindowTracker", "org.kde.MacOSDock", "updateWindows", JSON.stringify(list));
            } catch (err) {
                console.warn("[MacOSDock Tracker Error]: " + err);
            }
        }

        function hookWindow(c) {
            if (!c) return;
            try {
                c.minimizedChanged.connect(sendUpdate);
                c.captionChanged.connect(sendUpdate);
                c.activeChanged.connect(sendUpdate);
                if (c.desktopFileNameChanged) c.desktopFileNameChanged.connect(sendUpdate);
                if (c.windowClassChanged) c.windowClassChanged.connect(sendUpdate);
                if (c.readyForPaintingChanged) c.readyForPaintingChanged.connect(sendUpdate);
                if (c.closed) c.closed.connect(sendUpdate);
            } catch(e) {}
        }

        try {
            workspace.windowAdded.connect(function(c) {
                hookWindow(c);
                sendUpdate();
                if (retryTimer) retryTimer.start(250);
            });
            workspace.windowRemoved.connect(sendUpdate);

            if (workspace.windowActivated) {
                workspace.windowActivated.connect(sendUpdate);
            }

            // Hook all existing open windows
            var initialWindows = workspace.windowList();
            for (var j = 0; j < initialWindows.length; j++) {
                hookWindow(initialWindows[j]);
            }

            // Internal compositor timer to guarantee 100% synchronization
            syncTimer = new QTimer();
            syncTimer.interval = 1000;
            syncTimer.timeout.connect(sendUpdate);
            syncTimer.start();

            // Delayed retry timer for newly opened windows where Wayland sets props 150-250ms later
            retryTimer = new QTimer();
            retryTimer.interval = 250;
            retryTimer.singleShot = true;
            retryTimer.timeout.connect(sendUpdate);

            // Initial immediate broadcast
            sendUpdate();
        } catch(e) {
            console.warn("[MacOSDock Tracker Hook Error]: " + e);
            sendUpdate();
        }
    )").arg(dockPid);

    QFile file(scriptFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[DockManager] Could not write KWin script to" << scriptFilePath;
        return;
    }
    file.write(scriptCode.toUtf8());
    file.close();

    QDBusReply<int> reply = scriptingInterface.call("loadScript", scriptFilePath, "macos_dock_tracker");
    if (!reply.isValid() || reply.value() < 0) {
        qWarning() << "[DockManager] Failed to load macos_dock_tracker into KWin:" << reply.error().message();
        return;
    }

    m_kwinTrackerScriptId = reply.value();
    QString scriptPath = QString("/Scripting/Script%1").arg(m_kwinTrackerScriptId);
    QDBusInterface scriptInterface("org.kde.KWin", scriptPath, "org.kde.kwin.Script", QDBusConnection::sessionBus());
    scriptInterface.call("run");
    qDebug() << "[DockManager] Persistent KWin tracker loaded and running as Script" << m_kwinTrackerScriptId;
}

void DockManager::cleanupKWinWindowTracker() {
    QDBusInterface scriptingInterface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (scriptingInterface.isValid()) {
        scriptingInterface.call("unloadScript", "macos_dock_tracker");
    }
    m_kwinTrackerScriptId = -1;
}

void DockManager::checkTrackerHealth() {
    QDBusInterface scriptingInterface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (!scriptingInterface.isValid()) return;

    QDBusReply<bool> reply = scriptingInterface.call("isScriptLoaded", "macos_dock_tracker");
    if (!reply.isValid() || !reply.value()) {
        qDebug() << "[DockManager] Tracker script missing in KWin, reinstalling...";
        setupKWinWindowTracker();
    }
}

void DockManager::updateWindows(const QString &json) {
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isArray()) {
        matchWindowsToApps(doc.array());
    } else {
        qWarning() << "[DockManager] updateWindows received non-array JSON:" << json.left(100);
    }
}

void DockManager::matchWindowsToApps(const QJsonArray &windowList) {
    qDebug() << "[DockManager] matchWindowsToApps received" << windowList.size() << "windows";

    // Map of AppItem* -> QVariantList of window maps
    QMap<AppItem*, QVariantList> appWindows;
    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (item) {
            appWindows[item] = QVariantList();
        }
    }

    struct UnassignedWin {
        QString id;
        QString desktopFile;
        QString resourceClass;
        QString resourceName;
        QString caption;
        bool minimized;
        bool active;
    };

    QList<UnassignedWin> unassigned;

    for (const QJsonValue &val : windowList) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();

        QString winId = obj["id"].toString();
        QString desktopFile = obj["desktopFile"].toString().trimmed().toLower();
        QString resourceClass = obj["resourceClass"].toString().trimmed().toLower();
        QString resourceName = obj["resourceName"].toString().trimmed().toLower();
        QString caption = obj["caption"].toString().trimmed();
        bool minimized = obj["minimized"].toBool();
        bool active = obj["active"].toBool();
        qint64 winPid = obj["pid"].toVariant().toLongLong();
        qint64 myPid = QCoreApplication::applicationPid();

        // Strict filter for dock itself, xwaylandvideobridge, and background daemons
        // Do NOT filter on caption, as terminals or editors may have repository name in title!
        if (winPid > 0 && winPid == myPid) continue;
        if (resourceName.contains("macos-dock") || resourceClass.contains("macos-dock") || resourceClass == "macos dock") continue;
        if (desktopFile.contains("videobridge") || resourceClass.contains("videobridge") || resourceName.contains("videobridge")) continue;
        if (resourceClass == "plasmashell" || resourceClass == "krunner") continue;

        QVariantMap winMap;
        winMap["id"] = winId;
        winMap["title"] = caption.isEmpty() ? resourceClass : caption;
        winMap["minimized"] = minimized;
        winMap["active"] = active;

        bool matched = false;

        // Try to match against existing app items in dock
        for (QObject *appObj : m_apps) {
            auto *item = qobject_cast<AppItem*>(appObj);
            if (!item) continue;

            QString appId = item->id().toLower();
            QString appQuery = getAppQuery(appId);
            QStringList queries = appQuery.split("|");

            bool isMatch = false;

            // Special cases: Web apps running inside Chrome or browser
            if (caption.contains("whatsapp", Qt::CaseInsensitive)) {
                if (appId == "messages") {
                    isMatch = true;
                } else if (appId == "safari" || appId == "google-chrome") {
                    isMatch = false; // Don't let browser steal WhatsApp
                }
            } else if (caption.contains("gmail", Qt::CaseInsensitive) || caption.contains("mail.google.com", Qt::CaseInsensitive)) {
                if (appId == "mail") {
                    isMatch = true;
                } else if (appId == "safari" || appId == "google-chrome") {
                    isMatch = false; // Don't let browser steal Gmail
                }
            } else if (caption.contains("google maps", Qt::CaseInsensitive) || caption.contains("maps.google.com", Qt::CaseInsensitive)) {
                if (appId == "maps") {
                    isMatch = true;
                } else if (appId == "safari" || appId == "google-chrome") {
                    isMatch = false;
                }
            } else if (caption.contains("google contacts", Qt::CaseInsensitive) || caption.contains("contacts.google.com", Qt::CaseInsensitive)) {
                if (appId == "contacts") {
                    isMatch = true;
                } else if (appId == "safari" || appId == "google-chrome") {
                    isMatch = false;
                }
            } else if (caption.contains("sonyliv", Qt::CaseInsensitive) || caption.contains("sony liv", Qt::CaseInsensitive)) {
                if (appId == "tv") {
                    isMatch = true;
                } else if (appId == "safari" || appId == "google-chrome") {
                    isMatch = false; // Don't let browser steal Sony LIV
                }
            }

            if (!isMatch) {
                isMatch = (appId == desktopFile || appId == resourceClass || appId == resourceName);
            }

            if (!isMatch) {
                for (const QString &q : queries) {
                    if (!q.isEmpty()) {
                        if (desktopFile.contains(q) || resourceClass.contains(q) || resourceName.contains(q)) {
                            // Don't match Web apps to general browser
                            if (appId == "safari" || appId == "google-chrome") {
                                if (caption.contains("whatsapp", Qt::CaseInsensitive) ||
                                    caption.contains("gmail", Qt::CaseInsensitive) ||
                                    caption.contains("mail.google.com", Qt::CaseInsensitive) ||
                                    caption.contains("google maps", Qt::CaseInsensitive) ||
                                    caption.contains("maps.google.com", Qt::CaseInsensitive) ||
                                    caption.contains("contacts.google.com", Qt::CaseInsensitive) ||
                                    caption.contains("sonyliv", Qt::CaseInsensitive) ||
                                    caption.contains("sony liv", Qt::CaseInsensitive)) {
                                    continue;
                                }
                            }
                            isMatch = true;
                            break;
                        }
                    }
                }
            }

            if (isMatch) {
                appWindows[item].append(winMap);
                matched = true;
                break;
            }
        }

        if (!matched) {
            unassigned.append({winId, desktopFile, resourceClass, resourceName, caption, minimized, active});
        }
    }

    // Assign windows to matched apps
    for (auto it = appWindows.begin(); it != appWindows.end(); ++it) {
        it.key()->setWindows(it.value());
        if (it.key()->id() == "terminal" || it.key()->windowCount() > 0) {
            qDebug() << "[DockManager] App assigned:" << it.key()->id() << "windows:" << it.key()->windowCount();
        }
        if (it.key()->windowCount() > 0 && m_launchingAppIds.contains(it.key()->id())) {
            m_launchingAppIds.remove(it.key()->id());
            emit appLaunchFinished(it.key()->id());
            emit hasLaunchingAppChanged();
        }
    }

    for (const auto &uw : unassigned) {
        if (uw.desktopFile.contains("konsole") || uw.resourceClass.contains("konsole") || uw.resourceName.contains("konsole")) {
            qDebug() << "[DockManager] UNASSIGNED KONSOLE WINDOW:" << uw.id << uw.desktopFile << uw.resourceClass << uw.caption;
        }
    }

    // Process unassigned windows (Dynamic Unpinned Running Apps)
    bool structureChanged = false;
    QMap<QString, QList<UnassignedWin>> grouped;
    for (const auto &uw : unassigned) {
        QString key = !uw.desktopFile.isEmpty() ? uw.desktopFile : (!uw.resourceClass.isEmpty() ? uw.resourceClass : uw.resourceName);
        QString keyLower = key.toLower();

        if (keyLower.contains("macos-dock") || keyLower == "macos dock" ||
            keyLower.contains("videobridge") ||
            keyLower == "plasmashell" || keyLower == "krunner") {
            continue;
        }

        if (!key.isEmpty()) {
            grouped[key].append(uw);
        }
    }

    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        QString key = it.key();
        QString keyLower = key.toLower();
        if (keyLower.contains("macos-dock") || keyLower == "macos dock" ||
            keyLower.contains("videobridge") ||
            keyLower == "plasmashell" || keyLower == "krunner") {
            continue;
        }
        const auto &wins = it.value();

        // Check if an unpinned item already exists for this key
        AppItem *targetItem = nullptr;
        for (QObject *obj : m_apps) {
            auto *item = qobject_cast<AppItem*>(obj);
            if (item && item->id() == key) {
                targetItem = item;
                break;
            }
        }

        if (!targetItem) {
            // Create dynamic unpinned AppItem
            QString title = wins.first().caption;
            QString iconName = key;
            QString exec = key;

            QString cleanKey = key;
            if (cleanKey.endsWith(".desktop")) {
                cleanKey.chop(8);
            }

            // Search desktop files for real name & icon
            QString desktopPath = "/usr/share/applications/" + cleanKey + ".desktop";
            if (!QFile::exists(desktopPath)) {
                desktopPath = "/usr/share/applications/org.kde." + cleanKey + ".desktop";
            }
            if (!QFile::exists(desktopPath)) {
                desktopPath = QDir::homePath() + "/.local/share/applications/" + cleanKey + ".desktop";
            }
            if (!QFile::exists(desktopPath)) {
                desktopPath = "/var/lib/flatpak/exports/share/applications/" + cleanKey + ".desktop";
            }
            if (!QFile::exists(desktopPath)) {
                desktopPath = QDir::homePath() + "/.local/share/flatpak/exports/share/applications/" + cleanKey + ".desktop";
            }

            if (QFile::exists(desktopPath)) {
                QString dTitle, dIcon, dExec;
                parseDesktopFile(desktopPath, dTitle, dIcon, dExec);
                if (!dTitle.isEmpty()) title = dTitle;
                if (!dIcon.isEmpty()) iconName = dIcon;
                if (!dExec.isEmpty()) exec = dExec;
            } else {
                iconName = resolveSystemIcon(key);
                if (title.isEmpty()) title = key;
                if (!title.isEmpty()) title[0] = title[0].toUpper();
            }

            if (cleanKey.contains("spectacle", Qt::CaseInsensitive)) {
                iconName = "qrc:/icons/spectacle/256.png";
                title = "Spectacle";
            }

            targetItem = new AppItem(key, title, iconName, exec, false, false, false, this);

            // In macOS, running unpinned apps appear on the left of the divider (before files/trash)
            int insertIndex = -1;
            for (int i = 0; i < m_apps.size(); ++i) {
                auto *item = qobject_cast<AppItem*>(m_apps.at(i));
                if (item && item->dockBreaksBefore()) {
                    insertIndex = i;
                    break;
                }
            }
            if (insertIndex >= 0) {
                m_apps.insert(insertIndex, targetItem);
            } else {
                m_apps.append(targetItem);
            }
            structureChanged = true;
        }

        QVariantList wList;
        for (const auto &winsItem : wins) {
            QVariantMap wMap;
            wMap["id"] = winsItem.id;
            wMap["title"] = winsItem.caption.isEmpty() ? targetItem->title() : winsItem.caption;
            wMap["minimized"] = winsItem.minimized;
            wMap["active"] = winsItem.active;
            wList.append(wMap);
        }
        targetItem->setWindows(wList);
        if (targetItem->windowCount() > 0 && m_launchingAppIds.contains(targetItem->id())) {
            m_launchingAppIds.remove(targetItem->id());
            emit appLaunchFinished(targetItem->id());
            emit hasLaunchingAppChanged();
        }
    }

    // Clean up unpinned apps with 0 windows or blacklisted apps
    for (int i = m_apps.size() - 1; i >= 0; --i) {
        auto *item = qobject_cast<AppItem*>(m_apps.at(i));
        if (item && !item->isPinned()) {
            QString idLower = item->id().toLower();
            bool isBlacklisted = idLower.contains("macos-dock") || idLower == "macos dock" ||
                                 idLower.contains("videobridge") ||
                                 idLower == "plasmashell" || idLower == "krunner";
            if (item->windowCount() == 0 || isBlacklisted) {
                m_apps.removeAt(i);
                item->deleteLater();
                structureChanged = true;
            }
        }
    }

    if (structureChanged) {
        emit appsChanged();
    }
}

void DockManager::activateWindow(const QString &windowId) {
    // Immediately update local active state across apps for instant UI response and multi-window cycling
    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (!item) continue;
        QVariantList wList = item->windows();
        bool changed = false;
        for (int i = 0; i < wList.size(); ++i) {
            QVariantMap map = wList[i].toMap();
            bool shouldBeActive = (map.value("id").toString() == windowId);
            if (map.value("active").toBool() != shouldBeActive) {
                map["active"] = shouldBeActive;
                if (shouldBeActive) map["minimized"] = false;
                wList[i] = map;
                changed = true;
            }
        }
        if (changed) {
            item->setWindows(wList);
        }
    }

    QString script = QString(R"(
        var clients = workspace.windowList();
        for (var i = 0; i < clients.length; i++) {
            var c = clients[i];
            var id = c.internalId ? c.internalId.toString() : ("win_" + i);
            if (id === "%1") {
                c.minimized = false;
                workspace.activeWindow = c;
                break;
            }
        }
    )").arg(windowId);
    runKWinScript(script);
}

void DockManager::closeWindowById(const QString &windowId) {
    QString script = QString(R"(
        var clients = workspace.windowList();
        for (var i = 0; i < clients.length; i++) {
            var c = clients[i];
            var id = c.internalId ? c.internalId.toString() : ("win_" + i);
            if (id === "%1") {
                c.closeWindow();
                break;
            }
        }
    )").arg(windowId);
    runKWinScript(script);
}

void DockManager::launchOrToggleApp(const QString &id) {
    // Find the app in m_apps
    AppItem *targetApp = nullptr;
    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (item && item->id() == id) {
            targetApp = item;
            break;
        }
    }

    qDebug() << "[DockManager] launchOrToggleApp:" << id
             << "targetApp found:" << (targetApp != nullptr)
             << "windowCount:" << (targetApp ? targetApp->windowCount() : -1);

    if (targetApp && targetApp->windowCount() > 0) {
        // App is already running -> Switch / toggle window! Jumps ONCE!
        emit appSwitched(id);

        if (targetApp->windowCount() == 1) {
            // Single window toggle
            QVariantMap win = targetApp->windows().first().toMap();
            QString winId = win.value("id").toString();
            bool isActive = win.value("active").toBool();
            bool isMinimized = win.value("minimized").toBool();

            if (isActive && !isMinimized) {
                // Minimize active window
                minimizeApp(id);
            } else {
                // Restore / focus window
                activateWindow(winId);
            }
        } else {
            // Multiple windows: cycle through all windows in round-robin order!
            const auto &windows = targetApp->windows();
            int activeIndex = -1;
            for (int i = 0; i < windows.size(); ++i) {
                if (windows[i].toMap().value("active").toBool()) {
                    activeIndex = i;
                    break;
                }
            }

            int nextIndex = 0;
            if (activeIndex >= 0) {
                // If currently focused on a window of this app, switch to the NEXT window!
                nextIndex = (activeIndex + 1) % windows.size();
            } else {
                // None currently active -> find first non-minimized or first window
                nextIndex = 0;
                for (int i = 0; i < windows.size(); ++i) {
                    if (!windows[i].toMap().value("minimized").toBool()) {
                        nextIndex = i;
                        break;
                    }
                }
            }

            QString nextWinId = windows[nextIndex].toMap().value("id").toString();
            activateWindow(nextWinId);
        }
    } else {
        // App is not running -> Launch new instance! Jumps until window opens!
        launchNewInstance(id);
    }
}

void DockManager::launchNewInstance(const QString &id) {
    if (m_launchingAppIds.contains(id)) {
        return;
    }

    m_launchingAppIds.insert(id);
    emit hasLaunchingAppChanged();
    emit appLaunchStarted(id);
    emit appLaunched(id);

    // Timeout safety fallback if process never creates a window
    QTimer::singleShot(15000, this, [this, id]() {
        if (m_launchingAppIds.remove(id)) {
            emit appLaunchFinished(id);
            emit hasLaunchingAppChanged();
        }
    });

    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (item && item->id() == id) {
            launchCommand(item->execCommand());
            return;
        }
    }
}

void DockManager::minimizeApp(const QString &id) {
    AppItem *targetApp = nullptr;
    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (item && item->id() == id) {
            targetApp = item;
            break;
        }
    }

    if (targetApp && targetApp->windowCount() > 0) {
        QVariantList wList = targetApp->windows();
        for (int i = 0; i < wList.size(); ++i) {
            QVariantMap map = wList[i].toMap();
            map["active"] = false;
            map["minimized"] = true;
            wList[i] = map;
        }
        targetApp->setWindows(wList);

        QStringList winIds;
        for (const auto &w : targetApp->windows()) {
            winIds.append(w.toMap().value("id").toString());
        }
        QString idsArray = QString("[\"%1\"]").arg(winIds.join("\",\""));
        QString script = QString(R"(
            var ids = %1;
            var clients = workspace.windowList();
            for (var i = 0; i < clients.length; i++) {
                var c = clients[i];
                var id = c.internalId ? c.internalId.toString() : ("win_" + i);
                if (ids.indexOf(id) !== -1) {
                    c.minimized = true;
                }
            }
        )").arg(idsArray);
        runKWinScript(script);
        return;
    }

    QString query = getAppQuery(id);
    QString script = QString(R"(
        var clients = workspace.windowList();
        var queries = "%1".split("|");
        for (var i = 0; i < clients.length; i++) {
            var c = clients[i];
            if (!c.normalWindow) continue;
            var name = (c.desktopFileName + " " + c.resourceClass + " " + c.resourceName).toLowerCase();
            for (var q = 0; q < queries.length; q++) {
                if (name.indexOf(queries[q]) !== -1) {
                    c.minimized = true;
                    break;
                }
            }
        }
    )").arg(query);
    runKWinScript(script);
}

void DockManager::closeApp(const QString &id) {
    AppItem *targetApp = nullptr;
    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (item && item->id() == id) {
            targetApp = item;
            break;
        }
    }

    if (targetApp && targetApp->windowCount() > 0) {
        QStringList winIds;
        for (const auto &w : targetApp->windows()) {
            winIds.append(w.toMap().value("id").toString());
        }
        QString idsArray = QString("[\"%1\"]").arg(winIds.join("\",\""));
        QString script = QString(R"(
            var ids = %1;
            var clients = workspace.windowList();
            for (var i = 0; i < clients.length; i++) {
                var c = clients[i];
                var id = c.internalId ? c.internalId.toString() : ("win_" + i);
                if (ids.indexOf(id) !== -1) {
                    c.closeWindow();
                }
            }
        )").arg(idsArray);
        runKWinScript(script);
        return;
    }

    QString query = getAppQuery(id);
    QString script = QString(R"(
        var clients = workspace.windowList();
        var queries = "%1".split("|");
        for (var i = 0; i < clients.length; i++) {
            var c = clients[i];
            if (!c.normalWindow) continue;
            var name = (c.desktopFileName + " " + c.resourceClass + " " + c.resourceName).toLowerCase();
            for (var q = 0; q < queries.length; q++) {
                if (name.indexOf(queries[q]) !== -1) {
                    c.closeWindow();
                    break;
                }
            }
        }
    )").arg(query);
    runKWinScript(script);
}

void DockManager::initDefaultApps() {
    qDeleteAll(m_apps);
    m_apps.clear();

    struct AppDef {
        QString id;
        QString title;
        QString icon;
        QString execCommand;
        bool dockBreaksBefore;
    };

    QList<AppDef> defaultList = {
        {"finder", "Finder", "qrc:/icons/finder/256.png", "dolphin ~ || nautilus ~ || xdg-open ~", false},
        {"safari", "Safari", "qrc:/icons/safari/256.png", "google-chrome || firefox || xdg-open https://google.com", false},
        {"messages", "WhatsApp", "qrc:/icons/messages/256.png", "google-chrome --app=https://web.whatsapp.com || firefox --new-window https://web.whatsapp.com || xdg-open https://web.whatsapp.com", false},
        {"mail", "Mail", "qrc:/icons/mail/256.png", "google-chrome --app=https://mail.google.com || firefox --new-window https://mail.google.com || thunderbird || xdg-open https://mail.google.com", false},
        {"maps", "Maps", "qrc:/icons/maps/256.png", "google-chrome --app=https://maps.google.com || xdg-open https://maps.google.com", false},
        {"photos", "Photos", "qrc:/icons/photos/256.png", "gwenview || eog || shotwell || xdg-open ~/Pictures", false},
        {"contacts", "Contacts", "qrc:/icons/contacts/256.png", "kaddressbook || gnome-contacts || google-chrome --app=https://contacts.google.com", false},
        {"notes", "Notes", "qrc:/icons/notes/256.png", "kate || knotes || gedit", false},
        {"music", "Music", "qrc:/icons/music/256.png", "elisa || spotify || rhythmbox || google-chrome --app=https://music.youtube.com || xdg-open https://music.youtube.com", false},
        {"tv", "TV", "qrc:/icons/tv/256.png", "google-chrome --app=https://www.sonyliv.com || firefox --new-window https://www.sonyliv.com || xdg-open https://www.sonyliv.com", false},
        {"appstore", "App Store", "qrc:/icons/appstore/256.png", "plasma-discover || discover || gnome-software", false},
        {"system-preferences", "System Settings", "qrc:/icons/system-preferences/256.png", "systemsettings || gnome-control-center", false},
        {"antigravity", "Antigravity", "qrc:/icons/antigravity/256.png", "/home/gaurav/.local/bin/antigravity-ide || /opt/antigravity/antigravity --no-sandbox || antigravity", false},
        {"terminal", "Terminal", "qrc:/icons/terminal/256.png", "konsole || gnome-terminal || alacritty || x-terminal-emulator || kitty", false},
        {"calculator", "Calculator", "qrc:/icons/calculator/256.png", "kcalc || gnome-calculator", false},
        {"wallpapers", "Wallpapers", "qrc:/icons/wallpapers/256.png", "systemsettings kcm_desktoptheme || xdg-open /usr/share/wallpapers", true},
        {"view-source", "GitHub", "qrc:/icons/view-source/256.png", "xdg-open https://github.com/GauravGadhari", false}
    };

    for (const auto &item : defaultList) {
        auto *app = new AppItem(item.id, item.title, item.icon, item.execCommand, item.dockBreaksBefore, false, true, this);
        m_apps.append(app);
    }

    emit appsChanged();
}

void DockManager::setIsDarkTheme(bool isDark) {
    if (m_isDarkTheme != isDark) {
        m_isDarkTheme = isDark;
        emit isDarkThemeChanged();
    }
}

bool DockManager::isAutostartEnabled() const {
    QString autostartFile = QDir::homePath() + "/.config/autostart/macos-dock.desktop";
    return QFile::exists(autostartFile);
}

void DockManager::setAutostartEnabled(bool enabled) {
    QString autostartDir = QDir::homePath() + "/.config/autostart";
    QString autostartFile = autostartDir + "/macos-dock.desktop";

    if (enabled) {
        QDir().mkpath(autostartDir);
        QFile file(autostartFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QString content = 
                "[Desktop Entry]\n"
                "Type=Application\n"
                "Name=macOS Dock\n"
                "Comment=Light macOS Dock for Linux (KDE Plasma / Wayland / X11)\n"
                "Exec=/mnt/code/_Antigravity/General/macos-dock-qt6/run_dock.sh\n"
                "Icon=/mnt/code/_Antigravity/General/macos-dock-qt6/assets/icons/launchpad/256.png\n"
                "Terminal=false\n"
                "StartupNotify=false\n"
                "X-KDE-autostart-phase=2\n"
                "X-GNOME-Autostart-enabled=true\n"
                "Categories=Utility;\n";
            file.write(content.toUtf8());
            file.close();
            file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        }
    } else {
        QFile::remove(autostartFile);
    }
    emit autostartEnabledChanged();
}

void DockManager::toggleAutostart() {
    setAutostartEnabled(!isAutostartEnabled());
}

void DockManager::setBaseIconWidth(double width) {
    if (m_baseIconWidth != width) {
        m_baseIconWidth = width;
        emit baseIconWidthChanged();
    }
}

void DockManager::setMaxMagnification(double mag) {
    if (m_maxMagnification != mag) {
        m_maxMagnification = mag;
        emit maxMagnificationChanged();
    }
}

void DockManager::launchCommand(const QString &command) {
    if (command.isEmpty()) return;
    qDebug() << "Launching command detached:" << command;
    QProcess::startDetached("sh", QStringList() << "-c" << QString("( %1 ) >/dev/null 2>&1 &").arg(command));
}

void DockManager::quitDock() {
    QCoreApplication::quit();
}

void DockManager::refreshRunningStatus() {
    qint64 dockPid = QCoreApplication::applicationPid();
    QString script = QString(R"(
        var dockPid = %1;

        function isIgnored(c) {
            if (!c || !c.normalWindow) return true;
            if (c.pid && c.pid === dockPid) return true;
            var rClass = (c.resourceClass || "").toLowerCase();
            var rName = (c.resourceName || "").toLowerCase();
            var dFile = (c.desktopFileName || "").toLowerCase();
            if (rName.indexOf("macos-dock") !== -1 || rClass.indexOf("macos dock") !== -1 || rClass.indexOf("macos-dock") !== -1) return true;
            if (dFile.indexOf("videobridge") !== -1 || rClass.indexOf("videobridge") !== -1 || rName.indexOf("videobridge") !== -1) return true;
            if (rClass === "plasmashell" || rClass === "krunner") return true;
            return false;
        }

        var list = [];
        var clients = workspace.windowList();
        for (var i = 0; i < clients.length; i++) {
            var c = clients[i];
            if (isIgnored(c)) continue;
            list.push({
                "id": c.internalId ? c.internalId.toString() : ("win_" + i),
                "desktopFile": c.desktopFileName ? c.desktopFileName : "",
                "resourceClass": c.resourceClass ? c.resourceClass : "",
                "resourceName": c.resourceName ? c.resourceName : "",
                "caption": c.caption ? c.caption : "",
                "minimized": c.minimized ? true : false,
                "active": (workspace.activeWindow === c),
                "pid": c.pid ? c.pid : 0
            });
        }
        callDBus("org.kde.MacOSDock", "/WindowTracker", "org.kde.MacOSDock", "updateWindows", JSON.stringify(list));
    )").arg(dockPid);
    runKWinScript(script);
}
