/*****************************************************************************
 * fingerprintdialog.cpp: Fingerprinter Dialog
 *****************************************************************************
 * Copyright (C) 2012 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "dialogs/fingerprint/fingerprintdialog.hpp"
#include "ui_fingerprintdialog.h"
#include "dialogs/fingerprint/chromaprint.hpp"
#include "maininterface/mainctx.hpp"
#include "util/interface_translator.hpp"
#include "util/redesign_widget_theme.hpp"

#include <vlc_fingerprinter.h>

#include <QDesktopServices>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScreen>
#include <QUuid>
#include <QUrl>

FingerprintDialog::FingerprintDialog(QWidget *parent, qt_intf_t *p_intf,
                                     input_item_t *p_item ) :
    QDialog(parent), ui(new Ui::FingerprintDialog), t(nullptr), p_r(nullptr),
    sourceItem(p_item), intf(p_intf)
{
    ui->setupUi(this);
    setObjectName(QStringLiteral("redesignFingerprint"));
    setWindowRole("vlc-find-tags");
    ui->heading->setProperty("redesignHeading", true);
    ui->dialogLayout->setStretch(2, 1);
    char *name = sourceItem ? input_item_GetName(sourceItem.get()) : nullptr;
    ui->fileName->setText(qfu(name ? name : ""));
    free(name);
    char *uri = sourceItem ? input_item_GetURI(sourceItem.get()) : nullptr;
    const auto path = QUrl(qfu(uri ? uri : "")).toDisplayString(QUrl::PreferLocalFile);
    ui->fileName->setToolTip(QStringLiteral("<qt>%1</qt>").arg(path.toHtmlEscaped()));
    free(uri);
    ui->fileName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    detailsButton = ui->detailsButton;
    detailsButton->setObjectName(QStringLiteral("fingerprintDetails"));
    retryButton = ui->buttonBox->addButton(QString(), QDialogButtonBox::AcceptRole);
    retryButton->setObjectName(QStringLiteral("fingerprintRetry"));
    retryButton->setProperty("redesignPrimary", true);
    useButton = ui->buttonBox->addButton(QString(), QDialogButtonBox::AcceptRole);
    useButton->setObjectName(QStringLiteral("fingerprintUse"));
    useButton->setProperty("redesignPrimary", true);
    cancelButton = ui->buttonBox->addButton(QString(), QDialogButtonBox::RejectRole);
    cancelButton->setObjectName(QStringLiteral("fingerprintCancel"));
    connect(useButton, &QPushButton::clicked, this, &FingerprintDialog::applyIdentity);
    connect(cancelButton, &QPushButton::clicked, this, &FingerprintDialog::reject);
    connect(retryButton, &QPushButton::clicked, this, &FingerprintDialog::startSearch);
    connect(ui->recordsList, &QListWidget::currentRowChanged, this, &FingerprintDialog::updateActions);
    connect(ui->recordsList, &QListWidget::itemDoubleClicked, this, &FingerprintDialog::applyIdentity);
    connect(detailsButton, &QPushButton::clicked, this, [this] {
        const auto *item = ui->recordsList->currentItem();
        if (!item) return;
        const QUuid id(item->data(Qt::UserRole).toString());
        if (!id.isNull())
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://mb.videolan.org/recording/%1")
                .arg(id.toString(QUuid::WithoutBraces))));
    });
    translate();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &FingerprintDialog::translate);
    applyRedesignWidgetTheme(this, p_intf->p_mi);
    const auto scale = [this, ctx = p_intf->p_mi] {
        const auto dp = [ctx](int value) { return qRound(value * (ctx ? ctx->getIntfScaleFactor() : 1.)); };
        ui->dialogLayout->setContentsMargins(dp(24), dp(24), dp(24), dp(24));
        ui->dialogLayout->setSpacing(dp(16));
        ui->actionsLayout->setSpacing(dp(8));
        ui->buttonBox->layout()->setSpacing(dp(8));
        for (auto *layout : {ui->resultsLayout, ui->errorLayout, ui->waitLayout})
        {
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(dp(12));
        }
        ui->progressBar->setFixedHeight(dp(4));
        for (auto *button : {useButton, cancelButton, retryButton, detailsButton})
        {
            button->setMinimumWidth(dp(100));
            button->setFixedHeight(dp(32));
            button->setAutoDefault(true);
        }
    };
    scale();
    if (p_intf->p_mi) connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, scale);
    const double factor = p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.;
    QSize initial(qRound(560 * factor), qRound(440 * factor));
    if (screen()) initial = initial.boundedTo(screen()->availableGeometry().size() - QSize(32, 32));
    resize(initial);
    startSearch();
}

void FingerprintDialog::translate()
{
    setWindowTitle(qtr("Find tags"));
    ui->heading->setText(qtr("Find tags"));
    ui->resultsLabel->setText(qtr("Select a match."));
    ui->saveNote->setText(qtr("Select Save in Media information to save the tags."));
    ui->waitLabel->setText(qtr("Finding tags…"));
    ui->progressBar->setAccessibleName(qtr("Finding tags…"));
    ui->recordsList->setAccessibleName(qtr("Matching tags"));
    useButton->setText(qtr("Use tags"));
    cancelButton->setText(qtr("Cancel"));
    retryButton->setText(qtr("Try again"));
    detailsButton->setText(qtr("View details"));
    ui->errorLabel->setText(qtr(errorMessage));
}

void FingerprintDialog::startSearch()
{
    if (p_r) { fingerprint_request_Delete(p_r); p_r = nullptr; }
    ++searchGeneration;
    delete t;
    t = new (std::nothrow) Chromaprint(intf);
    if (t) connect(t, &Chromaprint::finished, this, [this, generation = searchGeneration] {
        if (searchGeneration == generation) handleResults();
    }, Qt::QueuedConnection);
    ui->recordsList->clear();
    ui->stackedWidget->setCurrentWidget(ui->wait);
    if (!t || !sourceItem || !t->enqueue(sourceItem.get()))
        showError(N_("Cannot start the tag search."));
    else
        updateActions();
}

void FingerprintDialog::showError(const char *message)
{
    errorMessage = message;
    ui->errorLabel->setText(qtr(errorMessage));
    ui->stackedWidget->setCurrentWidget(ui->error);
    updateActions();
}

void FingerprintDialog::updateActions()
{
    const bool results = ui->stackedWidget->currentWidget() == ui->results;
    const auto *item = ui->recordsList->currentItem();
    useButton->setVisible(results);
    useButton->setEnabled(results && item);
    useButton->setDefault(useButton->isEnabled());
    detailsButton->setVisible(results);
    detailsButton->setEnabled(results && item && !QUuid(item->data(Qt::UserRole).toString()).isNull());
    const bool failed = ui->stackedWidget->currentWidget() == ui->error;
    retryButton->setVisible(failed);
    retryButton->setDefault(failed);
    cancelButton->setDefault(!useButton->isEnabled() && !failed);
}

void FingerprintDialog::applyIdentity()
{
    const int row = ui->recordsList->currentRow();
    if (!p_r || row < 0 || static_cast<size_t>(row) >= vlc_array_count(&p_r->results.metas_array)) return;
    const auto *selected = static_cast<vlc_meta_t *>(vlc_array_item_at_index(&p_r->results.metas_array, row));
    const SharedInputItem tags { input_item_Copy(sourceItem.get()), false };
    auto *meta = vlc_meta_New();
    if (!tags || !meta)
    {
        if (meta) vlc_meta_Delete(meta);
        showError(N_("Cannot use these tags."));
        return;
    }
    vlc_meta_Merge(meta, selected);
    const QUuid recording(qfu(vlc_meta_GetExtra(meta, "musicbrainz-id")));
    if (!recording.isNull()) vlc_meta_SetTrackID(meta, qtu(recording.toString(QUuid::WithoutBraces)));
    // Return only the selected tags. The editor retains its own draft and
    // writes the real file only after the user selects Save.
    if (tags->p_meta) vlc_meta_Delete(tags->p_meta);
    tags->p_meta = meta;
    emit tagsSelected(tags);
    accept();
}

void FingerprintDialog::handleResults()
{
    if (!t || ui->stackedWidget->currentWidget() != ui->wait) return;
    p_r = t->fetchResults();
    if (!p_r || p_r->p_item != sourceItem.get() || vlc_array_count(&p_r->results.metas_array) == 0)
    {
        if (p_r) { fingerprint_request_Delete(p_r); p_r = nullptr; }
        showError(N_("No matching tags were found."));
        return;
    }
    for (size_t i = 0; i < vlc_array_count(&p_r->results.metas_array); ++i)
    {
        const auto *meta = static_cast<vlc_meta_t *>(vlc_array_item_at_index(&p_r->results.metas_array, i));
        const auto title = qfu(vlc_meta_Get(meta, vlc_meta_Title));
        const auto artist = qfu(vlc_meta_Get(meta, vlc_meta_Artist));
        const QString text = (title.isEmpty() ? qtr("Unknown title") : title) + QLatin1Char('\n')
            + (artist.isEmpty() ? qtr("Unknown artist") : artist);
        auto *item = new QListWidgetItem(text, ui->recordsList);
        item->setData(Qt::UserRole, qfu(vlc_meta_GetExtra(meta, "musicbrainz-id")));
        item->setToolTip(QStringLiteral("<qt>%1</qt>").arg(text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"))));
        item->setData(Qt::AccessibleTextRole, text);
    }
    ui->stackedWidget->setCurrentWidget(ui->results);
    ui->recordsList->setCurrentRow(0);
    ui->recordsList->setFocus();
    updateActions();
}

FingerprintDialog::~FingerprintDialog()
{
    delete t;
    if (p_r) fingerprint_request_Delete(p_r);
    delete ui;
}
