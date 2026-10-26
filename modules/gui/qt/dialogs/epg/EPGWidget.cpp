/*****************************************************************************
 * EPGWidget.cpp : EPGWidget
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
#include "player/player_controller.hpp"

#include <QStackedWidget>
#include <QGridLayout>
#include <QScrollBar>
#include <QLabel>
#include <QDateTime>
#include <QTimeZone>
#include <QEvent>
#include "util/interface_translator.hpp"

#include "EPGWidget.hpp"
#include "EPGRuler.hpp"
#include "EPGView.hpp"
#include "EPGChannels.hpp"
#include "EPGItem.hpp"

EPGWidget::EPGWidget( QWidget *parent ) : QWidget( parent )
{
    b_input_type_known = false;
    i_event_source_type = ITEM_TYPE_UNKNOWN;
    m_rulerWidget = new EPGRuler( this );
    m_epgView = new EPGView( this );
    m_channelsWidget = new EPGChannels( this, m_epgView );

    m_epgView->setObjectName(QStringLiteral("guideTimeline"));
    m_channelsWidget->setObjectName(QStringLiteral("guideChannels"));
    m_rulerWidget->setObjectName(QStringLiteral("guideRuler"));

    m_epgView->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    setZoom(zoomLevel);

    rootWidget = new QStackedWidget( this );

    QWidget *containerWidget = new QWidget( this );
    QGridLayout* layout = new QGridLayout(containerWidget);
    layout->addWidget( m_rulerWidget, 0, 1 );
    layout->addWidget( m_channelsWidget, 1, 0 );
    layout->addWidget( m_epgView, 1, 1 );
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    containerWidget->setLayout( layout );
    rootWidget->insertWidget( EPGVIEW_WIDGET, containerWidget );

    emptyMessage = new QLabel(this);
    emptyMessage->setObjectName(QStringLiteral("guideEmpty"));
    emptyMessage->setTextFormat(Qt::PlainText);
    emptyMessage->setWordWrap(true);
    emptyMessage->setAlignment(Qt::AlignCenter);
    rootWidget->insertWidget(NOEPG_WIDGET, emptyMessage);
    const auto translate = [this] {
        emptyMessage->setText(qtr("No program guide is available. Open a TV stream that has a program guide."));
        m_epgView->setAccessibleName(qtr("Program timeline"));
        m_epgView->setAccessibleDescription(qtr("Use the arrow keys to select a program. Press Enter to switch channels."));
    };
    translate();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, translate);

    rootWidget->setCurrentIndex( 1 );
    layout = new QGridLayout( this );
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget( rootWidget );
    setLayout( layout );

    connect( m_epgView, &EPGView::rangeChanged, m_rulerWidget, &EPGRuler::setRange );

    connect( m_epgView->horizontalScrollBar(), &QScrollBar::valueChanged,
             m_rulerWidget, &EPGRuler::setOffset );
    connect( m_epgView->verticalScrollBar(), &QScrollBar::valueChanged,
             m_channelsWidget, &EPGChannels::setOffset );
    connect( m_epgView, &EPGView::itemFocused, this, &EPGWidget::itemSelectionChanged );
    connect( m_epgView, &EPGView::programAdded, m_channelsWidget, &EPGChannels::addProgram );
    connect( m_epgView, &EPGView::programActivated, this, &EPGWidget::activateProgram );
}

void EPGWidget::reset()
{
    m_channelsWidget->reset();
    m_epgView->reset();
    currentInput.reset();
    b_input_type_known = false;
    rootWidget->setCurrentIndex(NOEPG_WIDGET);
    emit dataChanged();
}

void EPGWidget::setZoom( int level )
{
    zoomLevel = qBound(1, level, 8);
    const double scale = zoomLevel / 20. * fontMetrics().height() / 17.;
    m_epgView->setScale(scale);
    m_rulerWidget->setScale(scale);
}

void EPGWidget::setCompact(bool compact)
{
    m_epgView->setCompact(compact);
    m_channelsWidget->update();
    updateGeometry();
}

QSize EPGWidget::minimumSizeHint() const
{
    auto size = QWidget::minimumSizeHint();
    // Reserve the scrollbar even when it is hidden, so zooming does not
    // reduce the space available for a complete program row.
    const auto *scrollbar = m_epgView->horizontalScrollBar();
    const int rowHeight = m_epgView->trackHeight() + 2 * m_epgView->frameWidth();
    const int scrollHeight = qMax(scrollbar->minimumHeight(), scrollbar->sizeHint().height());
    size.setHeight(qMax(size.height(), m_rulerWidget->height() + rowHeight
                                      + scrollHeight + 2 * rootWidget->frameWidth()));
    return size;
}

void EPGWidget::updateEPG( input_item_t *p_input_item )
{
    if (!p_input_item) { reset(); return; }

    /* flush our EPG data if input type has changed */
    if (currentInput.get() != p_input_item ||
        (b_input_type_known && p_input_item->i_type != i_event_source_type)) reset();
    currentInput = SharedInputItem(p_input_item);
    i_event_source_type = p_input_item->i_type;
    b_input_type_known = true;

    vlc_mutex_lock(&p_input_item->lock);
    bool anyEvents = false;
    for (int i = 0; i < p_input_item->i_epg; ++i)
        anyEvents |= p_input_item->pp_epg[i]->i_event > 0;
    if (!anyEvents)
    {
        vlc_mutex_unlock(&p_input_item->lock);
        reset();
        return;
    }
    m_epgView->updateEPG( p_input_item->pp_epg, p_input_item->i_epg );
    m_epgView->setEpgTime( ( p_input_item->i_epg_time ) ?
                           QDateTime::fromSecsSinceEpoch( p_input_item->i_epg_time, QTimeZone::systemTimeZone() ) :
                           QDateTime() );
    vlc_mutex_unlock( & p_input_item->lock );

    m_epgView->cleanup();
    /* toggle our widget view */
    rootWidget->setCurrentIndex(
            m_epgView->hasValidData() ? EPGVIEW_WIDGET : NOEPG_WIDGET );

    m_channelsWidget->update();
    m_rulerWidget->setCurrentTime(m_epgView->epgTime());
    m_epgView->viewport()->update();
    emit itemSelectionChanged(m_epgView->selectedItem());
    emit dataChanged();
}

void EPGWidget::activateProgram( int id )
{
    emit programActivated( id );
}

bool EPGWidget::hasValidData() const { return m_epgView->hasValidData(); }
EPGItem *EPGWidget::selectedItem() const { return m_epgView->selectedItem(); }
void EPGWidget::goToNow() { m_epgView->goToNow(); }
void EPGWidget::activateSelectedProgram()
{
    if (auto *item = selectedItem()) item->activate();
}
void EPGWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange) setZoom(zoomLevel);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateGeometry();
}

void EPGWidget::setAccent(const QColor &color)
{
    m_epgView->setAccent(color);
    m_rulerWidget->setAccent(color);
}
