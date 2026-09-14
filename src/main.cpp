#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QDebug>
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
