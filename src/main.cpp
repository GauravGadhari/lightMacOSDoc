#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QIcon>
#include <QDebug>
#include "dock_manager.h"

int main(int argc, char *argv[]) {
    // Force X11/XCB backend so the dock window can position itself freely at the screen bottom
    // and use hardware input shape masks (setMask) to allow 100% click-through outside the dock pill.
    qputenv("QT_QPA_PLATFORM", "xcb");

    QGuiApplication app(argc, argv);
    app.setApplicationName("macOS Dock");
    app.setOrganizationName("Antigravity");
    app.setWindowIcon(QIcon(":/icons/finder/256.png"));

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

    return app.exec();
}
