/*****************************************************************************
 * info_widgets.hpp : Widgets for info panels
 ****************************************************************************
 * Copyright (C) 2013 the VideoLAN team
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
#ifndef INFO_WIDGETS_HPP
#define INFO_WIDGETS_HPP

#include <QWidget>
#include <QElapsedTimer>
#include <QPointF>
#include <QVector>

class VLCStatsView: public QWidget
{
    Q_OBJECT
public:
    VLCStatsView(QWidget *);
    void addValue(double);
    void reset();
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QElapsedTimer clock;
    QVector<QPointF> samples;
};

#endif // INFO_WIDGETS_HPP
