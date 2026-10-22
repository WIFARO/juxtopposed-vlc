// SPDX-License-Identifier: GPL-2.0-or-later
#include <QGuiApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QScreen>
#include <QTest>
#include <QtPlugin>
#include <cmath>
#include <memory>
#include "../plugins.hpp"
#include "util/textureproviderobserver.hpp"
#include "widgets/native/viewblockingrectangle.hpp"
#include "widgets/native/softwarebackdropblur.hpp"

class BackdropContext : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE qreal effectiveDevicePixelRatio(QWindow *window) const
    {
        auto *quick = qobject_cast<QQuickWindow *>(window);
        return quick ? quick->effectiveDevicePixelRatio() : 1;
    }
signals:
    void intfDevicePixelRatioChanged();
};

static QSGRendererInterface::GraphicsApi expectedApi;

class TestBackdropBlur : public QObject
{
    Q_OBJECT
private:
    std::unique_ptr<QQmlEngine> engine;
    std::unique_ptr<QObject> root;
    QQuickWindow *window = nullptr;
    QQuickItem *surface = nullptr;

    QColor pixel(const QImage &image, qreal x, qreal y) const
    {
        return image.pixelColor(int(x * image.width() / window->width()),
                                int(y * image.height() / window->height()));
    }

    QImage blurredImage()
    {
        QImage image;
        const bool ready = QTest::qWaitFor([&] {
            image = window->grabWindow();
            if (image.isNull()) return false;
            const int value = pixel(image, 240, 256).red();
            return value > 8 && value < 128;
        }, 5000);
        if (!ready)
            return {};
        return image;
    }

private slots:
    void initTestCase()
    {
        if (expectedApi == QSGRendererInterface::Null)
            QSKIP("Pixel checks require an explicit renderer; select software, opengl or d3d11.");
    }

    void init()
    {
        engine = std::make_unique<QQmlEngine>();
        engine->rootContext()->setContextProperty("testAlphaWindow",
            QByteArray(QTest::currentTestFunction()).startsWith("transparent"));
        const QString sourceRoot = qEnvironmentVariable("VLC_TEST_SOURCE_ROOT", QStringLiteral(TOP_SRCDIR));
        const auto path = QUrl::fromLocalFile(QDir(sourceRoot).filePath("modules/gui/qt/widgets/qml"));
        QQmlComponent component(engine.get());
        component.setData(QStringLiteral(R"(
import QtQuick
import QtQuick.Window
import BackdropTest 1.0
import "%1" as Widgets
Window {
    visible: true; width: 512; height: 512
    color: shadowTest ? "white" : testAlphaWindow ? "transparent" : "black"
    property bool shadowTest: false
    property int pattern: 0
    property bool edgePatch: false
    property bool clipBlur: false
    Item {
        id: underlay
        visible: !shadowTest
        anchors.fill: parent
        Rectangle { anchors.fill: parent; color: pattern === 4 ? "transparent" : "black" }
        Rectangle {
            x: pattern === 3 ? 0 : pattern === 1 ? 130 : 256
            y: pattern === 3 ? 256 : 0
            width: pattern === 3 ? 512 : pattern === 1 ? 126 : 256
            height: pattern === 3 ? 256 : 512
            color: pattern === 4 ? Qt.rgba(1, 0, 0, 0.6) : "white"; visible: pattern !== 2
        }
        Rectangle { x: 116; y: 200; width: 12; height: 112; color: "white"; visible: edgePatch }
        VideoHole { anchors.fill: parent; visible: pattern === 2 }
    }
    Item {
        width: clipBlur ? 260 : 512; height: 512; clip: clipBlur
        Widgets.BackdropBlur {
            objectName: "surface"
            visible: !shadowTest
            x: 128; y: 128; width: 256; height: 256
            sourceItem: underlay
            radius: 25; cornerRadius: 10
        }
    }
    Item {
        id: shadowCard
        objectName: "shadowCard"
        visible: shadowTest
        x: 128; y: 128; width: 256; height: 200
        Widgets.RoundedRectangleShadow {
            objectName: "referenceMenuShadow"
            anchors.alignWhenCentered: false
            blurRadius: 7.5; yOffset: 8; radius: 10
            color: Qt.rgba(0, 0, 0, 0.46)
            exclusionRect: Qt.rect(blurRadius * compensationFactor,
                                   blurRadius * compensationFactor - yOffset,
                                   shadowCard.width, shadowCard.height)
            exclusionRadius: radius
        }
    }
}
)").arg(path.toString()).toUtf8(), QUrl("qrc:/backdrop-blur-test.qml"));
        root.reset(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        window = qobject_cast<QQuickWindow *>(root.get());
        surface = root->findChild<QQuickItem *>("surface");
        QVERIFY(window && surface);
        QSignalSpy frames(window, &QQuickWindow::frameSwapped);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestUpdate();
        QTRY_VERIFY(!frames.isEmpty());
        QCOMPARE(window->rendererInterface()->graphicsApi(), expectedApi);
        QTRY_VERIFY(surface->property("ready").toBool());
        QVERIFY(!blurredImage().isNull());
    }

    void cleanup()
    {
        root.reset();
        engine.reset();
        window = nullptr;
        surface = nullptr;
    }

    void gaussianRadiusAndCrop()
    {
        const auto image = blurredImage();
        QVERIFY(!image.isNull());
        const qreal ratio = qreal(image.width()) / window->width();
        double weight = 0, first = 0, second = 0;
        const int y = image.height() / 2;
        for (int x = int(130 * ratio); x < int(382 * ratio); ++x)
        {
            const double value = image.pixelColor(x + 1, y).red() - image.pixelColor(x, y).red();
            QVERIFY(value >= -1);
            if (value <= 0) continue;
            const double position = (x + 1.) / ratio;
            weight += value; first += value * position; second += value * position * position;
        }
        QVERIFY(weight > 250);
        const double mean = first / weight;
        const double sigma = std::sqrt(second / weight - mean * mean);
        QVERIFY2(qAbs(mean - 256) < 2, qPrintable(QString::number(mean)));
        QVERIFY2(sigma > 22 && sigma < 28, qPrintable(QString::number(sigma)));
        QCOMPARE(pixel(image, 80, 256).red(), 0);
        QCOMPARE(pixel(image, 420, 256).red(), 255);
    }

    void roundedCorners()
    {
        root->setProperty("pattern", 1);
        QImage image;
        QTRY_VERIFY(([&] {
            image = window->grabWindow();
            return !image.isNull() && pixel(image, 129, 240).red() > 60;
        })());
        QVERIFY(pixel(image, 129, 129).red() < 5);
        QVERIFY(pixel(image, 129, 240).red() > 60);
        surface->setProperty("cornerRadius", 0);
        QTRY_VERIFY(([&] {
            const auto square = window->grabWindow();
            return !square.isNull() && pixel(square, 129, 129).red() > 60;
        })());
    }

    void referenceMenuShadow()
    {
        if (expectedApi == QSGRendererInterface::Software)
            QSKIP("This fixture checks the GPU shadow; software shadows use the image provider.");
        root->setProperty("shadowTest", true);
        auto *shadow = root->findChild<QQuickItem *>("referenceMenuShadow");
        auto *card = root->findChild<QQuickItem *>("shadowCard");
        QVERIFY(shadow && card);
        QCOMPARE(shadow->x(), -22.5);
        QCOMPARE(shadow->y(), -14.5);
        QImage image;
        QTRY_VERIFY(([&] {
            image = window->grabWindow();
            return !image.isNull() && pixel(image, 256, 329).red() < 200;
        })());
        // The exclusion follows the unshifted rounded panel, not its shadow.
        QCOMPARE(pixel(image, 256, 130).red(), 255);
        QCOMPARE(pixel(image, 256, 228).red(), 255);
        QCOMPARE(pixel(image, 134, 134).red(), 255);
        QVERIFY(pixel(image, 130, 130).red() < 254);
        QVERIFY(pixel(image, 256, 370).red() >= 254);
        const qreal ratio = qreal(image.width()) / window->width();
        for (int y : {329, 337, 345, 353})
        {
            // A long straight edge is a Gaussian step: sigma7.5, offset8,
            // opacity0.46, as specified by the menu SVG's drop-shadow filter.
            const double alpha = 0.46 * 0.5 * std::erfc((y + 0.5 / ratio - 336) / (std::sqrt(2.) * 7.5));
            const double expected = 255 * (1 - alpha);
            const int actual = pixel(image, 256, y).red();
            QVERIFY2(qAbs(actual - expected) < 3,
                     qPrintable(QString("y=%1 actual=%2 expected=%3").arg(y).arg(actual).arg(expected)));
        }
        card->setWidth(160);
        QTRY_VERIFY(([&] {
            image = window->grabWindow();
            return !image.isNull() && pixel(image, 289, 228).red() < 225;
        })());
        QCOMPARE(pixel(image, 280, 228).red(), 255);
        // Existing users without an exclusion keep the filled/hollow behavior.
        shadow->setProperty("exclusionRect", QRectF());
        QTRY_VERIFY(([&] {
            image = window->grabWindow();
            return !image.isNull() && pixel(image, 208, 228).red() < 145;
        })());
        shadow->setProperty("hollow", true);
        QTRY_VERIFY(([&] {
            image = window->grabWindow();
            return !image.isNull() && pixel(image, 208, 228).red() == 255;
        })());
        QVERIFY(pixel(image, 208, 340).red() < 250);
    }

    void transparentVideoHole()
    {
        root->setProperty("pattern", 2);
        QTRY_VERIFY(([&] {
            const auto image = window->grabWindow();
            return !image.isNull() && pixel(image, 256, 256).alpha() == 0;
        })());
    }

    void transparentUiEdgesReplaceUnderlay()
    {
        root->setProperty("pattern", 4);
        QImage image;
        QTRY_VERIFY(([&] {
            image = window->grabWindow();
            return !image.isNull() && pixel(image, 257, 256).alpha() < 130
                && pixel(image, 257, 256).red() > 250;
        })());
        const qreal ratio = qreal(image.width()) / window->width();
        for (int x : {240, 257, 272})
        {
            const double expected = 153 * 0.5 * std::erfc(
                (256 - x - 0.5 / ratio) / (std::sqrt(2.) * 25));
            const auto actual = pixel(image, x, 256);
            QVERIFY2(qAbs(actual.alpha() - expected) < 9,
                qPrintable(QString("x=%1 alpha=%2 expected=%3")
                    .arg(x).arg(actual.alpha()).arg(expected)));
            QVERIFY(actual.red() > 250);
            QCOMPARE(actual.green(), 0);
            QCOMPARE(actual.blue(), 0);
        }
        // A replacement must keep the original UI outside its rounded mask.
        QCOMPARE(pixel(image, 382, 129).alpha(), 153);
        QCOMPARE(pixel(image, 382, 129).red(), 255);
        QCOMPARE(pixel(image, 420, 256).alpha(), 153);
        QCOMPARE(pixel(image, 100, 256).alpha(), 0);
    }

    void movementAndIdle()
    {
        QVERIFY(!blurredImage().isNull());
        surface->setX(144);
        QImage image;
        QTRY_VERIFY(([&] {
            image = window->grabWindow();
            return !image.isNull() && qAbs(pixel(image, 256, 256).red() - 128) < 10;
        })());
        root->setProperty("pattern", 3);
        surface->setY(144);
        QTRY_VERIFY(([&] {
            image = window->grabWindow();
            return !image.isNull() && pixel(image, 256, 240).red() > 16
                && pixel(image, 256, 240).red() < 110 && pixel(image, 256, 272).red() > 145;
        })());
        const auto rect = surface->property("captureRect").toRectF();
        QCOMPARE(rect, QRectF(69, 69, 406, 406));
        // Background and occluded windows can complete queued capture work at
        // a reduced rate. Require two complete quiet intervals after settling.
        QSignalSpy frames(window, &QQuickWindow::frameSwapped);
        QStringList samples;
        int quiet = 0;
        for (int i = 0; i < 5 && quiet < 2; ++i)
        {
            const auto before = frames.size();
            QTest::qWait(1000);
            const auto count = frames.size() - before;
            samples.append(QString::number(count));
            quiet = count == 0 ? quiet + 1 : 0;
        }
        QVERIFY2(quiet >= 2, qPrintable(samples.join(", ")));
        root->setProperty("pattern", 0);
        QTRY_VERIFY(([&] {
            const auto updated = window->grabWindow();
            return !updated.isNull() && pixel(updated, 240, 256).red() > 16
                && pixel(updated, 240, 256).red() < 110 && pixel(updated, 272, 256).red() > 145;
        })());
        surface->setVisible(false);
        QTest::qWait(100);
        QCOMPARE(surface->property("ready").toBool(), false);
    }

    void parentClipAndHideRestoreUnderlay()
    {
        root->setProperty("pattern", 1);
        root->setProperty("clipBlur", true);
        QTRY_VERIFY(([&] {
            const auto image = window->grabWindow();
            return !image.isNull() && pixel(image, 240, 256).red() > 150
                && pixel(image, 272, 256).red() == 0;
        })());
        root->setProperty("clipBlur", false);
        QTRY_VERIFY(([&] {
            const auto image = window->grabWindow();
            return !image.isNull() && pixel(image, 272, 256).red() > 40;
        })());
        surface->setVisible(false);
        QTRY_VERIFY(([&] {
            const auto image = window->grabWindow();
            return !image.isNull() && pixel(image, 240, 256).red() == 255
                && pixel(image, 272, 256).red() == 0;
        })());
    }

    void partialUpdatesDoNotAccumulate()
    {
        // Capture the actual presented window, not grabWindow(), which can
        // repaint the whole scene and hide partial-damage dependency failures.
        QTest::qWait(100);
        const auto presented = [this] { return window->screen()->grabWindow(window->winId()).toImage(); };
        const auto original = presented();
        QVERIFY(!original.isNull());
        const int baseline = pixel(original, 136, 256).red();
        for (int i = 0; i < 4; ++i) {
            root->setProperty("edgePatch", true);
            QTRY_VERIFY(([&] {
                const auto image = presented();
                return !image.isNull() && pixel(image, 136, 256).red() > baseline + 15;
            })());
            root->setProperty("edgePatch", false);
            QTRY_VERIFY(([&] {
                const auto image = presented();
                return !image.isNull() && qAbs(pixel(image, 136, 256).red() - baseline) <= 1;
            })());
        }
    }

    void rejectsRecursiveAndOtherWindowSources()
    {
        surface->setProperty("sourceItem", QVariant::fromValue(window->contentItem()));
        QTRY_VERIFY(!surface->property("available").toBool());
        QTRY_VERIFY(!surface->property("ready").toBool());
        QQuickWindow otherWindow;
        surface->setProperty("sourceItem", QVariant::fromValue(otherWindow.contentItem()));
        QVERIFY(!surface->property("available").toBool());
        QVERIFY(!surface->property("ready").toBool());
        surface->setProperty("sourceItem", QVariant::fromValue<QQuickItem *>(nullptr));
    }
};

int main(int argc, char **argv)
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    const auto api = qEnvironmentVariable("VLC_TEST_GRAPHICS_API");
    QQuickWindow::setSceneGraphBackend(api == "software" ? "software" : "rhi");
    expectedApi = api == "software" ? QSGRendererInterface::Software : api == "opengl" ? QSGRendererInterface::OpenGL :
                  api == "d3d11" ? QSGRendererInterface::Direct3D11 : QSGRendererInterface::Null;
    QQuickWindow::setGraphicsApi(expectedApi);
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    qmlRegisterSingletonType<BackdropContext>("VLC.MainInterface", 1, 0, "MainCtx",
        [](QQmlEngine *engine, QJSEngine *) -> QObject * { return new BackdropContext(engine); });
    qmlRegisterType<SoftwareBackdropBlur>("VLC.Widgets", 1, 0, "SoftwareBackdropBlur");
    qmlRegisterType<TextureProviderObserver>("VLC.Util", 1, 0, "TextureProviderObserver");
    qmlRegisterType<ViewBlockingRectangle>("BackdropTest", 1, 0, "VideoHole");
    TestBackdropBlur test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_backdrop_blur.moc"
