/*****************************************************************************
 * messages.hpp : Information about a stream
 ****************************************************************************
 * Copyright (C) 2006-2007 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb (at) videolan.org>
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

#ifndef QVLC_MESSAGES_DIALOG_H_
#define QVLC_MESSAGES_DIALOG_H_ 1

#include "widgets/native/qvlcframe.hpp"

/* Auto-generated from .ui files */
#include "ui_messages_panel.h"

#include "util/message_log_buffer.hpp"
#include <QTimer>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
struct vlc_log_subscription;

class MessagesDialog : public QVLCFrame
{
    Q_OBJECT

public:
    MessagesDialog( qt_intf_t * );
    virtual ~MessagesDialog();

private:
    Ui::messagesPanelWidget ui;
    bool matchFilter( const QString& );

    std::atomic<int> verbosity{-1};
    static void MsgCallback( void *, int, const vlc_log_t *, const char *,
                             va_list );

private slots:
    bool save();
    void updateConfig();
    void changeVerbosity( int );
    void updateOrClear();
    void tabChanged( int );
    void filterMessages();
    void addError( int );
    void errorsCountChanged();
    void drainMessages();
    void updateHistoryInfo();

private:
    void buildTree( QTreeWidgetItem *, vlc_object_t * );
    void updateActionIcon();

    QPushButton *updateButton = nullptr;
    QLabel* m_historyInfo;
    MessageLogBuffer m_pendingMessages;
    QTimer m_logTimer;
    quint64 m_skippedMessages = 0;
    QList<int> m_errorRowLengths;
    vlc_log_subscription *m_logSubscription = nullptr;
#ifndef NDEBUG
    QTreeWidget *pldebugTree;
    void updatePLTree();
    void retranslatePLTree();
#endif

public:
    void showTab( int i );

protected:
    bool eventFilter(QObject*, QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void changeEvent(QEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
};

#endif
