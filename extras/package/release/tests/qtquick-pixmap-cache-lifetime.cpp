// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtWidgets/QApplication>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickItem>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlComponent>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtCore/QTemporaryDir>
#include <QtGui/QImage>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <thread>

static std::atomic<int> warnings{0};
static void message(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (message.contains(QLatin1String("Timers cannot be stopped")))
        ++warnings;
    std::fprintf(stderr, "%s\n", qFormatLogMessage(type, context, message).toUtf8().constData());
}

static bool runApplication(bool withWindow, bool appOwned)
{
    int argc = 1;
    char name[] = "cache-lifetime";
    char* argv[] = {name, nullptr};
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return false;
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::green);
    const auto imagePath = directory.filePath(QStringLiteral("cached.png"));
    if (!image.save(imagePath)) return false;
    QFile qml(directory.filePath(QStringLiteral("image.qml")));
    if (!qml.open(QFile::WriteOnly)) return false;
    qml.write("import QtQuick\nImage { width: 32; height: 32; source: '" + QUrl::fromLocalFile(imagePath).toEncoded() + "' }\n");
    qml.close();
    if (appOwned) {
        auto* engine = new QQmlEngine(&app);
        QQmlComponent component(engine, QUrl::fromLocalFile(qml.fileName()));
        QObject* image = component.create();
        if (!image || image->property("status").toInt() != 1) return false;
        image->setParent(engine);
        // Start expiry for a different cached image while the application
        // still owns a live image that will be released during child cleanup.
        std::unique_ptr<QObject> releasedImage(component.create());
        if (!releasedImage || !releasedImage->setProperty("sourceSize", QSize(16, 16))
                || releasedImage->property("status").toInt() != 1) return false;
        releasedImage.reset();
        QTimer::singleShot(30, &app, &QCoreApplication::quit);
        return app.exec() == 0;
    }
    if (!withWindow) {
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(qml.fileName()));
        std::unique_ptr<QObject> image(component.create());
        if (!image || image->property("status").toInt() != 1) return false;
        QTimer::singleShot(30, &app, &QCoreApplication::quit);
        return app.exec() == 0;
    }
    {
        QQuickView view;
        view.setSource(QUrl::fromLocalFile(qml.fileName()));
        view.show();
        QTimer::singleShot(30, &app, &QCoreApplication::quit);
        if (app.exec() != 0 || !view.rootObject()) return false;
        if (view.rootObject()->property("status").toInt() != 1) return false;
    }
    return true;
}

// The exit check is registered before the first cache is constructed, so it
// also catches warnings emitted by process-lifetime cache destruction.
static void checkWarningsAtExit()
{
    if (warnings.load() != 0) {
        std::fprintf(stderr, "FAILED: observed %d timer thread warnings\n", warnings.load());
        std::_Exit(2);
    }
}

int main(int argc, char** argv)
{
    qInstallMessageHandler(message);
    std::atexit(checkWarningsAtExit);
    bool success = true;
    const bool differentThreads = argc > 1 && std::strcmp(argv[1], "different-threads") == 0;
    const int iterations = argc > 2 ? std::atoi(argv[2]) : 1;
    const bool withWindow = !(argc > 3 && std::strcmp(argv[3], "no-window") == 0);
    const bool appOwned = argc > 3 && std::strcmp(argv[3], "app-owned") == 0;
    if (differentThreads) {
        for (int i = 0; i < iterations; ++i) {
            std::thread gui([&]{ success = runApplication(withWindow, appOwned) && success; });
            gui.join();
        }
    } else {
        std::thread gui([&]{
            for (int i = 0; i < iterations; ++i)
                success = runApplication(withWindow, appOwned) && success;
        });
        gui.join();
    }
    std::fprintf(stderr, "%s: loaded and closed %d application(s), %d timer thread warnings before exit\n", success ? "PASS" : "FAIL", iterations, warnings.load());
    return success ? 0 : 1;
}
