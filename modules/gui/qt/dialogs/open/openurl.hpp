/*****************************************************************************
 * openurl.hpp: Open a MRL or clipboard content
 ****************************************************************************
 * Copyright © 2009 the VideoLAN team
 *
 * Authors: Jean-Philippe André <jpeg@videolan.org>
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
 ******************************************************************************/

#ifndef QVLC_OPEN_URL_H_
#define QVLC_OPEN_URL_H_ 1

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include "widgets/native/qvlcframe.hpp"
#include <QPointer>

class MainCtx;
class QLineEdit;
class QLabel;
class QPushButton;
class QBoxLayout;
class QScrollArea;

class OpenUrlDialog : public QVLCDialog
{
    Q_OBJECT

public:
    OpenUrlDialog(qt_intf_t *, bool clipboard = true);
    ~OpenUrlDialog() override = default;
    QString url() const;
    bool shouldEnqueue() const;
    void accept() override;

protected:
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

private:
    void enqueue();
    void play();
    void submit(bool enqueue);
    void retranslate();
    void updateMetrics();
    void revealInput();
    QString inputError() const;
    int dp(int value) const;
    QSize availableSize() const;

    QPointer<MainCtx> m_ctx;
    QString lastUrl;
    bool bClipboard;
    bool bShouldEnqueue = false;
    bool initialized = false;
    QLineEdit *edit = nullptr;
    QLabel *heading = nullptr;
    QLabel *info = nullptr;
    QLabel *feedback = nullptr;
    QPushButton *playButton = nullptr;
    QPushButton *queueButton = nullptr;
    QPushButton *cancelButton = nullptr;
    QScrollArea *scroll = nullptr;
    QBoxLayout *actions = nullptr;
};

#endif
