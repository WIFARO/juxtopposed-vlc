/*****************************************************************************
 * epg.hpp : EPG Viewer dialog
 ****************************************************************************
 * Copyright © 2010 VideoLAN and AUTHORS
 *
 * Authors:    Jean-Baptiste Kempf <jb@videolan.org>
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

#ifndef QVLC_EPG_DIALOG_H_
#define QVLC_EPG_DIALOG_H_ 1

#include "widgets/native/qvlcframe.hpp"

class QLabel;
class QTextEdit;
class QTimer;
class EPGItem;
class EPGWidget;
class QPushButton;

class EpgDialog : public QVLCFrame
{
    Q_OBJECT
public:

    EpgDialog( qt_intf_t * );
    virtual ~EpgDialog();

protected:
    void showEvent(QShowEvent * event) override;
    void hideEvent(QHideEvent * event) override;

    EPGWidget *epg;
    QTextEdit *description;
    QLabel *title;
    QLabel *time;
    QLabel *heading;
    QLabel *help;
    QLabel *zoomLabel;
    QPushButton *nowButton;
    QPushButton *zoomOut;
    QPushButton *zoomIn;
    QPushButton *switchButton;
    QPushButton *closeButton;
    int zoomLevel = 2;
    EPGItem *displayedItem = nullptr;
    QTimer *timer;

private slots:
    void scheduleUpdate();
    void inputChanged();
    void updateInfos();
    void retranslate();
    void displayEvent( EPGItem * );
};

#endif

