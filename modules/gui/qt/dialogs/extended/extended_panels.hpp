/*****************************************************************************
 * extended_panels.hpp : Extended Panels
 ****************************************************************************
 * Copyright (C) 2006 the VideoLAN team
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Antoine Cellerier <dionoea at videolan dot org>
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

#ifndef VLC_QT_EXTENDED_PANELS_HPP_
#define VLC_QT_EXTENDED_PANELS_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <functional>

#include "qt.hpp"
#include "util/variables.hpp"

/* Auto-generated from .ui files */
#include "ui_video_effects.h"

class QScrollArea;
class QVBoxLayout;
class QTimer;
class QHideEvent;
class QTabWidget;

class QSignalMapper;

class ExtVideo: public QObject
{
    Q_OBJECT
    friend class ExtendedDialog;
public:
    ExtVideo( qt_intf_t *, QTabWidget * );
    void refreshVideoValues(const QHash<QString, QVariant>& pending);
    bool hasDraft() const;
    bool commitDrafts();
protected:
    bool eventFilter(QObject*, QEvent*) override;
private:
    Ui::ExtVideoWidget ui;
    QSignalMapper* filterMapper;
    QTabWidget* m_tabs;
    QList<QWidget*> m_options;
    QHash<QWidget*, QString> m_modules;
    QHash<QString, QString> m_filterTypes;
    std::function<void(const QHash<QString, QVariant>&)> m_refreshVideoValues;
    std::function<bool()> m_hasDraft;
    std::function<bool()> m_commitDrafts;
    std::function<void()> m_layoutControls;
    void buildControls();
    QVariant currentValue(const QString&, const QHash<QString, QVariant>& pending = {}) const;
    qt_intf_t *p_intf;
    void initComboBoxItems( QObject* );
    void setWidgetValue( QObject* );
    void setupSliderValueLabel( QSlider *slider, QLabel *label );
    void clean();
    void setFilterOption( const char *psz_module, const char *psz_option, int,
                          double, const char * );
    void changeVFiltersString( const char *psz_name, bool b_add );

private slots:
    void updateFilters();
    void updateFilterOptions();
    void cropChange();
    void browseLogo();
    void browseEraseFile();
signals:
    void configChanged( QString name, QVariant value );
    void draftChanged();
};

class ExtV4l2 : public QWidget
{
    Q_OBJECT
public:
    ExtV4l2(qt_intf_t *, QWidget *);
protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    qt_intf_t *p_intf;
    vlc_object_t *m_device = nullptr; // Identity only; dereference under player lock.
    QScrollArea *m_scroll;
    QWidget *m_content;
    QVBoxLayout *m_body;
    QLabel *m_status;
    QPushButton *m_open;
    QPushButton *m_refresh;
    QTimer *m_poll;
    bool m_retry = false;
    int m_controlCount = 0;
    QVariant applyValue(const QString&, int type, const QVariant& value = {});
    void updateMetrics();
    void updateStatus();
private slots:
    void Refresh();
    void checkDevice();
};

struct AudioFilterSliderData
{
    QString name;
    QString descs;
    QString units;
    float f_min;
    float f_max;
    float f_value;
    float f_resolution;
    float f_visual_multiplier;
};

class AudioFilterControlWidget : public QWidget
{
    Q_OBJECT

public:
    AudioFilterControlWidget( qt_intf_t *, QWidget *, const char *name );
    void refreshAudioValues(const QHash<QString, QVariant>& pending);

protected:
    std::function<void(const QHash<QString, QVariant>&)> m_refreshAudioValues;

    virtual void build();
    QVector<AudioFilterSliderData> controls;
    QGroupBox *slidersBox;
    qt_intf_t *p_intf;
    QString name; // filter's module name
    int i_smallfont;

protected slots:
    void enable( bool );

signals:
    void configChanged( QString name, QVariant value );
    void draftChanged();
};

class Equalizer: public AudioFilterControlWidget
{
    Q_OBJECT

public:
    Equalizer( qt_intf_t *, QWidget * );

protected:
    void build() override;

private:
    bool eventFilter(QObject*, QEvent*) override;
    std::function<void(int, bool)> m_selectBand;
    std::function<void()> m_layoutBands;

};

class Compressor: public AudioFilterControlWidget
{
    Q_OBJECT

public:
    Compressor( qt_intf_t *, QWidget * );
};

class Expander: public AudioFilterControlWidget
{
    Q_OBJECT

public:
    Expander( qt_intf_t *, QWidget * );
};

class Limiter: public AudioFilterControlWidget
{
    Q_OBJECT

public:
    Limiter( qt_intf_t *, QWidget * );
};

class Spatializer: public AudioFilterControlWidget
{
    Q_OBJECT

public:
    Spatializer( qt_intf_t *, QWidget * );
};

class StereoWidener: public AudioFilterControlWidget
{
    Q_OBJECT

public:
    StereoWidener( qt_intf_t *, QWidget * );
};

class PitchShifter: public AudioFilterControlWidget
{
    Q_OBJECT

public:
    PitchShifter( qt_intf_t *, QWidget * );
};

class StereoPanner: public AudioFilterControlWidget
{
    Q_OBJECT

public:
    StereoPanner( qt_intf_t *, QWidget * );
};

class SyncWidget : public QWidget
{
    Q_OBJECT
public:
    SyncWidget( QWidget * );
    void setValue( double d );
signals:
    void valueChanged( double );
private slots:
    void valueChangedHandler( double d );
private:
    QDoubleSpinBox spinBox;
    QLabel spinLabel;
};

class SyncControls : public QWidget
{
    Q_OBJECT
    friend class ExtendedDialog;
public:
    SyncControls( qt_intf_t *, QWidget * );
    virtual ~SyncControls();
private:
    qt_intf_t *p_intf;
    SyncWidget *AVSpin;
    SyncWidget *subsSpin;
    SyncWidget *secondarySubsSpin;
    QDoubleSpinBox *subSpeedSpin;
    QDoubleSpinBox *subDurationSpin;
    QVLCFloat m_SubsDelayCfgFactor;

    bool b_userAction;

    void clean();

    void initSubsDuration();
    void subsdelayClean();
    void subsdelaySetFactor( double );
    void changeVFiltersString( const char *psz_name, bool b_add );
public slots:
    void update();
private slots:
    void advanceAudio( double );
    void advanceSubs( double );
    void advanceSecondarySubs( double );
    void adjustSubsSpeed( double );
    void adjustSubsDuration( double );
};

#endif
