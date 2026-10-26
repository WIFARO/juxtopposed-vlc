/*****************************************************************************
 * open_panels.hpp : Panels for the open dialogs
 ****************************************************************************
 * Copyright (C) 2006-2009 the VideoLAN team
 * Copyright (C) 2007 Société des arts technologiques
 * Copyright (C) 2007 Savoir-faire Linux
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *          Pierre-Luc Beaudoin <pierre-luc.beaudoin@savoirfairelinux.com>
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

#ifndef VLC_QT_OPEN_PANELS_HPP_
#define VLC_QT_OPEN_PANELS_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "dialogs/preferences/preferences_widgets.hpp"

/* Auto-generated from .ui files */
#include "ui_open_file.h"
#include "ui_open_disk.h"
#include "ui_open_net.h"
#include "ui_open_capture.h"

#include <QFileDialog>
#include <QHash>
#include <QVariantMap>

#include <limits.h>

#define setSpinBoxFreq( spinbox ){ spinbox->setRange ( 0, INT_MAX ); \
    spinbox->setAccelerated( true ); }

enum
{
    V4L2_DEVICE,
    PVR_DEVICE,
    DTV_DEVICE,
    DSHOW_DEVICE,
    SCREEN_DEVICE,
    JACK_DEVICE
};

class QWidget;
class QLineEdit;
class QString;
class QEvent;
class QResizeEvent;
class EmbeddedFileBrowser;

class OpenPanel: public QWidget
{
    Q_OBJECT
public:
    OpenPanel( QWidget *p, qt_intf_t *_p_intf ) : QWidget( p )
    {
        p_intf = _p_intf;
        context = CONTEXT_INTERACTIVE;
    }
    virtual ~OpenPanel() {}
    virtual void clear() = 0;
    virtual void onFocus() {}
    virtual void onAccept() {}
    virtual QString validationError() const { return {}; }
    virtual QString validationFeedback() const { return {}; }
    virtual bool validateSelection() { return true; }

    static const int CONTEXT_INTERACTIVE = 0;
    static const int CONTEXT_BATCH = 1;

    virtual void updateContext(int c) { context = c; }

protected:
    qt_intf_t *p_intf;
    int context;

public slots:
    virtual void updateMRL() = 0;
signals:
    void mrlUpdated( const QStringList&, const QString& );
    void methodChanged( const QString& method );
    void acceptRequested();
    void cancelRequested();
};


class FileOpenPanel: public OpenPanel
{
    Q_OBJECT
public:
    FileOpenPanel( QWidget *, qt_intf_t * );
    virtual ~FileOpenPanel();
    void clear() override;
    void onFocus() override;
    void onAccept() override;
    bool validateSelection() override;
    QString validationFeedback() const override;
protected:
    void dropEvent( QDropEvent *) override;
    void dragEnterEvent( QDragEnterEvent * ) override;
    void dragMoveEvent( QDragMoveEvent * ) override;
    void dragLeaveEvent( QDragLeaveEvent * ) override;
private:
    Ui::OpenFile ui;
    QList<QUrl> urlList;
    QUrl subUrl; //url for subtitle
    EmbeddedFileBrowser *dialogBox;
    bool embedded = false;
    void createEmbeddedBrowser();
public slots:
    void updateMRL() override;
private slots:
    void browseFileSub();
    void browseFile();
    void removeFile();
    void updateButtons();
};

class NetOpenPanel: public OpenPanel
{
    Q_OBJECT
public:
    NetOpenPanel( QWidget *, qt_intf_t * );
    ~NetOpenPanel() override = default;
    void clear()  override;
    void onFocus() override;
    void onAccept() override;
private:
    Ui::OpenNetwork ui;
    struct EditorState {
        QString text;
        int cursor = 0;
        int start = -1;
        int length = 0;
    };
    EditorState m_lastFocusedState;
    EditorState editorState() const;
    void restoreSelection(const EditorState &);
    bool m_layoutPending = false;
    bool historyEnabled() const;
    QStringList savedHistory() const;
    void refreshHistory();
    void retranslate();
    void scheduleLayoutUpdate();
protected:
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
public slots:
    void updateMRL() override;
    void clearHistory();
};

class DiscOpenPanel: public OpenPanel
{
    Q_OBJECT
    enum    DiscType
    {
        None,
        Dvd,
        Vcd,
        Cdda,
        BRD,
        Dvda
    };
public:
    DiscOpenPanel( QWidget *, qt_intf_t * );
    virtual ~DiscOpenPanel();
    void clear() override;
    virtual void accept();
    QString validationError() const override;
#if defined( _WIN32 ) || defined( __OS2__ )
    void onFocus() override;
#endif
    void updateContext(int) override;
protected:
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
private:
    Ui::OpenDisk ui;
    char *psz_dvddiscpath, *psz_vcddiscpath, *psz_cddadiscpath;
    DiscType m_discType;
#ifdef _WIN32
    bool m_deviceListInitialized = false;
#endif
    bool defaultSkipDiscMenus() const;
public slots:
    void updateMRL() override;
private slots:
    void updateEditorText();
    void updateLayoutMetrics();
    void browseDevice();
    void updateButtons() ;
    void eject();
};


class CaptureOpenPanel: public OpenPanel
{
    Q_OBJECT
public:
    CaptureOpenPanel( QWidget *, qt_intf_t * );
    virtual ~CaptureOpenPanel();
    void clear() override;
    QString validationError() const override;
protected:
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
private:
    Ui::OpenCapture ui;
    bool isInitialized;

    QHash<int, QString> advancedMRLs;
    QHash<int, QVariantMap> advancedValues;
    QStringList configList;
    QDialog *adv;
#ifdef _WIN32
    StringListConfigControl *vdevDshowW, *adevDshowW;
    QLineEdit *dshowVSizeLine;
#else
    QSpinBox  *pvrFreq;
    QComboBox *v4l2VideoDevice, *v4l2AudioDevice;
    QComboBox *pvrDevice, *pvrAudioDevice;
    QComboBox *v4l2StdBox, *pvrNormBox;
    QSpinBox *jackChannels;
    QCheckBox *jackPace, *jackConnect;
    QLineEdit *jackPortsSelected;
#endif
    QRadioButton *dvbc, *dvbs, *dvbs2, *dvbt, *dvbt2, *atsc, *cqam;
    QLabel *dvbBandLabel, *dvbSrateLabel, *dvbModLabel;
    QComboBox *dvbQamBox, *dvbPskBox, *dvbBandBox;
    QSpinBox *dvbCard, *dvbFreq, *dvbSrate;
    QDoubleSpinBox *screenFPS;

public slots:
    void updateMRL() override;
    void initialize();
private slots:
    void updateButtons();
    void updateLayoutMetrics();
    void enableAdvancedDialog( int );
    void advancedDialog();
};

#endif
