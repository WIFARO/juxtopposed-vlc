/*****************************************************************************
 * info_widgets.cpp : Widgets for info panels
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

#include "qt.hpp"
#include "info_widgets.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QLocale>
#include <algorithm>
#include <cmath>

VLCStatsView::VLCStatsView(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    clock.start();
}

void VLCStatsView::reset()
{
    samples.clear();
    clock.restart();
    update();
}

void VLCStatsView::addValue(double value)
{
    if (!std::isfinite(value) || value < 0) return;
    const double now = clock.elapsed() / 1000.;
    samples.append(QPointF(now, value));
    // Keep the visible minute bounded even if updates arrive more often than
    // the player's normal one-second statistics callback.
    while (samples.size() > 600 || (!samples.isEmpty() && samples.front().x() < now - 60))
        samples.removeFirst();
    update();
}

void VLCStatsView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto metrics = fontMetrics();
    const qreal padding = std::max(4, metrics.height() / 3);
    qreal maximum = 1;
    qreal average = 0;
    for (const auto &sample : samples)
    {
        maximum = std::max(maximum, sample.y());
        average += sample.y() / samples.size();
    }
    const QString top = locale().toString(maximum, 'g', 3);
    const qreal left = std::min(width() * .35, metrics.horizontalAdvance(top) + padding);
    const QRectF plot(left + padding, metrics.height() / 2. + padding,
                      width() - left - 2 * padding, height() - metrics.height() * 1.5 - 2 * padding);
    if (plot.width() <= 0 || plot.height() <= 0) return;
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRectF(0, plot.top() - metrics.height() / 2., left, metrics.height()),
                     Qt::AlignRight | Qt::AlignVCenter, top);
    painter.drawText(QRectF(0, plot.bottom() - metrics.height() / 2., left, metrics.height()),
                     Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0"));
    const double end = samples.isEmpty() ? 0 : samples.back().x();
    const double span = std::clamp(std::ceil(end / 10) * 10, 10., 60.);
    painter.drawText(QRectF(plot.left(), plot.bottom() + padding, plot.width(), metrics.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("−%1 s").arg(int(span)));
    painter.drawText(QRectF(plot.left(), plot.bottom() + padding, plot.width(), metrics.height()),
                     Qt::AlignRight | Qt::AlignVCenter, qtr("Now"));
    QPen grid(palette().color(QPalette::Mid));
    grid.setStyle(Qt::DotLine);
    painter.setPen(grid);
    for (int i = 0; i <= 2; ++i)
    {
        const qreal y = plot.top() + plot.height() * i / 2;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    if (samples.isEmpty()) return;
    const auto position = [&](const QPointF &sample) {
        return QPointF(plot.left() + (sample.x() - end + span) / span * plot.width(),
                       plot.bottom() - sample.y() / maximum * plot.height());
    };
    QPainterPath line;
    line.moveTo(position(samples.front()));
    for (qsizetype i = 1; i < samples.size(); ++i) line.lineTo(position(samples[i]));
    QPainterPath area = line;
    area.lineTo(plot.right(), plot.bottom());
    area.lineTo(position(samples.front()).x(), plot.bottom());
    area.closeSubpath();
    painter.setClipRect(plot.adjusted(-1, -1, 1, 1));
    QColor fill = palette().color(QPalette::Highlight);
    fill.setAlpha(48);
    painter.fillPath(area, fill);
    QPen curve(palette().color(QPalette::Highlight));
    curve.setWidthF(1.5);
    painter.setPen(curve);
    painter.drawPath(line);
    painter.setBrush(palette().color(QPalette::Highlight));
    painter.drawEllipse(position(samples.back()), 2, 2);
    QPen mean(palette().color(QPalette::Text));
    mean.setStyle(Qt::DashLine);
    painter.setPen(mean);
    const qreal y = plot.bottom() - average / maximum * plot.height();
    painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
}
