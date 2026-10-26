/*****************************************************************************
 * EPGView.cpp: EPGView
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

#include "qt.hpp"

#include <vlc_epg.h>

#include "EPGView.hpp"
#include "EPGItem.hpp"

#include <QDateTime>
#include <QRectF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <limits>
#include <utility>
#include <QtMath>
#include <QScrollBar>
#include <QSignalBlocker>

EPGGraphicsScene::EPGGraphicsScene( QObject *parent ) : QGraphicsScene( parent )
{}

void EPGGraphicsScene::drawBackground( QPainter *painter, const QRectF &rect)
{
    EPGView *epgView = qobject_cast<EPGView *>(parent());

    if( !epgView->startTime().isValid() )
    {
        QGraphicsScene::drawBackground( painter, rect );
        return;
    }

    /* day change */
    QDateTime rectstarttime = epgView->startTime().addSecs( rect.left() );
    QDateTime nextdaylimit = rectstarttime.date().startOfDay();

    QRectF area( rect );
    while( area.left() < rect.right() )
    {
        nextdaylimit = nextdaylimit.addDays( 1 );
        area.setRight( epgView->startTime().secsTo( nextdaylimit ) );

        if ( epgView->startTime().date().daysTo( nextdaylimit.date() ) % 2 != 0 )
            painter->fillRect( area, epgView->window()->palette().color( QPalette::Base ) );
        else
            painter->fillRect( area, epgView->window()->palette().color( QPalette::AlternateBase ) );

        area.setLeft( area.right() + 1 );
    }

    /* channels lines */
    QPen line(epgView->window()->palette().color(QPalette::Mid));
    line.setCosmetic(true);
    painter->setPen(line);
    const int height = epgView->trackHeight();
    for (int y = (qFloor(rect.top() / height) + 1) * height; y < rect.bottom(); y += height)
       painter->drawLine( QLineF( rect.left(), y, rect.right(), y ) );

    /* current hour line */
    if( epgView->epgTime().isValid() )
    {
        int x = epgView->startTime().secsTo( epgView->epgTime() );
        line.setColor(epgView->accentColor());
        painter->setPen(line);
        painter->drawLine( QLineF( x, rect.top(), x, rect.bottom() ) );
    }
}

EPGView::EPGView( QWidget *parent ) : QGraphicsView( parent )
{
    setContentsMargins( 0, 0, 0, 0 );
    setFrameStyle( QFrame::Box );
    setAlignment( Qt::AlignLeft | Qt::AlignTop );

    m_startTime = QDateTime();
    m_maxTime = m_startTime;

    EPGGraphicsScene *EPGscene = new EPGGraphicsScene( this );

    setScene(EPGscene);
    connect(EPGscene, &QGraphicsScene::selectionChanged, this, [this] {
        if (m_selectedItem && !m_selectedItem->isSelected()) focusItem(nullptr);
    });
}

void EPGView::setScale( double scaleFactor )
{
    const QPointF center = mapToScene(viewport()->rect().center());
    QTransform matrix;
    matrix.scale( scaleFactor, 1 );
    setTransform( matrix );
    centerOn(center);
}

void EPGView::setCompact(bool compact)
{
    if (m_compact == compact) return;
    m_compact = compact;
    for (auto *program : programs) program->updateEventPos();
    walkItems(false);
    if (m_selectedItem) ensureVisible(m_selectedItem, 0, 0);
    viewport()->update();
}

const QDateTime& EPGView::startTime() const
{
    return m_startTime;
}

QDateTime EPGView::epgTime() const
{
    if( m_startTime.isValid() && m_maxTime.isValid() )
        return m_epgTime;
    return QDateTime();
}

void EPGView::setEpgTime(const QDateTime &time)
{
    m_epgTime = time;
}

bool EPGView::hasValidData() const
{
    for (auto *program : programs) if (!program->eventsbyid.isEmpty()) return true;
    return false;
}

bool EPGView::updateEPG( const vlc_epg_t * const *pp_epg, size_t i_epg )
{
    m_updtMinTime = QDateTime();
    for (auto *program : programs) program->clearCurrent();

    for ( size_t i = 0; i < i_epg; ++i )
    {
        const vlc_epg_t *p_epg = pp_epg[i];

        EPGProgram *program;

        QHash<uint16_t, EPGProgram*>::iterator it = programs.find( p_epg->i_source_id );
        if( it != programs.end() )
        {
            program = *it;
        }
        else
        {
            program = new EPGProgram( this, p_epg );
            program->setPosition( programs.count() );
            programs.insert( p_epg->i_source_id, program );
            emit programAdded( program );
        }
        program->setName(qfu(p_epg->psz_name));
        program->updateEvents( p_epg->pp_event, p_epg->i_event, p_epg->p_current, &m_updtMinTime );
    }

    if( !m_startTime.isValid() )
        m_startTime = m_updtMinTime;

    return true;
}

void EPGView::reset()
{
    focusItem(nullptr);
    /* clean our items storage and remove them from the scene */
    qDeleteAll(programs);
    programs.clear();
    m_startTime = m_maxTime = m_epgTime = QDateTime();
    scene()->setSceneRect(QRectF());
    emit rangeChanged(QDateTime(), QDateTime());
}

void EPGView::walkItems( bool b_cleanup )
{
    if (!b_cleanup) m_updtMinTime = m_startTime;
    QDateTime maxTime;
    bool b_rangechanged = false;

    foreach( EPGProgram *program, programs )
    {
        /* remove expired items and clear their current flag */
        if( b_cleanup && m_updtMinTime.isValid() )
            program->pruneEvents( m_updtMinTime );

        if( !program->eventsbytime.isEmpty() )
        {
            const EPGItem *last = program->eventsbytime.last();
            if( !maxTime.isValid() ||
                 last->start().addSecs( last->duration() ) > maxTime )
            {
                maxTime = last->start().addSecs( last->duration() );
            }
        }
    }

    if( m_startTime.isValid() && m_startTime != m_updtMinTime )
        b_rangechanged = m_updtMinTime.isValid();

    if( maxTime.isValid() && m_maxTime != maxTime )
        b_rangechanged |= m_updtMinTime.isValid();

    m_startTime = m_updtMinTime;
    m_maxTime = maxTime;

    scene()->setSceneRect(0, 0, m_startTime.isValid() && m_maxTime.isValid()
                        ? m_startTime.secsTo(m_maxTime) : 0, programs.count() * trackHeight());
    if ( b_rangechanged )
    {
        foreach( EPGProgram *program, programs )
            program->updateEventPos();
        emit rangeChanged( m_startTime, m_maxTime );
    }
}

void EPGView::cleanup()
{
    walkItems( true );
}

EPGView::~EPGView()
{
    // QWidget deletes this child after EPGWidget's derived destructor has run.
    // Do not send selection/range callbacks to that partially destroyed owner.
    const QSignalBlocker blocker(this);
    reset();
}

void EPGView::focusItem( EPGItem *epgItem )
{
    auto *previous = m_selectedItem;
    m_selectedItem = epgItem;
    if (epgItem) epgItem->setSelected(true);
    if (previous && previous != epgItem) previous->setSelected(false);
    emit itemFocused(epgItem);
}

void EPGView::activateProgram( int id )
{
    emit programActivated( id );
}

void EPGView::goToNow()
{
    if (!m_startTime.isValid() || !m_maxTime.isValid()) return;
    const auto now = m_epgTime.isValid() ? m_epgTime : QDateTime::currentDateTime();
    const int scroll = verticalScrollBar()->value();
    centerOn(qBound<qint64>(0, m_startTime.secsTo(now), m_startTime.secsTo(m_maxTime)), 0);
    verticalScrollBar()->setValue(scroll);
}

void EPGView::changeEvent(QEvent *event)
{
    QGraphicsView::changeEvent(event);
    if (event->type() == QEvent::FontChange)
    {
        for (auto *program : programs) program->updateEventPos();
        walkItems(false);
    }
    if (event->type() == QEvent::PaletteChange)
    {
        if (scene()) scene()->update();
    }
}

void EPGView::keyPressEvent(QKeyEvent *event)
{
    if (m_selectedItem && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
    {
        m_selectedItem->activate();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right
        || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)
    {
        const QPointF origin = m_selectedItem ? m_selectedItem->sceneBoundingRect().center() : QPointF();
        EPGItem *best = nullptr;
        auto distance = std::make_pair(std::numeric_limits<qreal>::max(), std::numeric_limits<qreal>::max());
        for (auto *raw : scene()->items())
        {
            auto *item = static_cast<EPGItem *>(raw);
            if (item == m_selectedItem) continue;
            const QPointF delta = item->sceneBoundingRect().center() - origin;
            if (m_selectedItem && ((event->key() == Qt::Key_Left && (delta.x() >= 0 || delta.y() != 0))
                || (event->key() == Qt::Key_Right && (delta.x() <= 0 || delta.y() != 0))
                || (event->key() == Qt::Key_Up && delta.y() >= 0)
                || (event->key() == Qt::Key_Down && delta.y() <= 0))) continue;
            const auto score = std::make_pair(qAbs(delta.y()), qAbs(delta.x()));
            if (score < distance) { best = item; distance = score; }
        }
        if (best)
        {
            best->setFocus(Qt::TabFocusReason);
            focusItem(best);
            const int margin = qBound(0, (viewport()->height() - trackHeight()) / 2, 8);
            ensureVisible(best, 50, margin);
        }
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void EPGView::mousePressEvent(QMouseEvent *event)
{
    const bool empty = !itemAt(event->position().toPoint());
    QGraphicsView::mousePressEvent(event);
    if (empty)
    {
        scene()->clearSelection();
        scene()->setFocusItem(nullptr);
        focusItem(nullptr);
    }
}
