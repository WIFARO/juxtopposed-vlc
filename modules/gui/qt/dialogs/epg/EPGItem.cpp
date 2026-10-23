/*****************************************************************************
 * EPGItem.cpp: EPGItem
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

#include <QTransform>
#include <QFont>
#include <QFontMetrics>
#include <QDateTime>
#include <QFocusEvent>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsSceneHoverEvent>
#include <QStyle>
#include <QTimeZone>
#include <QLocale>
#include <QtMath>

#include "EPGItem.hpp"
#include "EPGView.hpp"

#include "qt.hpp"

EPGItem::EPGItem( const vlc_epg_event_t *data, EPGView *view, EPGProgram *prog )
    : QGraphicsItem(), m_duration(0), m_rating(0)
{
    m_view = view;
    program = prog;
    m_id = data->i_id;
    setData( data );
    m_boundingRect.setHeight(view->trackHeight());
    setFlags( QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    setAcceptHoverEvents( true );
}

QRectF EPGItem::boundingRect() const
{
    return m_boundingRect;
}

void EPGItem::paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget*)
{
    painter->save();
    painter->setWorldMatrixEnabled(false);
    painter->setRenderHint(QPainter::Antialiasing);
    QRectF mapped = deviceTransform(m_view->viewportTransform()).mapRect(boundingRect());
    const qreal scale = m_view->fontMetrics().height() / 17.;
    mapped.adjust(scale, 3 * scale, -scale, -3 * scale);
    if (mapped.width() <= 2) { painter->restore(); return; }
    const auto colors = m_view->window()->palette();
    const QColor accent = m_view->accentColor();
    const bool selected = isSelected() || hasFocus();
    QColor fill = colors.color(QPalette::AlternateBase);
    if (selected) { fill = accent; fill.setAlpha(55); }
    painter->setBrush(fill);
    painter->setPen(QPen(selected || (option->state & QStyle::State_MouseOver) ? accent : colors.color(QPalette::Mid), scale));
    painter->drawRoundedRect(mapped, 6 * scale, 6 * scale);
    if (program->getCurrent() == this)
        painter->fillRect(QRectF(mapped.left() + 2 * scale, mapped.top() + 7 * scale, 3 * scale, mapped.height() - 14 * scale), accent);
    mapped.adjust(9 * scale, 7 * scale, -8 * scale, -7 * scale);
    painter->setFont(m_view->font());
    const auto metrics = painter->fontMetrics();
    painter->setPen(colors.color(QPalette::Text));
    painter->drawText(mapped, Qt::AlignTop | Qt::AlignLeft, metrics.elidedText(m_name, Qt::ElideRight, qMax(0, qFloor(mapped.width()))));
    mapped.adjust(0, metrics.height() + 4 * scale, 0, 0);
    painter->setPen(colors.color(QPalette::WindowText));
    const QLocale locale;
    const auto hours = locale.toString(start().time(), QLocale::ShortFormat) + QStringLiteral(" – ")
                     + locale.toString(end().time(), QLocale::ShortFormat);
    painter->drawText(mapped, Qt::AlignTop | Qt::AlignLeft, metrics.elidedText(hours, Qt::ElideRight, qMax(0, qFloor(mapped.width()))));
    if (m_rating && mapped.width() > 45 * scale)
        painter->drawText(mapped, Qt::AlignBottom | Qt::AlignRight, QStringLiteral("%1+").arg(m_rating));
    painter->restore();
}

const QDateTime& EPGItem::start() const
{
    return m_start;
}

QDateTime EPGItem::end() const
{
    return QDateTime( m_start ).addSecs( m_duration );
}

uint32_t EPGItem::duration() const
{
    return m_duration;
}

uint16_t EPGItem::eventID() const
{
    return m_id;
}

bool EPGItem::setData( const vlc_epg_event_t *data )
{
    QDateTime newtime = QDateTime::fromSecsSinceEpoch( data->i_start, QTimeZone::systemTimeZone() );
    QString newname = qfu( data->psz_name );
    QString newdesc = qfu( data->psz_description );
    QString newshortdesc = qfu( data->psz_short_description );

    QList<QPair<QString, QString>> newItems;
    for (int i = 0; i < data->i_description_items; ++i)
        newItems.append({qfu(data->description_items[i].psz_key), qfu(data->description_items[i].psz_value)});
    if (m_rating != data->i_rating || m_descitems != newItems || m_start != newtime ||
         m_name != newname ||
         m_description != newdesc ||
         m_shortDescription != newshortdesc ||
         m_duration != data->i_duration )
    {
        m_start = newtime;
        m_name = newname;
        setToolTip(QStringLiteral("<qt>%1</qt>").arg(newname.toHtmlEscaped()));
        m_description = std::move(newdesc);
        m_shortDescription = std::move(newshortdesc);
        setDuration( data->i_duration );
        setRating( data->i_rating );
        m_descitems = std::move(newItems);
        updatePos();
        update();
        return true;
    }
    return false;
}

bool EPGItem::endsBefore( const QDateTime &ref ) const
{
    return m_start.addSecs( m_duration ) <= ref;
}

bool EPGItem::playsAt( const QDateTime & ref ) const
{
    return (m_start <= ref) && !endsBefore( ref );
}

const QList<QPair<QString, QString>> & EPGItem::descriptionItems() const
{
    return m_descitems;
}

void EPGItem::setDuration( uint32_t duration )
{
    prepareGeometryChange();
    m_duration = duration;
    m_boundingRect.setWidth( duration );
}

void EPGItem::setRating( uint8_t i_rating )
{
    m_rating = i_rating;
}

QString EPGItem::description() const
{
    if( m_description.isEmpty() )
        return m_shortDescription;

    QString text( m_description );
    if( !m_shortDescription.isEmpty() )
        text += QString(" - ") += m_shortDescription;
    return text;
}

void EPGItem::updatePos()
{
    if (m_boundingRect.height() != m_view->trackHeight())
    {
        prepareGeometryChange();
        m_boundingRect.setHeight(m_view->trackHeight());
    }
    QDateTime overallmin = m_view->startTime();
    if( overallmin.isValid() )
    {
        int x = m_view->startTime().secsTo( m_start );
        setPos( x, program->getPosition() * m_view->trackHeight() );
    }
}

void EPGItem::hoverEnterEvent ( QGraphicsSceneHoverEvent * event )
{
    event->accept();
    scene()->update();
}

void EPGItem::hoverLeaveEvent ( QGraphicsSceneHoverEvent * event )
{ /* required to redraw our background without flaws */
    hoverEnterEvent( event );
}

void EPGItem::mouseDoubleClickEvent( QGraphicsSceneMouseEvent * )
{
    activate();
}

void EPGItem::focusInEvent( QFocusEvent * event )
{
    event->accept();
    m_view->focusItem( this );
    update();
}

void EPGItem::activate() { program->activate(); }
