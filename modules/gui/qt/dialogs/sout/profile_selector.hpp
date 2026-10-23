/*****************************************************************************
 * profile_selector.hpp : A small profile selector and editor
 ****************************************************************************
 * Copyright (C) 2009 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb@videolan.org>
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

#ifndef VLC_QT_PROFILE_SELECTOR_HPP_
#define VLC_QT_PROFILE_SELECTOR_HPP_

#include "qt.hpp"

#include <QWidget>
#include <QSet>
#include <QHash>
#include <QPointer>

#include "widgets/native/qvlcframe.hpp"
#include "util/soutchain.hpp"

/* Auto-generated from .ui files */
#include "ui_profiles.h"

class QComboBox;
class MainCtx;
class QToolButton;
class QPushButton;

class VLCProfileSelector : public QWidget
{
    Q_OBJECT

public:
    VLCProfileSelector( QWidget *_parent );
    ~VLCProfileSelector();
    void setThemeContext(MainCtx* ctx);
    QString getMux() { return mux; }
    SoutChain getTranscode() { return transcode; }
private:
    QComboBox *profileBox;
    void fillProfilesCombo();
    void editProfilePrivate( const QString&, const QString& );
    void saveProfiles();
    QString mux;
    SoutChain transcode;
    QPointer<MainCtx> m_themeContext;
    QToolButton *m_editButton;
    QToolButton *m_deleteButton;
    QToolButton *m_newButton;
    void updateActionIcons();
private slots:
    void newProfile();
    void editProfile();
    void deleteProfile();
    void updateOptions( int i );
    void updateOptionsOldFormat( int i );
signals:
    void optionsChanged();
};

class VLCProfileEditor : public QVLCDialog
{
    Q_OBJECT

    Ui::Profiles ui;
public:
    VLCProfileEditor(const QString&, const QString&, QWindow *, MainCtx* = nullptr,
                     const QStringList& existingNames = {});

    QString name;
    QString muxValue;
    QString transcodeValue();
    QStringList qpcodecsList;
private:
    QPointer<MainCtx> m_themeContext;
    const QString m_originalName;
    const QStringList m_existingNames;
    QStringList m_extraOptions;
    QPushButton *m_saveButton;
    void updateEditorText();
    void updateValidation();
    QString validationError() const;
    QString scaleValue(bool *valid = nullptr) const;
    void updateLayoutMetrics();
    void updateFilterSearch();
    void selectComboValue(QComboBox*, const QString&);
    void registerCodecs();
    void registerFilters();
    void retranslateFilters();
    void updateMuxerWarning();
    void fillProfile( const QString& qs );
    void fillProfileOldFormat( const QString& qs );
    typedef QSet<QString> resultset;
    QHash<QString, resultset> caps;
    void loadCapabilities();
    void reset();
protected:
    bool eventFilter(QObject*, QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
public slots:
    void accept() override;
private slots:
    void muxSelected();
    void codecSelected();
    void activatePanels();
    void fixBirateState();
    void fixQPState();
};

#endif
