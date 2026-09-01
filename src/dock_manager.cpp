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

DockManager::DockManager(QObject *parent) : QObject(parent) {
    if (!loadApps()) {
        initDefaultApps();
        saveApps();
    }

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() {
        refreshRunningStatus();
        checkDesktopState();
    });
    m_pollTimer->start(1000);

    // Initial check
    QTimer::singleShot(200, this, [this]() {
        refreshRunningStatus();
        checkDesktopState();
    });
}

DockManager::~DockManager() {
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

void DockManager::checkDesktopState() {
    // Check if there are active windows or if user is on desktop
    // We query KWin via D-Bus to see if the active window is a normal app or empty desktop
    QDBusInterface kwinIface("org.kde.KWin", "/KWin", "org.kde.KWin", QDBusConnection::sessionBus());
    if (kwinIface.isValid()) {
        QDBusReply<QString> reply = kwinIface.call("activeWindow");
        if (reply.isValid()) {
            QString activeWin = reply.value();
            bool onDesk = activeWin.isEmpty() || activeWin.contains("plasma", Qt::CaseInsensitive) || activeWin.contains("desktop", Qt::CaseInsensitive);
            if (m_isOnDesktop != onDesk) {
                m_isOnDesktop = onDesk;
                emit isOnDesktopChanged();
            }
        }
    }
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

    // Generous horizontal margin (100px on left and right) so magnified edge icons
    // (Finder on left, GitHub on right) and their shadows are NEVER clipped.
    int maskX = qMax(0, x - 100);
    int maskW = qMin(winW - maskX, width + 200);

    // Full vertical window headroom (from y=0 to y=winH) so bouncing icons,
    // 2x magnified icons, and tooltips are NEVER clipped by the X11 shape mask.
    int maskY = 0;
    int maskH = winH;

    // Full dock active region + bottom 6px trigger strip across the whole screen width
    QRegion dockRegion(maskX, maskY, maskW, maskH);
    QRegion triggerStrip(0, winH - 6, winW, 6);

    m_window->setMask(dockRegion.united(triggerStrip));
}

void DockManager::resetMask() {
    if (!m_window) return;

    int winW = m_window->width();
    // When the window is shifted down to (realScreenHeight - 6),
    // the top 6px of the window (y=0 to y=6) is the part on-screen.
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
        if (item) {
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
    // Avoid duplicate IDs
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

    auto *item = new AppItem(uniqueId, title, icon, execCommand, dockBreaksBefore, false, this);
    m_apps.append(item);
    emit appsChanged();
    saveApps();
}

QString DockManager::resolveSystemIcon(const QString &iconName) {
    if (iconName.isEmpty()) return "qrc:/icons/launchpad/256.png";
    if (iconName.startsWith("/") || iconName.startsWith("file://") || iconName.startsWith("qrc:/")) {
        return iconName.startsWith("/") ? ("file://" + iconName) : iconName;
    }

    // Search common icon locations
    QStringList searchPaths = {
        "/usr/share/icons/hicolor/256x256/apps/",
        "/usr/share/icons/hicolor/128x128/apps/",
        "/usr/share/icons/hicolor/64x64/apps/",
        "/usr/share/icons/hicolor/48x48/apps/",
        "/usr/share/icons/hicolor/scalable/apps/",
        "/usr/share/pixmaps/",
        QDir::homePath() + "/.local/share/icons/hicolor/256x256/apps/",
        QDir::homePath() + "/.local/share/icons/hicolor/scalable/apps/"
    };

    QStringList extensions = {".png", ".svg", ".xpm"};

    for (const QString &dir : searchPaths) {
        for (const QString &ext : extensions) {
            QString fullPath = dir + iconName + ext;
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
                // Strip field codes (%f, %F, %u, %U, %i, %c, %k, %v, %m)
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
        // Capitalize title
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
    if (id == "finder") return "dolphin";
    if (id == "safari") return "chrome|firefox|brave|chromium";
    if (id == "messages") return "whatsapp";
    if (id == "terminal") return "konsole|terminal|alacritty|kitty";
    if (id == "vscode") return "code";
    if (id == "antigravity") return "antigravity";
    if (id == "system-preferences") return "systemsettings|control-center";
    if (id == "calculator") return "kcalc|calculator";
    if (id == "music") return "spotify|elisa|rhythmbox";
    if (id == "mail") return "thunderbird|kmail";
    if (id == "notes") return "kate|knotes|gedit";
    if (id == "photos") return "gwenview|eog|shotwell";
    if (id == "appstore") return "plasma-discover|discover";
    if (id == "tv") return "vlc";
    return id;
}

bool DockManager::runKWinScript(const QString &scriptCode, QString *outResult) {
    QTemporaryFile tempFile;
    if (!tempFile.open()) return false;
    tempFile.write(scriptCode.toUtf8());
    tempFile.flush();
    QString filePath = tempFile.fileName();

    QDBusInterface scriptingInterface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (!scriptingInterface.isValid()) {
        return false;
    }

    QDBusReply<int> reply = scriptingInterface.call("loadScript", filePath, QString("dock_%1").arg(QDateTime::currentMSecsSinceEpoch()));
    if (!reply.isValid()) {
        return false;
    }

    int scriptId = reply.value();
    QString scriptPath = QString("/Scripting/Script%1").arg(scriptId);

    QDBusInterface scriptInterface("org.kde.KWin", scriptPath, "org.kde.kwin.Script", QDBusConnection::sessionBus());
    scriptInterface.call("run");
    scriptInterface.call("stop");

    return true;
}

void DockManager::launchOrToggleApp(const QString &id) {
    emit appLaunched(id);

    QString query = getAppQuery(id);
    
    QString script = QString(R"(
        var clients = workspace.windowList();
        var queries = "%1".split("|");
        var matched = null;
        for (var i = 0; i < clients.length; i++) {
            var c = clients[i];
            if (!c.normalWindow) continue;
            var name = (c.desktopFileName + " " + c.resourceClass + " " + c.resourceName + " " + c.caption).toLowerCase();
            for (var q = 0; q < queries.length; q++) {
                if (name.indexOf(queries[q]) !== -1) {
                    matched = c;
                    break;
                }
            }
            if (matched) break;
        }
        if (matched) {
            if (workspace.activeWindow === matched && !matched.minimized) {
                matched.minimized = true;
            } else {
                matched.minimized = false;
                workspace.activeWindow = matched;
            }
        }
    )").arg(query);

    bool isRunning = false;
    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (item && item->id() == id && item->isRunning()) {
            isRunning = true;
            break;
        }
    }

    if (isRunning) {
        runKWinScript(script);
    } else {
        launchNewInstance(id);
    }
}

void DockManager::launchNewInstance(const QString &id) {
    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (item && item->id() == id) {
            launchCommand(item->execCommand());
            item->setIsRunning(true);
            return;
        }
    }
}

void DockManager::minimizeApp(const QString &id) {
    QString query = getAppQuery(id);
    QString script = QString(R"(
        var clients = workspace.windowList();
        var queries = "%1".split("|");
        for (var i = 0; i < clients.length; i++) {
            var c = clients[i];
            if (!c.normalWindow) continue;
            var name = (c.desktopFileName + " " + c.resourceClass + " " + c.resourceName + " " + c.caption).toLowerCase();
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
    QString query = getAppQuery(id);
    QString script = QString(R"(
        var clients = workspace.windowList();
        var queries = "%1".split("|");
        for (var i = 0; i < clients.length; i++) {
            var c = clients[i];
            if (!c.normalWindow) continue;
            var name = (c.desktopFileName + " " + c.resourceClass + " " + c.resourceName + " " + c.caption).toLowerCase();
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
        {"launchpad", "Launchpad", "qrc:/icons/launchpad/256.png", "krunner || rofi -show drun || wofi --show drun", false},
        {"safari", "Safari", "qrc:/icons/safari/256.png", "google-chrome || firefox || xdg-open https://google.com", false},
        {"messages", "WhatsApp", "qrc:/icons/messages/256.png", "google-chrome --app=https://web.whatsapp.com || firefox --new-window https://web.whatsapp.com || xdg-open https://web.whatsapp.com", false},
        {"mail", "Mail", "qrc:/icons/mail/256.png", "thunderbird || kmail || xdg-open mailto:", false},
        {"maps", "Maps", "qrc:/icons/maps/256.png", "google-chrome --app=https://maps.google.com || xdg-open https://maps.google.com", false},
        {"photos", "Photos", "qrc:/icons/photos/256.png", "gwenview || eog || shotwell || xdg-open ~/Pictures", false},
        {"facetime", "FaceTime", "qrc:/icons/facetime/256.png", "google-chrome --app=https://meet.google.com || xdg-open https://meet.google.com", false},
        {"calendar", "Calendar", "qrc:/icons/calendar/256.png", "korganizer || gnome-calendar || google-chrome --app=https://calendar.google.com || xdg-open https://calendar.google.com", false},
        {"contacts", "Contacts", "qrc:/icons/contacts/256.png", "kaddressbook || gnome-contacts || google-chrome --app=https://contacts.google.com", false},
        {"reminders", "Reminders", "qrc:/icons/reminders/256.png", "google-chrome --app=https://tasks.google.com || xdg-open https://tasks.google.com", false},
        {"notes", "Notes", "qrc:/icons/notes/256.png", "kate || knotes || gedit", false},
        {"music", "Music", "qrc:/icons/music/256.png", "elisa || spotify || rhythmbox || google-chrome --app=https://music.youtube.com || xdg-open https://music.youtube.com", false},
        {"podcasts", "Podcasts", "qrc:/icons/podcasts/256.png", "google-chrome --app=https://podcasts.google.com", false},
        {"tv", "TV", "qrc:/icons/tv/256.png", "vlc || xdg-open https://tv.apple.com", false},
        {"appstore", "App Store", "qrc:/icons/appstore/256.png", "plasma-discover || discover || gnome-software", false},
        {"system-preferences", "System Settings", "qrc:/icons/system-preferences/256.png", "systemsettings || gnome-control-center", false},
        {"vscode", "VS Code", "qrc:/icons/vscode/256.png", "code", false},
        {"antigravity", "Antigravity", "qrc:/icons/antigravity/256.png", "/opt/antigravity/antigravity --no-sandbox || /usr/local/bin/antigravity", false},
        {"terminal", "Terminal", "qrc:/icons/terminal/256.png", "konsole || gnome-terminal || alacritty || x-terminal-emulator || kitty", false},
        {"calculator", "Calculator", "qrc:/icons/calculator/256.png", "kcalc || gnome-calculator", false},
        {"wallpapers", "Wallpapers", "qrc:/icons/wallpapers/256.png", "systemsettings kcm_desktoptheme || xdg-open /usr/share/wallpapers", true},
        {"view-source", "GitHub", "qrc:/icons/view-source/256.png", "xdg-open https://github.com/GauravGadhari", false}
    };

    for (const auto &item : defaultList) {
        auto *app = new AppItem(item.id, item.title, item.icon, item.execCommand, item.dockBreaksBefore, false, this);
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
    QProcess::startDetached("sh", QStringList() << "-c" << QString("nohup %1 >/dev/null 2>&1 &").arg(command));
}

void DockManager::quitDock() {
    QCoreApplication::quit();
}

void DockManager::refreshRunningStatus() {
    QProcess process;
    process.start("pgrep", QStringList() << "-a" << ".");
    if (!process.waitForFinished(800)) return;

    QString output = QString::fromUtf8(process.readAllStandardOutput()).toLower();

    for (QObject *obj : m_apps) {
        auto *item = qobject_cast<AppItem*>(obj);
        if (!item) continue;

        QString id = item->id();
        bool running = false;

        if (id == "finder") {
            running = output.contains("dolphin") || output.contains("nautilus") || output.contains("nemo");
        } else if (id == "safari") {
            running = output.contains("chrome") || output.contains("firefox") || output.contains("brave") || output.contains("chromium");
        } else if (id == "terminal") {
            running = output.contains("konsole") || output.contains("gnome-terminal") || output.contains("alacritty") || output.contains("kitty");
        } else if (id == "vscode") {
            running = output.contains("code");
        } else if (id == "system-preferences") {
            running = output.contains("systemsettings") || output.contains("gnome-control-center");
        } else if (id == "calculator") {
            running = output.contains("kcalc") || output.contains("gnome-calculator");
        } else if (id == "music") {
            running = output.contains("spotify") || output.contains("elisa") || output.contains("rhythmbox");
        } else if (id == "mail") {
            running = output.contains("thunderbird") || output.contains("kmail");
        } else if (id == "notes") {
            running = output.contains("kate") || output.contains("knotes") || output.contains("gedit");
        } else if (id == "photos") {
            running = output.contains("gwenview") || output.contains("eog") || output.contains("shotwell");
        } else if (id == "appstore") {
            running = output.contains("plasma-discover") || output.contains("discover") || output.contains("gnome-software");
        }

        item->setIsRunning(running);
    }
}
