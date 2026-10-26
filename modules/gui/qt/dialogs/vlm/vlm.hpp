/*****************************************************************************
 * vlm.hpp : VLM Management
 ****************************************************************************
 * Copyright ( C ) 2006 the VideoLAN team
 *
 * Authors: Jean-François Massol <jf.massol@gmail.com>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
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

#ifndef QVLC_VLM_DIALOG_H_
#define QVLC_VLM_DIALOG_H_ 1

#include "widgets/native/qvlcframe.hpp"
#include "ui_vlm.h"
#include <vlc_vlm.h>
#include <QHash>
#include <memory>
#include <vector>

class VLMDialog : public QVLCFrame
{
    Q_OBJECT
public:
    explicit VLMDialog(qt_intf_t*);
    ~VLMDialog() override;
    void toggleVisible();
protected:
    void keyPressEvent(QKeyEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    void resizeEvent(QResizeEvent*) override;
private:
    using Media = std::unique_ptr<vlm_media_t, decltype(&vlm_media_Delete)>;
    Ui::Vlm ui;
    vlm_t *vlm = nullptr;
    std::vector<Media> media;
    QHash<qint64, QHash<QString, int>> states;
    qint64 editingId = -1;
    bool dirty = false, loadingEditor = false, refreshQueued = false, ready = false;
    QString lastExportPath;
    static int vlmEvent(vlc_object_t*, const char*, vlc_value_t, vlc_value_t, void*);
    bool refresh(qint64 selectId = -1);
    void queueRefresh();
    void updateText();
    void updateMetrics();
    void ensureEditorFocusVisible();
    void filterRows();
    void updateActions();
    QString validationError() const;
    void feedback(const QString&);
    vlm_media_t *selected() const;
    vlm_media_t *find(qint64) const;
    void edit(vlm_media_t*);
    void control(int);
    bool execute(const QString&);
private slots:
    void addVLMItem();
    void startModifyVLMItem();
    void saveModifications();
    void clearWidgets();
    void removeVLMItem();
    void selectInput();
    void selectOutput();
    bool exportVLMConf();
    bool importVLMConf();
};
#endif
