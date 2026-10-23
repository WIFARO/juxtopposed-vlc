/*****************************************************************************
 * podcast_configuration.hpp: Podcast configuration dialog
 ****************************************************************************
 * Copyright (C) 2007 the VideoLAN team
 *
 * Authors: Antoine Cellerier <dionoea at videolan dot org>
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

#ifndef QVLC_PODCAST_CONFIGURATION_DIALOG_H_
#define QVLC_PODCAST_CONFIGURATION_DIALOG_H_ 1

#include "widgets/native/qvlcframe.hpp"
#include <QStringList>

/* Auto-generated from .ui files */
#include "ui_podcast_configuration.h"

class PodcastConfigDialog : public QVLCDialog
{
    Q_OBJECT

public:
    PodcastConfigDialog( qt_intf_t * );
    virtual ~PodcastConfigDialog();

public slots:
    void accept() override;
    void add();
    void remove();

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject *, QEvent *) override;
    bool event(QEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    void updateState();
    void retranslate();
    void moveSelected(int direction);
    bool addPendingFeed();
    bool isDirty() const;
    QStringList urls() const;
    void showMessage(const QString &);
    Ui::PodcastConfiguration ui;
    QPushButton *saveButton;
    QPushButton *cancelButton;
    QLabel *emptyMessage;
    QStringList originalUrls;
    bool loading = false;
};

#endif
