// SPDX-License-Identifier: GPL-2.0-or-later
#include <QQuickItem>
#include <QTest>
#include <QSignalSpy>
#include <QResizeEvent>

#include "../maininterface/compositor_x11_uisurface.hpp"

class SurfaceSizeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("QT_QUICK_BACKEND", "software");
    }

    void initialLayout_data()
    {
        QTest::addColumn<QSize>("restoredSize");
        QTest::newRow("default") << QSize(960, 540);
        QTest::newRow("large") << QSize(1600, 1000);
        QTest::newRow("portrait") << QSize(720, 1280);
    }

    void initialLayout()
    {
        QFETCH(QSize, restoredSize);
        QWindow mainWindow;
        mainWindow.resize(restoredSize);
        vlc::CompositorX11UISurface surface(&mainWindow);
        surface.create();
        QSignalSpy sizes(&surface, &vlc::CompositorX11UISurface::sizeChanged);
        auto* root = new QQuickItem;

        // No expose or resize event has reached the new QML surface yet.
        surface.setContent(root);
        QCOMPARE(root->size(), QSizeF(restoredSize));
        QCOMPARE(surface.getOffscreenWindow()->size(), restoredSize);
        QVERIFY(!sizes.isEmpty());
        QCOMPARE(sizes.last().at(0).toSize(), restoredSize * surface.devicePixelRatio());
    }

    void softwareResizeReportsPhysicalSize()
    {
        QWindow mainWindow;
        mainWindow.resize(960, 540);
        vlc::CompositorX11UISurface surface(&mainWindow);
        surface.create();
        QSignalSpy sizes(&surface, &vlc::CompositorX11UISurface::sizeChanged);
        auto* root = new QQuickItem;
        surface.setContent(root);
        const QSize next(800, 600);
        mainWindow.resize(next);
        QResizeEvent event(next, QSize(960, 540));
        QCoreApplication::sendEvent(&mainWindow, &event);
        QCOMPARE(root->size(), QSizeF(next));
        QVERIFY(!sizes.isEmpty());
        QCOMPARE(sizes.last().at(0).toSize(), next * surface.devicePixelRatio());
    }
};

QTEST_MAIN(SurfaceSizeTest)
#include "test_x11_surface_size.moc"
