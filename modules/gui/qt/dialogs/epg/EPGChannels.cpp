/*****************************************************************************
 * EPGChannels.cpp: EPGChannels
 ****************************************************************************
 * Copyright © 2009-2010 VideoLAN
 *
 * Authors: Adrien Maglo <magsoft@videolan.org>
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

#include "qt.hpp"

#include <vlc_epg.h>

#include "EPGChannels.hpp"
#include "EPGProgram.hpp"
#include "EPGView.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>

EPGChannels::EPGChannels( QWidget *parent, EPGView *m_epgView )
    : QWidget( parent ), m_epgView( m_epgView ), m_offset( 0 )
{
    setContentsMargins(0, 0, 0, 0);
    setMouseTracking(true);
    setFixedWidth(fontMetrics().horizontalAdvance(QStringLiteral("A channel name")) + 24);
}

void EPGChannels::setOffset( int offset )
{
    m_offset = offset;
    update();
}

void EPGChannels::addProgram( const EPGProgram *program )
{
    if ( !programsList.contains( program ) )
    {
        programsList << program;
        std::sort(programsList.begin(), programsList.end(), EPGProgram::lessThan);
        update();
    }
}

void EPGChannels::reset()
{
    programsList.clear();
    update();
}

void EPGChannels::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        setFixedWidth(fontMetrics().horizontalAdvance(QStringLiteral("A channel name")) + 24);
}

void EPGChannels::mouseMoveEvent(QMouseEvent *event)
{
    const int row = (event->position().y() + m_offset) / m_epgView->trackHeight();
    setToolTip(row >= 0 && row < programsList.size()
               ? QStringLiteral("<qt>%1</qt>").arg(programsList[row]->getName().toHtmlEscaped()) : QString());
    QWidget::mouseMoveEvent(event);
}

void EPGChannels::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), window()->palette().color(QPalette::Base));
    for (const auto *program : programsList)
    {
        QString name = program->getName();
        const int suffix = name.lastIndexOf(QStringLiteral(" ["));
        if (suffix > 0) name.truncate(suffix);
        const QRect row(0, int(program->getPosition()) * m_epgView->trackHeight() - m_offset,
                        width(), m_epgView->trackHeight());
        painter.setPen(window()->palette().color(QPalette::Mid));
        painter.drawLine(row.bottomLeft(), row.bottomRight());
        painter.setPen(window()->palette().color(QPalette::Text));
        painter.drawText(row.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(name, Qt::ElideRight, width() - 16));
    }
}
