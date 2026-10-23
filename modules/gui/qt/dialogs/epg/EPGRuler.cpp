/*****************************************************************************
 * EPGRuler.cpp: EPGRuler
 ****************************************************************************
 * Copyright © 2009-2010 VideoLAN
 *
 * Authors: Ludovic Fauvet <etix@l0cal.com>
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
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "EPGRuler.hpp"

class QPaintEvent;
#include <QPainter>
#include <QDateTime>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QPalette>
#include <QColor>
#include <QEvent>
#include <QLocale>

EPGRuler::EPGRuler( QWidget* parent )
    : QWidget( parent )
{
    setContentsMargins( 0, 0, 0, 0 );
    setFixedHeight(fontMetrics().height() * 2 + 8);
    m_offset = 0;
    m_scale = 1.0;
}

void EPGRuler::setScale( double scale )
{
    m_scale = scale;
    update();
}

void EPGRuler::setRange( const QDateTime& startTime, const QDateTime& endTime )
{
    if( !startTime.isValid() || !endTime.isValid() )
    {
        m_startTime = QDateTime();
    }
    else
    {
        m_startTime = startTime;
        m_duration = startTime.secsTo( endTime );
    }
    update();
}

void EPGRuler::setOffset( int offset )
{
    m_offset = offset;
    update();
}

void EPGRuler::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange) setFixedHeight(fontMetrics().height() * 2 + 8);
}

void EPGRuler::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), window()->palette().color(QPalette::AlternateBase));
    if (!m_startTime.isValid() || m_scale <= 0) return;
    const auto left = m_startTime.addSecs(m_offset / m_scale);
    auto hour = left;
    hour.setTime(QTime(left.time().hour(), 0));
    const QLocale locale;
    for (qreal x = left.secsTo(hour) * m_scale; x < width(); x += 3600 * m_scale, hour = hour.addSecs(3600))
    {
        painter.setPen(window()->palette().color(QPalette::Mid));
        painter.drawLine(QPointF(x, 0), QPointF(x, height()));
        QString label = locale.toString(hour.time(), QLocale::ShortFormat);
        if (hour.time().hour() == 0) label += QStringLiteral("  ") + locale.toString(hour.date(), QLocale::ShortFormat);
        painter.setPen(window()->palette().color(QPalette::Text));
        const QRectF cell(x + 8, 0, 3600 * m_scale - 16, height());
        painter.drawText(cell, Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(label, Qt::ElideRight, qMax(0, int(cell.width()))));
    }
    const auto now = m_currentTime.isValid() ? m_currentTime : QDateTime::currentDateTime();
    const qreal x = m_startTime.secsTo(now) * m_scale - m_offset;
    painter.setPen(m_accent.isValid() ? m_accent : window()->palette().color(QPalette::Highlight));
    painter.drawLine(QPointF(x, 0), QPointF(x, height()));
}
