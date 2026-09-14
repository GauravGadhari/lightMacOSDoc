#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QDebug>
#include <QLockFile>
#include <QDir>
#include <QSessionManager>
#include <QThread>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#include <QSocketNotifier>
#include "dock_manager.h"

static int sigTermFd[2];

static void termSignalHandler(int) {
    char a = 1;
    [[maybe_unused]] auto r = ::write(sigTermFd[0], &a, sizeof(a));
}

int main(int argc, char *argv[]) {
    // Force X11/XCB backend so the dock window can position itself freely at the screen bottom
    // and use hardware input shape masks (setMask) to allow 100% click-through outside the dock pill.
    qputenv("QT_QPA_PLATFORM", "xcb");

    QGuiApplication app(argc, argv);
    app.setApplicationName("macOS Dock");
    app.setOrganizationName("Antigravity");
    app.setWindowIcon(QIcon(":/icons/finder/256.png"));

    // Prevent KDE Plasma Session Manager (ksmserver) from saving/restoring this process on logout/reboot
    // because the dock is independently launched via autostart.
    QObject::connect(&app, &QGuiApplication::saveStateRequest, [](QSessionManager &sm) {
        sm.setRestartHint(QSessionManager::RestartNever);
        sm.setRestartCommand(QStringList());
    });
    QObject::connect(&app, &QGuiApplication::commitDataRequest, [](QSessionManager &sm) {
        sm.setRestartHint(QSessionManager::RestartNever);
        sm.setRestartCommand(QStringList());
    });

    // ── Single-Instance Enforcement ──
    QString lockPath = QDir::tempPath() + QStringLiteral("/macos-dock-%1.lock").arg(getuid());
    QLockFile lockFile(lockPath);
    lockFile.setStaleLockTime(0);

    bool replace = app.arguments().contains(QStringLiteral("--replace"));

    if (replace) {
        qint64 existingPid = 0;
        QString existingHost, existingApp;
        if (lockFile.getLockInfo(&existingPid, &existingHost, &existingApp) && existingPid > 0) {
            qInfo() << "[macOS Dock] Replacing existing dock process (PID:" << existingPid << ")";
            kill(static_cast<pid_t>(existingPid), SIGTERM);
            for (int i = 0; i < 20; ++i) {
                if (kill(static_cast<pid_t>(existingPid), 0) != 0) break;
                QThread::msleep(50);
            }
            if (kill(static_cast<pid_t>(existingPid), 0) == 0) {
                kill(static_cast<pid_t>(existingPid), SIGKILL);
                QThread::msleep(50);
            }
        }
    }

    if (!lockFile.tryLock(200)) {
        qint64 runningPid = 0;
        QString runningHost, runningApp;
        lockFile.getLockInfo(&runningPid, &runningHost, &runningApp);
        qWarning() << "[macOS Dock] Another instance of macOS Dock is already running (PID:" << runningPid << "). Exiting duplicate.";
        return 0;
    }

    // Safe Unix signal handling for SIGTERM and SIGINT
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigTermFd) == 0) {
        auto *sn = new QSocketNotifier(sigTermFd[1], QSocketNotifier::Read, &app);
        QObject::connect(sn, &QSocketNotifier::activated, [&app, sn]() {
            sn->setEnabled(false);
            char a;
            [[maybe_unused]] auto r = ::read(sigTermFd[1], &a, sizeof(a));
            app.quit();
        });
        struct sigaction term{};
        term.sa_handler = termSignalHandler;
        sigemptyset(&term.sa_mask);
        term.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &term, nullptr);
        sigaction(SIGINT, &term, nullptr);
    }

    QQuickWindow::setDefaultAlphaBuffer(true);

    DockManager dockManager;

    QScreen *screen = QGuiApplication::primaryScreen();
    int screenWidth = screen ? screen->geometry().width() : 1920;
    int screenHeight = screen ? screen->geometry().height() : 1200;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("dockManager", &dockManager);
    engine.rootContext()->setContextProperty("realScreenWidth", screenWidth);
    engine.rootContext()->setContextProperty("realScreenHeight", screenHeight);

    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url, &dockManager](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            qCritical() << "Failed to load QML component:" << url;
            QCoreApplication::exit(-1);
        }
        auto *window = qobject_cast<QQuickWindow*>(obj);
        if (window) {
            dockManager.setWindow(window);
        }
    }, Qt::QueuedConnection);

    engine.load(url);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&dockManager]() {
        dockManager.cleanupKWinWindowTracker();
    });

    return app.exec();
}
