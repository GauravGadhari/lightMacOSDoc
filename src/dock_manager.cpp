#include "dock_manager.h"
#include "window_preview_provider.h"
#include <QQmlApplicationEngine>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
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
#include <QThread>
#include <QRegularExpression>
#include <QSet>
#include <QMimeDatabase>
#include <QMimeType>
#include <QCryptographicHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFont>

DockManager::DockManager(QObject *parent) : QObject(parent) {
    m_appModel = new AppListModel(this);

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
    m_lastWindowUpdateTime = QDateTime::currentDateTime();
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &DockManager::checkTrackerHealth);
    m_pollTimer->start(2000);

    // Periodic memory trimming (every 10 minutes) to keep idle RAM bounded
    auto *idleTrimTimer = new QTimer(this);
    idleTrimTimer->setInterval(10 * 60 * 1000);
    connect(idleTrimTimer, &QTimer::timeout, this, [this]() {
        if (m_isAutoHidden || !m_isMenuOpen) {
            trimMemory();
        }
    });
    idleTrimTimer->start();
}

DockManager::~DockManager() {
    cleanupKWinWindowTracker();
    if (m_appModel) {
        m_appModel->clear();
    }
}

QList<QObject*> DockManager::apps() const {
    QList<QObject*> list;
    if (m_appModel) {
        for (AppItem *item : m_appModel->items()) {
            list.append(item);
        }
    }
    return list;
}

int DockManager::appCount() const {
    return m_appModel ? m_appModel->size() : 0;
}

void DockManager::setWindow(QQuickWindow *win) {
    if (m_window) {
        m_window->removeEventFilter(this);
    }
    m_window = win;
    if (m_window) {
        m_window->installEventFilter(this);
    }
}

bool DockManager::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_window) {
        if (event->type() == QEvent::Leave) {
            emit mouseLeftWindow();
        }
    }
    return QObject::eventFilter(watched, event);
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
    if (hidden) {
        QTimer::singleShot(800, this, &DockManager::trimMemory);
    }
}

void DockManager::trimMemory() {
    if (m_qmlEngine) {
        m_qmlEngine->collectGarbage();
        m_qmlEngine->trimComponentCache();
    }
    if (m_previewProvider) {
        QSet<QString> activeWinIds;
        for (AppItem *item : m_appModel->items()) {
            if (item) {
                for (const QVariant &v : item->windows()) {
                    QString id = v.toMap().value("id").toString();
                    if (!id.isEmpty()) activeWinIds.insert(id);
                }
            }
        }
        m_previewProvider->pruneTo(activeWinIds);
    }
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
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
    for (AppItem *item : m_appModel->items()) {
        // ONLY persist pinned apps that are not being removed
        if (item && item->isPinned() && !item->isRemoving()) {
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

    m_appModel->clear();

    for (const QJsonValue &val : array) {
        if (val.isObject()) {
            AppItem *item = AppItem::fromJson(val.toObject(), this);
            if (item) {
                m_appModel->append(item);
            }
        }
    }

    emit appsChanged();
    return !m_appModel->isEmpty();
}

void DockManager::resetToDefaultApps() {
    m_appModel->clear();
    initDefaultApps();
    saveApps();
    emit appsChanged();
}

void DockManager::moveApp(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= m_appModel->size() || toIndex < 0 || toIndex >= m_appModel->size() || fromIndex == toIndex) {
        return;
    }

    if (qAbs(fromIndex - toIndex) == 1) {
        AppItem *fromItem = m_appModel->at(fromIndex);
        AppItem *toItem = m_appModel->at(toIndex);
        if (fromItem && toItem && fromItem->dockBreaksBefore() != toItem->dockBreaksBefore()) {
            bool fromBreaks = fromItem->dockBreaksBefore();
            fromItem->setDockBreaksBefore(toItem->dockBreaksBefore());
            toItem->setDockBreaksBefore(fromBreaks);
        }
    }

    m_appModel->move(fromIndex, toIndex);
    emit appsChanged();
    QTimer::singleShot(250, this, &DockManager::saveApps);
}

void DockManager::removeApp(int index) {
    if (index < 0 || index >= m_appModel->size()) return;

    auto *item = m_appModel->at(index);
    if (item) {
        item->setIsRemoving(true);
        // QML morph-out animation will call finalizeRemoveApp when done
    }
}

void DockManager::removeAppById(const QString &id) {
    for (AppItem *item : m_appModel->items()) {
        if (item && item->id() == id) {
            item->setIsRemoving(true);
            return;
        }
    }
}

void DockManager::finalizeRemoveApp(const QString &id) {
    for (int i = 0; i < m_appModel->size(); ++i) {
        auto *item = m_appModel->at(i);
        if (item && item->id() == id) {
            m_appModel->removeAt(i);
            item->deleteLater();
            saveApps();
            emit appsChanged();
            return;
        }
    }
}

void DockManager::pinApp(const QString &id) {
    for (AppItem *item : m_appModel->items()) {
        if (item && item->id() == id) {
            // Cancel any in-progress removal if user pins the app
            if (item->isRemoving()) {
                item->setIsRemoving(false);
            }
            item->setIsPinned(true);
            saveApps();
            emit appsChanged();
            return;
        }
    }
}

void DockManager::unpinApp(const QString &id) {
    for (int i = 0; i < m_appModel->size(); ++i) {
        auto *item = m_appModel->at(i);
        if (item && item->id() == id) {
            if (item->windowCount() > 0) {
                item->setIsPinned(false);
                saveApps();
                emit appsChanged();
            } else {
                // Mark for animated removal; QML will call finalizeRemoveApp
                item->setIsRemoving(true);
            }
            return;
        }
    }
}

void DockManager::showAllWindows(const QString &id) {
    for (AppItem *item : m_appModel->items()) {
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
    for (AppItem *item : m_appModel->items()) {
        if (item && item->id() == id) {
            item->setDockBreaksBefore(!item->dockBreaksBefore());
            saveApps();
            return;
        }
    }
}

void DockManager::addApp(const QString &id, const QString &title, const QString &icon, const QString &execCommand, bool dockBreaksBefore, int insertIndex) {
    QString cleanBaseId = id.trimmed();
    if (cleanBaseId.isEmpty()) cleanBaseId = "item";

    QString uniqueId = cleanBaseId;
    int counter = 1;
    bool exists = true;
    while (exists) {
        exists = false;
        for (AppItem *item : m_appModel->items()) {
            if (item && item->id() == uniqueId) {
                exists = true;
                uniqueId = QString("%1_%2").arg(cleanBaseId).arg(counter++);
                break;
            }
        }
    }

    auto *item = new AppItem(uniqueId, title, icon, execCommand, dockBreaksBefore, false, true, this);
    if (insertIndex >= 0 && insertIndex <= m_appModel->size()) {
        m_appModel->insert(insertIndex, item);
    } else {
        m_appModel->append(item);
    }
    emit appsChanged();
    saveApps();
}

QString DockManager::resolveSystemIcon(const QString &iconName) {
    if (iconName.isEmpty()) return "qrc:/icons/launchpad/256.png";
    if (iconName.startsWith("/") || iconName.startsWith("file://") || iconName.startsWith("qrc:/")) {
        QString rawPath = iconName.startsWith("file://") ? iconName.mid(7) : iconName;
        if (iconName.startsWith("qrc:/") || QFile::exists(rawPath)) {
            return iconName.startsWith("/") ? ("file://" + iconName) : iconName;
        }
    }

    QString cleanName = iconName;
    if (cleanName.endsWith(".png") || cleanName.endsWith(".svg") || cleanName.endsWith(".xpm")) {
        cleanName = QFileInfo(cleanName).completeBaseName();
    }

    // Special macOS Dock bundled overrides
    if (cleanName == "spectacle" || cleanName == "org.kde.spectacle" || cleanName == "accessories-screenshot") {
        return "qrc:/icons/spectacle/256.png";
    }
    if (cleanName == "antigravity" || cleanName == "antigravity-ide") {
        return "qrc:/icons/antigravity/256.png";
    }

    QString home = QDir::homePath();
    QStringList searchPaths = {
        home + "/.local/share/icons/MacTahoe/apps/scalable/",
        home + "/.local/share/icons/MacTahoe-dark/apps/scalable/",
        home + "/.local/share/icons/WhiteSur/apps/scalable/",
        home + "/.local/share/icons/MacTahoe/apps/256x256/",
        home + "/.local/share/icons/MacTahoe-dark/apps/256x256/",
        home + "/.local/share/icons/WhiteSur/apps/256x256/",
        home + "/.local/share/icons/MacTahoe/mimes/scalable/",
        home + "/.local/share/icons/WhiteSur/mimes/scalable/",
        home + "/.local/share/icons/MacTahoe/places/scalable/",
        home + "/.local/share/icons/WhiteSur/places/scalable/",
        "/var/lib/flatpak/exports/share/icons/hicolor/scalable/apps/",
        "/var/lib/flatpak/exports/share/icons/hicolor/256x256/apps/",
        home + "/.local/share/flatpak/exports/share/icons/hicolor/scalable/apps/",
        home + "/.local/share/flatpak/exports/share/icons/hicolor/256x256/apps/",
        "/usr/share/icons/hicolor/256x256/apps/",
        "/usr/share/icons/hicolor/128x128/apps/",
        "/usr/share/icons/hicolor/scalable/apps/",
        "/usr/share/icons/hicolor/64x64/apps/",
        "/usr/share/icons/hicolor/48x48/apps/",
        "/usr/share/icons/breeze/apps/48/",
        "/usr/share/icons/breeze/mimetypes/64/",
        "/usr/share/pixmaps/",
        home + "/.local/share/icons/hicolor/256x256/apps/",
        home + "/.local/share/icons/hicolor/scalable/apps/"
    };

    QStringList extensions = {".svg", ".png", ".xpm"};

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

QString DockManager::findDesktopFilePath(const QString &nameOrPath) {
    QString cleaned = nameOrPath.trimmed();
    if (cleaned.startsWith("file://")) {
        cleaned = QUrl(cleaned).toLocalFile();
    } else if (cleaned.startsWith("applications:")) {
        cleaned.remove(QRegularExpression("^applications:/+"));
    }

    if (cleaned.startsWith("/") && QFile::exists(cleaned) && cleaned.endsWith(".desktop", Qt::CaseInsensitive)) {
        return cleaned;
    }

    QString base = QFileInfo(cleaned).fileName();
    if (base.endsWith(".desktop", Qt::CaseInsensitive)) {
        base.chop(8);
    }
    if (base.isEmpty()) return QString();

    QString home = QDir::homePath();
    QStringList appDirs = {
        home + "/.local/share/applications/",
        "/usr/share/applications/",
        "/var/lib/flatpak/exports/share/applications/",
        home + "/.local/share/flatpak/exports/share/applications/",
        "/var/lib/snapd/desktop/applications/"
    };

    QStringList candidates = {
        base + ".desktop",
        base.toLower() + ".desktop",
        "org.kde." + base.toLower() + ".desktop",
        "org.gnome." + base.toLower() + ".desktop"
    };

    for (const QString &dirPath : appDirs) {
        for (const QString &cand : candidates) {
            QString full = dirPath + cand;
            if (QFile::exists(full)) return full;
        }
        QDir dir(dirPath);
        if (dir.exists()) {
            const QStringList entries = dir.entryList({"*.desktop"}, QDir::Files);
            for (const QString &entry : entries) {
                if (entry.compare(base + ".desktop", Qt::CaseInsensitive) == 0 ||
                    entry.endsWith("." + base + ".desktop", Qt::CaseInsensitive)) {
                    return dirPath + entry;
                }
            }
        }
    }
    return QString();
}

QString DockManager::resolveFolderIcon(const QFileInfo &fi) {
    QString name = fi.fileName().toLower();
    QString folderSvg = "blue-folder.svg";

    if (name.contains("download")) {
        folderSvg = "blue-folder-download.svg";
    } else if (name.contains("document") || name == "docs" || name == "papers") {
        folderSvg = "blue-folder-documents.svg";
    } else if (name.contains("picture") || name.contains("photo") || name.contains("image") || name.contains("screenshot") || name.contains("wallpaper")) {
        folderSvg = "blue-folder-images.svg";
    } else if (name.contains("music") || name.contains("audio") || name.contains("song") || name.contains("podcast")) {
        folderSvg = "blue-folder-music.svg";
    } else if (name.contains("video") || name.contains("movie") || name.contains("film") || name.contains("clip")) {
        folderSvg = "blue-folder-videos.svg";
    } else if (name == "desktop") {
        folderSvg = "blue-user-desktop.svg";
    } else if (name.contains("code") || name.contains("antigravity") || name.contains("project") || name.contains("dev") || name.contains("src") || name.contains("git") || name.contains("repo")) {
        folderSvg = "blue-folder-code.svg";
    } else if (name == QDir::home().dirName().toLower()) {
        folderSvg = "blue-user-home.svg";
    }

    QString tahoePath = QDir::homePath() + "/.local/share/icons/MacTahoe/places/scalable/" + folderSvg;
    if (QFile::exists(tahoePath)) {
        return "file://" + tahoePath;
    }
    QString fallbackTahoe = QDir::homePath() + "/.local/share/icons/MacTahoe/places/scalable/blue-folder.svg";
    if (QFile::exists(fallbackTahoe)) {
        return "file://" + fallbackTahoe;
    }
    return "qrc:/icons/finder/256.png";
}

QString DockManager::createSquircleIconFromImage(const QImage &srcImg, const QString &cacheKey, bool addVideoPlayBadge) {
    if (srcImg.isNull()) {
        return addVideoPlayBadge ? "qrc:/icons/tv/256.png" : "qrc:/icons/photos/256.png";
    }

    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/macos-dock/dropped_icons";
    QDir().mkpath(cacheDir);
    QString outPath = cacheDir + "/" + cacheKey + ".png";

    QImage canvas(256, 256, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QRectF iconRect(22.0, 22.0, 212.0, 212.0);
    qreal radius = 47.0;

    QPainterPath squircle;
    squircle.addRoundedRect(iconRect, radius, radius);

    p.save();
    p.setClipPath(squircle);

    // Subtle dark backing in case the image has transparent regions
    p.fillRect(iconRect, QColor(28, 28, 32));

    // Aspect-fill (cover crop) image into 212x212
    QImage scaled = srcImg.scaled(212, 212, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    int srcX = qMax(0, (scaled.width() - 212) / 2);
    int srcY = qMax(0, (scaled.height() - 212) / 2);
    p.drawImage(iconRect.topLeft(), scaled, QRect(srcX, srcY, 212, 212));

    if (addVideoPlayBadge) {
        // Minimal translucent play pill in center
        QRectF badgeRect(94.0, 94.0, 68.0, 68.0);
        p.setPen(QPen(QColor(255, 255, 255, 210), 2.0));
        p.setBrush(QColor(0, 0, 0, 145));
        p.drawEllipse(badgeRect);

        QPainterPath playTri;
        playTri.moveTo(121.0, 114.0);
        playTri.lineTo(143.0, 128.0);
        playTri.lineTo(121.0, 142.0);
        playTri.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 245));
        p.drawPath(playTri);
    }

    p.restore();

    // Crisp Apple specular inner rim
    p.setPen(QPen(QColor(255, 255, 255, 65), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(iconRect.adjusted(0.75, 0.75, -0.75, -0.75), radius, radius);
    p.end();

    canvas.save(outPath, "PNG");
    return "file://" + outPath;
}

QString DockManager::extractVideoSquircleIcon(const QString &videoPath, const QString &cacheKey) {
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/macos-dock/dropped_icons";
    QDir().mkpath(cacheDir);
    QString tmpFrame = cacheDir + "/tmp_" + cacheKey + ".png";

    auto tryExtract = [&](const QString &timestamp) -> bool {
        QProcess ff;
        ff.start("ffmpeg", {"-y", "-ss", timestamp, "-i", videoPath, "-frames:v", "1", "-vf", "scale=320:-1", tmpFrame});
        if (!ff.waitForFinished(1500)) {
            ff.kill();
            ff.waitForFinished(200);
            return false;
        }
        return QFile::exists(tmpFrame) && QFileInfo(tmpFrame).size() > 0;
    };

    if (tryExtract("00:00:01") || tryExtract("00:00:00")) {
        QImage frame(tmpFrame);
        QFile::remove(tmpFrame);
        if (!frame.isNull()) {
            return createSquircleIconFromImage(frame, cacheKey, true);
        }
    } else {
        QFile::remove(tmpFrame);
    }

    return "qrc:/icons/tv/256.png";
}

QString DockManager::createTextSnippetIcon(const QString &text, const QString &cacheKey) {
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/macos-dock/dropped_icons";
    QDir().mkpath(cacheDir);
    QString outPath = cacheDir + "/" + cacheKey + ".png";

    QImage canvas(256, 256, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QRectF iconRect(22.0, 22.0, 212.0, 212.0);
    qreal radius = 47.0;

    QPainterPath squircle;
    squircle.addRoundedRect(iconRect, radius, radius);

    p.save();
    p.setClipPath(squircle);

    // Warm Apple Notes card background
    p.fillRect(iconRect, QColor(250, 248, 242));

    // Classic Apple Notes amber/gold header band
    QLinearGradient headerGrad(0, 22, 0, 70);
    headerGrad.setColorAt(0.0, QColor(255, 208, 44));
    headerGrad.setColorAt(1.0, QColor(240, 180, 18));
    p.fillRect(QRectF(22.0, 22.0, 212.0, 48.0), headerGrad);

    // Subtle paper ruled lines
    p.setPen(QPen(QColor(0, 0, 0, 18), 1.0));
    for (int y = 104; y <= 206; y += 26) {
        p.drawLine(36, y, 220, y);
    }

    // Render live snippet text preview on the card
    QString cleanSnippet = text.trimmed();
    cleanSnippet.replace("\r\n", "\n");
    if (cleanSnippet.length() > 140) {
        cleanSnippet = cleanSnippet.left(140) + "...";
    }

    QFont font("sans-serif");
    font.setPixelSize(17);
    font.setWeight(QFont::DemiBold);
    p.setFont(font);
    p.setPen(QColor(40, 40, 44));
    p.drawText(QRectF(36.0, 78.0, 184.0, 144.0), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, cleanSnippet);

    p.restore();

    // Subtle outer border
    p.setPen(QPen(QColor(0, 0, 0, 40), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(iconRect.adjusted(0.75, 0.75, -0.75, -0.75), radius, radius);
    p.end();

    canvas.save(outPath, "PNG");
    return "file://" + outPath;
}

QString DockManager::createDocumentBadgeIcon(const QString &ext, const QString &cacheKey) {
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/macos-dock/dropped_icons";
    QDir().mkpath(cacheDir);
    QString outPath = cacheDir + "/" + cacheKey + ".png";

    QImage canvas(256, 256, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QRectF iconRect(22.0, 22.0, 212.0, 212.0);
    qreal radius = 47.0;

    QPainterPath squircle;
    squircle.addRoundedRect(iconRect, radius, radius);

    p.save();
    p.setClipPath(squircle);

    QLinearGradient bgGrad(0, 22, 0, 234);
    bgGrad.setColorAt(0.0, QColor(248, 249, 252));
    bgGrad.setColorAt(1.0, QColor(226, 229, 236));
    p.fillRect(iconRect, bgGrad);

    // Top accent bar
    p.fillRect(QRectF(22.0, 22.0, 212.0, 42.0), QColor(10, 132, 255));

    // Extension pill/text
    QString badge = ext.toUpper().trimmed();
    if (badge.isEmpty()) badge = "FILE";
    if (badge.length() > 6) badge = badge.left(6);

    QFont font("sans-serif");
    font.setPixelSize(badge.length() <= 4 ? 42 : 32);
    font.setWeight(QFont::Bold);
    p.setFont(font);
    p.setPen(QColor(34, 36, 42));
    p.drawText(QRectF(32.0, 64.0, 192.0, 156.0), Qt::AlignCenter, badge);

    p.restore();

    p.setPen(QPen(QColor(0, 0, 0, 40), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(iconRect.adjusted(0.75, 0.75, -0.75, -0.75), radius, radius);
    p.end();

    canvas.save(outPath, "PNG");
    return "file://" + outPath;
}

QString DockManager::resolveMimeIcon(const QFileInfo &fi) {
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(fi);

    QString home = QDir::homePath();
    QStringList mimeDirs = {
        home + "/.local/share/icons/MacTahoe/mimes/scalable/",
        home + "/.local/share/icons/WhiteSur/mimes/scalable/",
        "/usr/share/icons/breeze/mimetypes/64/",
        "/usr/share/icons/hicolor/scalable/mimetypes/",
        "/usr/share/icons/hicolor/256x256/mimetypes/"
    };

    QStringList iconCandidates;
    if (mime.isValid()) {
        if (!mime.iconName().isEmpty()) iconCandidates.append(mime.iconName());
        if (!mime.genericIconName().isEmpty()) iconCandidates.append(mime.genericIconName());
    }

    for (const QString &cand : iconCandidates) {
        for (const QString &dir : mimeDirs) {
            if (QFile::exists(dir + cand + ".svg")) return "file://" + dir + cand + ".svg";
            if (QFile::exists(dir + cand + ".png")) return "file://" + dir + cand + ".png";
        }
    }

    QByteArray hash = QCryptographicHash::hash(fi.absoluteFilePath().toUtf8(), QCryptographicHash::Md5).toHex().left(12);
    return createDocumentBadgeIcon(fi.suffix(), "doc_" + QString::fromLatin1(hash));
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
        exec = "gtk-launch " + QFileInfo(path).fileName() + " || xdg-open \"" + path + "\"";
    }

    return QFileInfo(path).baseName().toLower();
}

bool DockManager::addSingleLocalPath(const QString &localPath, int &insertIndex, bool &placeAfterDivider) {
    QString cleanPath = localPath.trimmed();
    if (cleanPath.startsWith("~/")) {
        cleanPath = QDir::homePath() + cleanPath.mid(1);
    }

    // 1. Check if it's a .desktop file or application name
    if (cleanPath.endsWith(".desktop", Qt::CaseInsensitive)) {
        QString resolvedDesktop = findDesktopFilePath(cleanPath);
        if (!resolvedDesktop.isEmpty()) {
            QString title, icon, exec;
            QString id = parseDesktopFile(resolvedDesktop, title, icon, exec);
            if (!title.isEmpty()) {
                bool breaks = false;
                if (placeAfterDivider && insertIndex >= 0 && insertIndex < m_appModel->size() && m_appModel->at(insertIndex)->dockBreaksBefore()) {
                    breaks = true;
                    m_appModel->at(insertIndex)->setDockBreaksBefore(false);
                    placeAfterDivider = false;
                }
                addApp(id, title, icon, exec, breaks, insertIndex);
                if (insertIndex >= 0) insertIndex++;
                return true;
            }
        }
    }

    QFileInfo fi(cleanPath);
    if (!fi.exists()) return false;

    QString absPath = fi.absoluteFilePath();
    QByteArray pathHash = QCryptographicHash::hash(absPath.toUtf8(), QCryptographicHash::Md5).toHex().left(10);
    QString hashStr = QString::fromLatin1(pathHash);

    auto commitAdd = [&](const QString &id, const QString &title, const QString &icon, const QString &exec) {
        bool breaks = false;
        if (placeAfterDivider && insertIndex >= 0 && insertIndex < m_appModel->size() && m_appModel->at(insertIndex)->dockBreaksBefore()) {
            breaks = true;
            m_appModel->at(insertIndex)->setDockBreaksBefore(false);
            placeAfterDivider = false;
        }
        addApp(id, title, icon, exec, breaks, insertIndex);
        if (insertIndex >= 0) insertIndex++;
    };

    // 2. Folder / Directory
    if (fi.isDir()) {
        QString title = fi.fileName().isEmpty() ? absPath : fi.fileName();
        QString icon = resolveFolderIcon(fi);
        QString exec = QString("dolphin \"%1\" || xdg-open \"%1\"").arg(absPath);
        commitAdd("dir_" + hashStr, title, icon, exec);
        return true;
    }

    QMimeDatabase mimeDb;
    QMimeType mime = mimeDb.mimeTypeForFile(fi);
    QString mimeName = mime.name().toLower();
    QString ext = fi.suffix().toLower();

    static const QSet<QString> imageExts = {"png", "jpg", "jpeg", "webp", "gif", "bmp", "svg", "ico", "avif", "tif", "tiff", "heic"};
    static const QSet<QString> videoExts = {"mp4", "mkv", "webm", "mov", "avi", "m4v", "flv", "wmv", "mpg", "mpeg", "ts", "3gp"};
    static const QSet<QString> audioExts = {"mp3", "flac", "wav", "ogg", "m4a", "aac", "opus", "wma", "aiff"};

    // 3. Image File -> Rounded Squircle Live Thumbnail Icon
    if (mimeName.startsWith("image/") || imageExts.contains(ext)) {
        QImage img(absPath);
        QString icon = !img.isNull()
            ? createSquircleIconFromImage(img, "img_" + hashStr, false)
            : (ext == "svg" ? ("file://" + absPath) : "qrc:/icons/photos/256.png");
        QString title = fi.fileName();
        QString exec = QString("gwenview \"%1\" || xdg-open \"%1\"").arg(absPath);
        commitAdd("img_" + hashStr, title, icon, exec);
        return true;
    }

    // 4. Video File -> Extracted Frame in Rounded Squircle + Play Badge
    if (mimeName.startsWith("video/") || videoExts.contains(ext)) {
        QString icon = extractVideoSquircleIcon(absPath, "vid_" + hashStr);
        QString title = fi.fileName();
        QString exec = QString("vlc \"%1\" || mpv \"%1\" || xdg-open \"%1\"").arg(absPath);
        commitAdd("vid_" + hashStr, title, icon, exec);
        return true;
    }

    // 5. Audio File -> Embedded Cover Art Squircle or Music Icon
    if (mimeName.startsWith("audio/") || audioExts.contains(ext)) {
        QString icon = "qrc:/icons/music/256.png";
        QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/macos-dock/dropped_icons";
        QDir().mkpath(cacheDir);
        QString tmpCover = cacheDir + "/tmp_aud_" + hashStr + ".png";
        QProcess ff;
        ff.start("ffmpeg", {"-y", "-i", absPath, "-an", "-vcodec", "png", "-frames:v", "1", tmpCover});
        if (ff.waitForFinished(1200) && QFile::exists(tmpCover) && QFileInfo(tmpCover).size() > 0) {
            QImage cover(tmpCover);
            QFile::remove(tmpCover);
            if (!cover.isNull()) {
                icon = createSquircleIconFromImage(cover, "aud_" + hashStr, false);
            }
        } else {
            QFile::remove(tmpCover);
        }
        QString title = fi.fileName();
        QString exec = QString("elisa \"%1\" || vlc \"%1\" || xdg-open \"%1\"").arg(absPath);
        commitAdd("aud_" + hashStr, title, icon, exec);
        return true;
    }

    // 6. Executable / AppImage / Script
    if (ext == "appimage" || (fi.isExecutable() && !mimeName.startsWith("text/"))) {
        if (ext == "appimage" && !fi.isExecutable()) {
            QFile::setPermissions(absPath, fi.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup);
        }
        QString baseTitle = fi.completeBaseName();
        // Clean version suffixes from AppImage names (e.g. "Obsidian-1.5.3-x86_64" -> "Obsidian")
        if (ext == "appimage") {
            baseTitle.remove(QRegularExpression("[-_][vV]?\\d+.*$"));
        }
        if (!baseTitle.isEmpty()) baseTitle[0] = baseTitle[0].toUpper();

        QString icon = resolveSystemIcon(baseTitle.toLower());
        if (icon == "qrc:/icons/launchpad/256.png" && ext != "appimage") {
            icon = "qrc:/icons/terminal/256.png";
        }
        QString exec = QString("\"%1\"").arg(absPath);
        commitAdd(baseTitle.toLower(), baseTitle, icon, exec);
        return true;
    }

    // 7. All Other Files (PDFs, Code, Markdown, Archives, Office Docs, etc.)
    QString title = fi.fileName();
    QString icon = resolveMimeIcon(fi);
    QString exec = QString("xdg-open \"%1\"").arg(absPath);
    commitAdd("file_" + hashStr, title, icon, exec);
    return true;
}

void DockManager::handleDrop(const QList<QUrl> &urls, const QString &text, int insertIndex, bool placeAfterDivider) {
    qDebug() << "[DockManager] handleDrop urls:" << urls.size() << "text len:" << text.length() << "insertIndex:" << insertIndex;

    int initialCount = m_appModel->size();
    if (!urls.isEmpty()) {
        addAppsFromUrls(urls, insertIndex, placeAfterDivider);
    }

    // If URLs did not add anything (or no URLs were provided) and text is non-empty, process text
    if (m_appModel->size() == initialCount && !text.trimmed().isEmpty()) {
        addAppFromText(text, insertIndex, placeAfterDivider);
    }
}

void DockManager::addAppsFromUrls(const QList<QUrl> &urls, int insertIndex, bool placeAfterDivider) {
    QSet<QString> seen;
    for (const QUrl &url : urls) {
        QString urlStr = url.toString().trimmed();
        if (urlStr.isEmpty() || seen.contains(urlStr)) continue;
        seen.insert(urlStr);

        if (url.isLocalFile()) {
            addSingleLocalPath(url.toLocalFile(), insertIndex, placeAfterDivider);
        } else if (url.scheme() == "applications" || urlStr.endsWith(".desktop", Qt::CaseInsensitive)) {
            QString desktopPath = findDesktopFilePath(urlStr);
            if (!desktopPath.isEmpty()) {
                addSingleLocalPath(desktopPath, insertIndex, placeAfterDivider);
            }
        } else if (urlStr.startsWith("data:image/", Qt::CaseInsensitive)) {
            int commaIdx = urlStr.indexOf(',');
            if (commaIdx > 0) {
                QByteArray raw = QByteArray::fromBase64(urlStr.mid(commaIdx + 1).toLatin1());
                QImage img = QImage::fromData(raw);
                if (!img.isNull()) {
                    qint64 ts = QDateTime::currentMSecsSinceEpoch();
                    QString saveDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/dropped_images";
                    QDir().mkpath(saveDir);
                    QString imgPath = QString("%1/image_%2.png").arg(saveDir).arg(ts);
                    img.save(imgPath, "PNG");
                    addSingleLocalPath(imgPath, insertIndex, placeAfterDivider);
                }
            }
        } else {
            addAppFromText(urlStr, insertIndex, placeAfterDivider);
        }
    }
}

void DockManager::addAppFromText(const QString &text, int insertIndex, bool placeAfterDivider) {
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;

    auto commitAdd = [&](const QString &id, const QString &title, const QString &icon, const QString &exec) {
        bool breaks = false;
        if (placeAfterDivider && insertIndex >= 0 && insertIndex < m_appModel->size() && m_appModel->at(insertIndex)->dockBreaksBefore()) {
            breaks = true;
            m_appModel->at(insertIndex)->setDockBreaksBefore(false);
            placeAfterDivider = false;
        }
        addApp(id, title, icon, exec, breaks, insertIndex);
        if (insertIndex >= 0) insertIndex++;
    };

    // 1. Check if the text is a list of file:// URLs or local paths (one per line)
    QStringList lines = trimmed.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    if (!lines.isEmpty()) {
        bool allPathsOrUrls = true;
        for (const QString &line : lines) {
            QString l = line.trimmed();
            if (l.startsWith("#")) continue;
            bool isFileUrl = l.startsWith("file://", Qt::CaseInsensitive) || l.startsWith("applications:", Qt::CaseInsensitive);
            bool isLocalPath = (l.startsWith("/") || l.startsWith("~/")) && QFileInfo::exists(l.startsWith("~/") ? (QDir::homePath() + l.mid(1)) : l);
            bool isDesktopName = l.endsWith(".desktop", Qt::CaseInsensitive) && !findDesktopFilePath(l).isEmpty();
            if (!isFileUrl && !isLocalPath && !isDesktopName) {
                allPathsOrUrls = false;
                break;
            }
        }
        if (allPathsOrUrls) {
            for (const QString &line : lines) {
                QString l = line.trimmed();
                if (l.startsWith("#")) continue;
                if (l.startsWith("file://", Qt::CaseInsensitive)) {
                    addSingleLocalPath(QUrl(l).toLocalFile(), insertIndex, placeAfterDivider);
                } else if (l.startsWith("applications:", Qt::CaseInsensitive) || l.endsWith(".desktop", Qt::CaseInsensitive)) {
                    QString dp = findDesktopFilePath(l);
                    if (!dp.isEmpty()) addSingleLocalPath(dp, insertIndex, placeAfterDivider);
                } else {
                    addSingleLocalPath(l, insertIndex, placeAfterDivider);
                }
            }
            return;
        }
    }

    // 2. Check if text is a Web URL (http://, https://, or single-token www.domain.tld)
    bool isWebUrl = trimmed.startsWith("http://", Qt::CaseInsensitive)
                 || trimmed.startsWith("https://", Qt::CaseInsensitive)
                 || (!trimmed.contains(QRegularExpression("\\s")) && trimmed.startsWith("www.", Qt::CaseInsensitive) && trimmed.contains('.'));
    if (isWebUrl) {
        QString fullUrl = trimmed.startsWith("www.", Qt::CaseInsensitive) ? ("https://" + trimmed) : trimmed;
        QUrl url(fullUrl);
        QString host = url.host();
        if (host.startsWith("www.")) host = host.mid(4);

        QString title = host.isEmpty() ? "Web Link" : host;
        if (!title.isEmpty()) {
            title[0] = title[0].toUpper();
        }
        QString pathPart = url.path();
        if (!pathPart.isEmpty() && pathPart != "/") {
            QString cleanSeg = QFileInfo(pathPart).fileName();
            if (!cleanSeg.isEmpty() && cleanSeg.length() <= 20) {
                title += " - " + cleanSeg;
            }
        }

        QString id = QString("web_%1").arg(QDateTime::currentMSecsSinceEpoch());
        QString exec = QString("xdg-open \"%1\"").arg(fullUrl);
        QString icon = "qrc:/icons/safari/256.png";

        commitAdd(id, title, icon, exec);
        return;
    }

    // 3. Check if single-word text matches an installed .desktop application
    if (!trimmed.contains(QRegularExpression("\\s")) && trimmed.length() <= 64) {
        QString dp = findDesktopFilePath(trimmed);
        if (!dp.isEmpty()) {
            addSingleLocalPath(dp, insertIndex, placeAfterDivider);
            return;
        }
    }

    // 4. Plain Text / Note / Code Snippet -> Save to persistent note file & generate live Note squircle icon!
    qint64 ts = QDateTime::currentMSecsSinceEpoch();
    QString notesDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notes";
    QDir().mkpath(notesDir);
    QString notePath = QString("%1/note_%2.txt").arg(notesDir).arg(ts);

    QFile noteFile(notePath);
    if (noteFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        noteFile.write(trimmed.toUtf8());
        noteFile.close();
    }

    QString firstLine = lines.isEmpty() ? trimmed : lines.first().trimmed();
    if (firstLine.length() > 28) {
        firstLine = firstLine.left(25) + "...";
    }
    QString title = firstLine.isEmpty() ? "Text Note" : firstLine;
    QString cacheKey = QString("note_%1").arg(ts);
    QString icon = createTextSnippetIcon(trimmed, cacheKey);
    QString exec = QString("kate \"%1\" || kwrite \"%1\" || xdg-open \"%1\"").arg(notePath);

    commitAdd(cacheKey, title, icon, exec);
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
    if (id == "recordly") return "recordly";
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
    QThread::msleep(40);

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

    // Immediately trigger a one-shot query to guarantee zero startup delay
    refreshRunningStatus();
}

void DockManager::cleanupKWinWindowTracker() {
    QDBusInterface scriptingInterface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (scriptingInterface.isValid()) {
        scriptingInterface.call("unloadScript", "macos_dock_tracker");
    }
    m_kwinTrackerScriptId = -1;
}

void DockManager::checkTrackerHealth() {
    qint64 secsSinceLast = m_lastWindowUpdateTime.isValid() ? m_lastWindowUpdateTime.secsTo(QDateTime::currentDateTime()) : 999;

    // Layer 1: If no update received in >= 3 seconds, query windows immediately via one-shot script
    if (secsSinceLast >= 3) {
        refreshRunningStatus();
    }

    // Layer 2: If no update received in >= 6 seconds, the persistent tracker has stalled or stopped. Reinstall it.
    if (secsSinceLast >= 6) {
        qDebug() << "[DockManager] Tracker script stalled (no updates for" << secsSinceLast << "s). Reinstalling tracker...";
        setupKWinWindowTracker();
    }
}

void DockManager::updateWindows(const QString &json) {
    m_lastWindowUpdateTime = QDateTime::currentDateTime();
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isArray()) {
        matchWindowsToApps(doc.array());
    } else {
        qWarning() << "[DockManager] updateWindows received non-array JSON:" << json.left(100);
    }
}

void DockManager::matchWindowsToApps(const QJsonArray &windowList) {
    QString currentSummary;
    for (const QJsonValue &val : windowList) {
        if (!val.isObject()) continue;
        QJsonObject o = val.toObject();
        currentSummary += QString("[%1:%2:%3]").arg(o["id"].toString(), o["desktopFile"].toString(), o["active"].toBool() ? "1" : "0");
    }

    bool stateChanged = (currentSummary != m_lastWindowSummary);
    if (stateChanged) {
        m_lastWindowSummary = currentSummary;
        qDebug() << "[DockManager] matchWindowsToApps state changed, received" << windowList.size() << "windows";
    }

    // Map of AppItem* -> QVariantList of window maps
    QMap<AppItem*, QVariantList> appWindows;
    for (AppItem *item : m_appModel->items()) {
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
        for (AppItem *item : m_appModel->items()) {
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
        if (stateChanged && (it.key()->id() == "terminal" || it.key()->windowCount() > 0)) {
            qDebug() << "[DockManager] App assigned:" << it.key()->id() << "windows:" << it.key()->windowCount();
        }
        if (it.key()->windowCount() > 0 && m_launchingAppIds.contains(it.key()->id())) {
            m_launchingAppIds.remove(it.key()->id());
            emit appLaunchFinished(it.key()->id());
            emit hasLaunchingAppChanged();
        }
    }

    if (stateChanged) {
        for (const auto &uw : unassigned) {
            if (uw.desktopFile.contains("konsole") || uw.resourceClass.contains("konsole") || uw.resourceName.contains("konsole")) {
                qDebug() << "[DockManager] UNASSIGNED KONSOLE WINDOW:" << uw.id << uw.desktopFile << uw.resourceClass << uw.caption;
            }
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
        for (AppItem *item : m_appModel->items()) {
            if (item && item->id() == key) {
                targetItem = item;
                // Cancel any in-progress removal — app has windows again
                if (item->isRemoving()) {
                    item->setIsRemoving(false);
                }
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

            // Search desktop files for real name & icon using full unified lookup
            QString desktopPath = findDesktopFilePath(cleanKey);
            if (desktopPath.isEmpty() && !wins.isEmpty()) {
                if (!wins.first().resourceClass.isEmpty()) {
                    desktopPath = findDesktopFilePath(wins.first().resourceClass);
                }
                if (desktopPath.isEmpty() && !wins.first().resourceName.isEmpty()) {
                    desktopPath = findDesktopFilePath(wins.first().resourceName);
                }
            }

            if (!desktopPath.isEmpty() && QFile::exists(desktopPath)) {
                QString dTitle, dIcon, dExec;
                parseDesktopFile(desktopPath, dTitle, dIcon, dExec);
                if (!dTitle.isEmpty()) title = dTitle;
                if (!dIcon.isEmpty()) iconName = dIcon;
                if (!dExec.isEmpty()) exec = dExec;
            } else {
                iconName = resolveSystemIcon(key);
                if (title.isEmpty()) {
                    title = (!wins.isEmpty() && !wins.first().resourceClass.isEmpty()) ? wins.first().resourceClass : key;
                }
                if (!title.isEmpty()) title[0] = title[0].toUpper();
            }

            if (cleanKey.contains("spectacle", Qt::CaseInsensitive)) {
                iconName = "qrc:/icons/spectacle/256.png";
                title = "Spectacle";
            }

            targetItem = new AppItem(key, title, iconName, exec, false, false, false, this);

            // In macOS, running unpinned apps appear on the left of the divider (before files/trash)
            int insertIndex = -1;
            for (int i = 0; i < m_appModel->size(); ++i) {
                auto *item = m_appModel->at(i);
                if (item && item->dockBreaksBefore()) {
                    insertIndex = i;
                    break;
                }
            }
            if (insertIndex >= 0) {
                m_appModel->insert(insertIndex, targetItem);
            } else {
                m_appModel->append(targetItem);
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
    for (int i = m_appModel->size() - 1; i >= 0; --i) {
        auto *item = m_appModel->at(i);
        if (item && !item->isPinned()) {
            QString idLower = item->id().toLower();
            bool isBlacklisted = idLower.contains("macos-dock") || idLower == "macos dock" ||
                                 idLower.contains("videobridge") ||
                                 idLower == "plasmashell" || idLower == "krunner";

            if (isBlacklisted) {
                // Force-remove blacklisted apps instantly (they should never appear)
                m_appModel->removeAt(i);
                item->deleteLater();
                structureChanged = true;
            } else if (item->windowCount() == 0 && !item->isRemoving()) {
                // Mark for animated removal — QML morph-out will call finalizeRemoveApp
                item->setIsRemoving(true);
            } else if (item->isRemoving() && item->removingElapsedMs() > 4000) {
                // Safety fallback: if animation didn't complete in 4 seconds, force remove
                qDebug() << "[DockManager] Safety fallback: force removing stuck item" << item->id();
                m_appModel->removeAt(i);
                item->deleteLater();
                structureChanged = true;
            }
        }
    }

    if (m_previewProvider) {
        QSet<QString> activeWinIds;
        for (AppItem *item : m_appModel->items()) {
            if (item) {
                for (const QVariant &v : item->windows()) {
                    QString id = v.toMap().value("id").toString();
                    if (!id.isEmpty()) activeWinIds.insert(id);
                }
            }
        }
        m_previewProvider->pruneTo(activeWinIds);
    }

    if (structureChanged) {
        emit appsChanged();
    }
}

void DockManager::activateWindow(const QString &windowId) {
    // Immediately update local active state across apps for instant UI response and multi-window cycling
    for (AppItem *item : m_appModel->items()) {
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
    // Find the app in m_appModel
    AppItem *targetApp = nullptr;
    for (AppItem *item : m_appModel->items()) {
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
    bool isResourceItem = id.startsWith("dir_") || id.startsWith("img_") || id.startsWith("vid_") ||
                          id.startsWith("aud_") || id.startsWith("file_") || id.startsWith("note_") ||
                          id.startsWith("web_") || id == "view-source" || id == "wallpapers";

    if (isResourceItem) {
        emit appSwitched(id);
        emit appLaunched(id);
        for (AppItem *item : m_appModel->items()) {
            if (item && item->id() == id) {
                launchCommand(item->execCommand());
                return;
            }
        }
        return;
    }

    if (m_launchingAppIds.contains(id)) {
        return;
    }

    m_launchingAppIds.insert(id);
    emit hasLaunchingAppChanged();
    emit appLaunchStarted(id);
    emit appLaunched(id);

    // Timeout safety fallback if process never creates a window
    QTimer::singleShot(4000, this, [this, id]() {
        if (m_launchingAppIds.remove(id)) {
            emit appLaunchFinished(id);
            emit hasLaunchingAppChanged();
        }
    });

    for (AppItem *item : m_appModel->items()) {
        if (item && item->id() == id) {
            launchCommand(item->execCommand());
            return;
        }
    }
}

void DockManager::minimizeApp(const QString &id) {
    AppItem *targetApp = nullptr;
    for (AppItem *item : m_appModel->items()) {
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
    for (AppItem *item : m_appModel->items()) {
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
    m_appModel->clear();

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
        {"recordly", "Recordly", "file:///home/gaurav/.local/share/icons/recordly.png", "/home/gaurav/.local/bin/recordly", false},
        {"wallpapers", "Wallpapers", "qrc:/icons/wallpapers/256.png", "systemsettings kcm_desktoptheme || xdg-open /usr/share/wallpapers", true},
        {"view-source", "GitHub", "qrc:/icons/view-source/256.png", "xdg-open https://github.com/GauravGadhari", false}
    };

    for (const auto &item : defaultList) {
        auto *app = new AppItem(item.id, item.title, item.icon, item.execCommand, item.dockBreaksBefore, false, true, this);
        m_appModel->append(app);
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
                "X-KDE-Unique=true\n"
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
    // Launch via systemd-run --user --scope so launched apps have their own independent cgroups
    // and do not inherit or pollute the dock's service cgroup or terminate if dock restarts.
    bool ok = QProcess::startDetached("systemd-run", QStringList() << "--user" << "--scope" << "sh" << "-c" << QString("( %1 )").arg(command));
    if (!ok) {
        QProcess::startDetached("sh", QStringList() << "-c" << QString("( %1 ) >/dev/null 2>&1 &").arg(command));
    }
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
