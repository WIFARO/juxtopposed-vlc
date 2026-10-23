// SPDX-License-Identifier: GPL-2.0-or-later
#include <QBuffer>
#include <QGuiApplication>
#include <QImageReader>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickView>
#include <QTest>
#include <QUrlQuery>
#include <algorithm>

// A synchronous test provider with the same inline-SVG/request-size contract.
// The reference path below reads the authored SVG independently.
class SourceSvgProvider : public QQuickImageProvider
{
public:
    SourceSvgProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        const QUrl url{id};
        const QUrlQuery query{url};
        auto svg = query.queryItemValue(QStringLiteral("_res_SVG"), QUrl::FullyDecoded).toUtf8();
        QBuffer buffer(&svg);
        if (!buffer.open(QIODevice::ReadOnly)) return {};
        QImageReader reader(&buffer, "svg");
        if (!requestedSize.isEmpty()) reader.setScaledSize(requestedSize);
        auto image = reader.read();
        if (size) *size = image.size();
        return image;
    }
};

class RedesignSvgRasterTest : public QObject
{
    Q_OBJECT

    static QQuickItem *fileIcon(QQuickItem *item)
    {
        if (item->isVisible() && item->property("name").toString() == QStringLiteral("menu-file")
            && item->property("paths").isValid()) return item;
        for (auto *child : item->childItems())
            if (auto *icon = fileIcon(child)) return icon;
        return nullptr;
    }

private slots:
    void authoredRaster_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<bool>("provider");
        for (const auto &mode : {QStringLiteral("shared"), QStringLiteral("menu"), QStringLiteral("action")})
            for (const bool provider : {false, true}) {
                const auto name = mode + (provider ? QStringLiteral("-provider") : QStringLiteral("-preview"));
                QTest::newRow(qPrintable(name)) << mode << provider;
            }
    }

    void authoredRaster()
    {
        QFETCH(QString, mode);
        QFETCH(bool, provider);
        const QString testDirectory = QStringLiteral(TOP_SRCDIR) + QStringLiteral("/modules/gui/qt/tests/");
        QQuickView view;
        view.setColor(Qt::transparent);
        view.engine()->rootContext()->setContextProperty(QStringLiteral("redesignSvgImages"), provider);
        view.engine()->addImageProvider(QStringLiteral("svgcolor"), new SourceSvgProvider);
        view.setSource(QUrl::fromLocalFile(testDirectory + QStringLiteral("redesign-svg-raster.qml")));
        QVERIFY2(view.status() != QQuickView::Error,
                 qPrintable(view.errors().isEmpty() ? QStringLiteral("Cannot load fixture") : view.errors().first().toString()));
        auto *root = view.rootObject();
        QVERIFY(root);
        root->setProperty("mode", mode);
        view.resize(640, 800);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(QMetaObject::invokeMethod(root, "openForTest"));
        QTRY_VERIFY(root->property("ready").toBool());
        QTRY_VERIFY(fileIcon(view.contentItem()));
        auto *icon = fileIcon(view.contentItem());
        QTRY_COMPARE(icon->property("status").toInt(), 1);

        // grabToImage uses logical target dimensions and applies the window DPR.
        // QuickTest's grabImage crops using local coordinates and can lose DPR
        // metadata on the software backend, so it cannot check this nested item.
        auto grab = icon->grabToImage(icon->size().toSize());
        QVERIFY(grab);
        QTRY_VERIFY(!grab->image().isNull());
        const QImage actual = grab->image();
        QCOMPARE(actual.size(), QSize(qRound(24 * view.devicePixelRatio()), qRound(24 * view.devicePixelRatio())));

        // Read the authored SVG directly at the displayed pixel size. This does
        // not use RedesignSvg's sourceSize calculation or the image provider.
        QImageReader reader(testDirectory + QStringLiteral("reference-menu-file.svg"), "svg");
        reader.setScaledSize(actual.size());
        const QImage expected = reader.read();
        QVERIFY2(!expected.isNull(), qPrintable(reader.errorString()));
        int maximumAlphaError = 0;
        int referenceAlpha = 0;
        for (int y = 0; y < actual.height(); ++y)
            for (int x = 0; x < actual.width(); ++x) {
                referenceAlpha += expected.pixelColor(x, y).alpha();
                maximumAlphaError = std::max(maximumAlphaError,
                    std::abs(actual.pixelColor(x, y).alpha() - expected.pixelColor(x, y).alpha()));
            }
        QVERIFY(referenceAlpha > 10 * actual.width() * actual.height());
        QCOMPARE(maximumAlphaError, 0);
    }
};

int main(int argc, char **argv)
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    if (!qEnvironmentVariableIsSet("QT_QUICK_BACKEND"))
        qputenv("QT_QUICK_BACKEND", "software");
    // Exercise the regression even when the test host uses 100% display scale.
    if (!qEnvironmentVariableIsSet("QT_SCALE_FACTOR"))
        qputenv("QT_SCALE_FACTOR", "1.25");
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    RedesignSvgRasterTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_redesign_svg_raster.moc"
