/*****************************************************************************
 * help.hpp : Help and About dialogs
 ****************************************************************************
 * Copyright (C) 2007 the VideoLAN team
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

#ifndef QVLC_HELP_DIALOG_H_
#define QVLC_HELP_DIALOG_H_ 1

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include "widgets/native/qvlcframe.hpp"
#include "util/singleton.hpp"

/* Auto-generated from .ui files */
#include "ui_about.h"
#include "ui_update.h"
#include <QTextCursor>
#include <QPointer>

class QEvent;
class QTextBrowser;
class QLineEdit;
class QLabel;
class QPushButton;
class QTimer;

class HelpDialog : public QVLCFrame
{
    Q_OBJECT
public:
    HelpDialog( qt_intf_t * );
    virtual ~HelpDialog();

public slots:
    void close() override { hide(); }

protected:
    bool eventFilter(QObject*, QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    void updateSearch();
    void showMatch(int index);
    QTextBrowser *m_browser;
    QLineEdit *m_find;
    QLabel *m_findInfo;
    QPushButton *m_previous;
    QPushButton *m_next;
    QList<QTextCursor> m_matches;
    int m_currentMatch = -1;
};

class AboutDialog : public QVLCDialog
{
    Q_OBJECT
public:
    AboutDialog( qt_intf_t * );
    ~AboutDialog() override;

protected:
    bool eventFilter(QObject*, QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    Ui::aboutWidget ui;
};

#if defined(UPDATE_CHECK)

class UpdateModelPrivate;
class UpdateModel : public QObject
{
    Q_OBJECT

public:
    enum Status {
        Unchecked,
        Checking,
        UpToDate,
        NeedUpdate,
        CheckFailed,
        Downloading // TODO
    };
    Q_ENUM(Status)

    Q_PROPERTY(Status updateStatus READ updateStatus RESET resetStatus NOTIFY updateStatusChanged FINAL)
    Q_PROPERTY(int major READ getMajor NOTIFY updateStatusChanged FINAL)
    Q_PROPERTY(int minor READ getMinor NOTIFY updateStatusChanged FINAL)
    Q_PROPERTY(int revision READ getRevision NOTIFY updateStatusChanged FINAL)
    Q_PROPERTY(int extra READ getExtra NOTIFY updateStatusChanged FINAL)
    Q_PROPERTY(QString version READ getVersion NOTIFY updateStatusChanged FINAL)
    Q_PROPERTY(QString assetUrl READ getAssetUrl NOTIFY updateStatusChanged FINAL)
    Q_PROPERTY(QString description READ getDescription NOTIFY updateStatusChanged FINAL)
    Q_PROPERTY(QString url READ getUrl NOTIFY updateStatusChanged FINAL)
    Q_PROPERTY(double progress READ getProgress NOTIFY progressChanged FINAL) // TODO
    Q_PROPERTY(bool explicitCheck READ explicitCheck WRITE _setExplicitCheck RESET resetExplicitCheck NOTIFY explicitCheckChanged FINAL)

public:
    explicit UpdateModel(qt_intf_t * p_intf);
    ~UpdateModel();

    Q_INVOKABLE void checkUpdate(bool explicitCheck = false);

    Q_INVOKABLE bool openReleasePage();
    Q_INVOKABLE bool download(QString destDir);
    Q_INVOKABLE bool download();

    Q_INVOKABLE void resetStatus();

    Status updateStatus() const;
    int getMajor() const;
    int getMinor() const;
    int getRevision() const;
    int getExtra() const;
    QString getVersion() const;
    QString getAssetUrl() const;
    QString getDescription() const;
    QString getUrl() const;
    double getProgress() const;
    bool explicitCheck() const;
    void resetExplicitCheck();
    bool presentationDismissed() const;
    Q_INVOKABLE void dismissPresentation();

signals:
    void updateStatusChanged();
    void progressChanged();
    void explicitCheckChanged();
    void presentationRequested();

private:
    void _setExplicitCheck(bool) const
    {
        // This method is added as a workaround for QML engine bug
        // where the property can't be reset, if it does not have
        // WRITE method. Do not use it.
        assert(false);
    }

    Q_DECLARE_PRIVATE(UpdateModel)
    QScopedPointer<UpdateModelPrivate> d_ptr;
};

class UpdateDialog : public QVLCFrame
{
    Q_OBJECT
public:
    UpdateDialog( qt_intf_t * );
    virtual ~UpdateDialog();
    void present(bool forceActivation = false);

    bool event(QEvent *event) override;

protected:
    bool eventFilter(QObject*, QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    Ui::updateWidget ui;
    QPointer<UpdateModel> m_model;
    QTimer *m_copyTimer = nullptr;
    enum Feedback { None, LinkCopied, BrowserFailed } m_feedback = None;
    bool m_ready = false;
    bool m_layoutUpdating = false;
    void updateMetrics();
    void updateActions();

private slots:
    void openRelease();
    void updateUI();
    void close() override { hide(); }
};
#endif

#endif
