/*****************************************************************************
 * EPGView.hpp : EPGView
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

#ifndef EPGVIEW_H
#define EPGVIEW_H

#include "qt.hpp"

#include "EPGProgram.hpp"

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QHash>
#include <QDateTime>

class EPGItem;


class EPGGraphicsScene : public QGraphicsScene
{
Q_OBJECT
public:
    explicit EPGGraphicsScene( QObject *parent = 0 );
protected:
    void drawBackground ( QPainter *, const QRectF &);
};

class EPGView : public QGraphicsView
{
Q_OBJECT

public:
    explicit EPGView( QWidget *parent = 0 );
    virtual ~EPGView();

    void            setScale( double scaleFactor );

    const QDateTime& startTime() const;
    QDateTime       epgTime() const;
    void            setEpgTime(const QDateTime&);

    bool            updateEPG( const vlc_epg_t * const *, size_t );
    void            reset();
    void            cleanup();
    bool            hasValidData() const;
    void            activateProgram( int );
    int             trackHeight() const { return fontMetrics().height() * 4 + 12; }
    EPGItem *       selectedItem() const { return m_selectedItem; }
    void            goToNow();
    QColor          accentColor() const { return m_accent.isValid() ? m_accent : palette().color(QPalette::Highlight); }
    void            setAccent(const QColor &color) { m_accent = color; scene()->update(); }

signals:
    void            rangeChanged( const QDateTime&, const QDateTime& );
    void            itemFocused( EPGItem * );
    void            programAdded( const EPGProgram * );
    void            programActivated( int );

protected:
    void            changeEvent(QEvent *) override;
    void            keyPressEvent(QKeyEvent *) override;
    void            mousePressEvent(QMouseEvent *) override;
    void            walkItems( bool );
    QDateTime       m_epgTime;
    QDateTime       m_startTime;
    QDateTime       m_maxTime;
    QDateTime       m_updtMinTime; /* >= startTime before pruning */

public slots:
    void            focusItem( EPGItem * );

private:
    QColor m_accent;
    EPGItem *m_selectedItem = nullptr;
    QHash<uint16_t, EPGProgram*> programs;
};

#endif // EPGVIEW_H
