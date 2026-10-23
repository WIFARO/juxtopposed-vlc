/*****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifndef QML_TEST_HPP
#define QML_TEST_HPP

#include <QObject>
#include <QQmlEngine>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>

// Keep the presented window's physical pixels. Qt Quick Test's grabImage()
// crops with local item coordinates and can lose the DPR on the software path.
class TestWindowImage : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int width READ width CONSTANT)
    Q_PROPERTY(int height READ height CONSTANT)

public:
    explicit TestWindowImage(QQuickWindow *window);
    int width() const { return m_image.width(); }
    int height() const { return m_image.height(); }
    Q_INVOKABLE QPoint point(QQuickItem *item, qreal x, qreal y) const;
    Q_INVOKABLE int red(int x, int y) const;
    Q_INVOKABLE int green(int x, int y) const;
    Q_INVOKABLE int blue(int x, int y) const;

private:
    QImage m_image;
    QPointer<QQuickWindow> m_window;
    QSize m_logicalSize;
};

class TestImages : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QObject *grabWindow(QQuickItem *item) const;
};

class Setup : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void qmlEngineAvailable(QQmlEngine *engine);
};

#endif /* QML_TEST_HPP */
