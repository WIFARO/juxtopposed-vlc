/*****************************************************************************
 * Copyright (C) 2023 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
*****************************************************************************/

#include <QtQuickTest>
#include <QQmlContext>
#include <QtMath>
#include "qml_test.hpp"

TestWindowImage::TestWindowImage(QQuickWindow *window)
    : m_image(window->grabWindow()), m_window(window), m_logicalSize(window->size())
{
}

QPoint TestWindowImage::point(QQuickItem *item, qreal x, qreal y) const
{
    if (!item || !m_window || item->window() != m_window || m_logicalSize.isEmpty())
        return {-1, -1};
    const QPointF scene = item->mapToScene(QPointF(x, y));
    // Use actual image dimensions; grabWindow's DPR metadata is not reliable
    // on every backend. Select the physical pixel containing the scene point.
    return {qFloor(scene.x() * m_image.width() / m_logicalSize.width()),
            qFloor(scene.y() * m_image.height() / m_logicalSize.height())};
}

int TestWindowImage::red(int x, int y) const
{
    return m_image.valid(x, y) ? m_image.pixelColor(x, y).red() : -1;
}

int TestWindowImage::green(int x, int y) const
{
    return m_image.valid(x, y) ? m_image.pixelColor(x, y).green() : -1;
}

int TestWindowImage::blue(int x, int y) const
{
    return m_image.valid(x, y) ? m_image.pixelColor(x, y).blue() : -1;
}

QObject *TestImages::grabWindow(QQuickItem *item) const
{
    if (!item || !item->window())
        return nullptr;
    auto *image = new TestWindowImage(item->window());
    QQmlEngine::setObjectOwnership(image, QQmlEngine::JavaScriptOwnership);
    return image;
}

void Setup::qmlEngineAvailable(QQmlEngine *engine)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    engine->addImportPath(":/qt/qml");
#endif
    engine->rootContext()->setContextProperty(QStringLiteral("testImages"), new TestImages(engine));
}

int main(int argc, char **argv)
{
    QTEST_SET_MAIN_SOURCE_PATH

    Q_INIT_RESOURCE( util_assets );
#ifdef QT_USE_QMLCACHEGEN
    Q_INIT_RESOURCE( util_cachegen );
#endif

    //run tests offscreen as the CI doesn't have a desktop environment
    qputenv("QT_QPA_PLATFORM", "offscreen");
    Setup setup;
    return quick_test_main_with_setup(argc, argv, "qml_test", QUICK_TEST_SOURCE_DIR, &setup);
}
