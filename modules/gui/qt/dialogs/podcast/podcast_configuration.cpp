/*****************************************************************************
 * podcast_configuration.cpp: Podcast configuration dialog
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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "podcast_configuration.hpp"

#include "maininterface/mainctx.hpp"
#include "util/interface_translator.hpp"
#include "util/redesign_widget_theme.hpp"

#include <QClipboard>
#include <QApplication>
#include <QKeyEvent>
#include <QUrl>
#include <QScreen>
#include <QItemSelectionModel>
#include <algorithm>

PodcastConfigDialog::PodcastConfigDialog(qt_intf_t *intf)
    : QVLCDialog(nullptr, intf)
{
    ui.setupUi(this);
    setObjectName(QStringLiteral("redesignPodcasts"));
    setWindowRole("vlc-podcast-configuration");
    ui.heading->setProperty("redesignHeading", true);
    for (auto *label : {ui.heading, ui.help, ui.entryMessage, ui.draftStatus, ui.urlLabel})
        label->setTextFormat(Qt::PlainText);
    ui.rootLayout->setStretch(2, 1);
    ui.podcastList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.podcastList->setTextElideMode(Qt::ElideRight);
    ui.podcastList->installEventFilter(this);
    ui.podcastList->viewport()->installEventFilter(this);
    ui.podcastURL->installEventFilter(this);
    emptyMessage = new QLabel(ui.podcastList->viewport());
    emptyMessage->setObjectName(QStringLiteral("podcastEmpty"));
    emptyMessage->setAlignment(Qt::AlignCenter);
    emptyMessage->setWordWrap(true);
    emptyMessage->setTextFormat(Qt::PlainText);
    emptyMessage->setAttribute(Qt::WA_TransparentForMouseEvents);
    saveButton = new QPushButton(this);
    saveButton->setObjectName(QStringLiteral("savePodcastSubscriptions"));
    saveButton->setProperty("redesignPrimary", true);
    cancelButton = new QPushButton(this);
    cancelButton->setObjectName(QStringLiteral("cancelPodcastSubscriptions"));
    ui.okCancel->addButton(saveButton, QDialogButtonBox::AcceptRole);
    ui.okCancel->addButton(cancelButton, QDialogButtonBox::RejectRole);
    for (auto *button : {saveButton, cancelButton, ui.podcastAdd, ui.podcastDelete, ui.podcastUp, ui.podcastDown})
        button->setAutoDefault(false);
    connect(ui.okCancel, &QDialogButtonBox::accepted, this, &PodcastConfigDialog::accept);
    connect(ui.okCancel, &QDialogButtonBox::rejected, this, &PodcastConfigDialog::reject);
    connect(ui.podcastAdd, &QPushButton::clicked, this, &PodcastConfigDialog::add);
    connect(ui.podcastDelete, &QPushButton::clicked, this, &PodcastConfigDialog::remove);
    connect(ui.podcastUp, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(ui.podcastDown, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(ui.podcastURL, &QLineEdit::textChanged, this, [this] {
        showMessage({});
        updateState();
    });
    connect(ui.podcastList, &QListWidget::itemSelectionChanged, this, &PodcastConfigDialog::updateState);
    for (auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(ui.podcastList->model(), signal, this, [this] { updateState(); });
    connect(ui.podcastList->model(), &QAbstractItemModel::rowsMoved, this, [this] { updateState(); });
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &PodcastConfigDialog::retranslate);
    applyRedesignWidgetTheme(this, p_intf->p_mi);
    const auto scale = [this] {
        const auto dp = [this](int value) { return qRound(value * p_intf->p_mi->getIntfScaleFactor()); };
        ui.rootLayout->setContentsMargins(dp(24), dp(24), dp(24), dp(24));
        ui.rootLayout->setSpacing(dp(12));
        ui.entryLayout->setSpacing(dp(8));
        ui.listActions->setSpacing(dp(8));
        ui.podcastList->setMinimumHeight(dp(140));
        for (auto *button : {saveButton, cancelButton, ui.podcastAdd, ui.podcastDelete, ui.podcastUp, ui.podcastDown})
            button->setFixedHeight(dp(32));
        ui.podcastURL->setFixedHeight(dp(32));
    };
    scale();
    connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, scale);
    retranslate();
    const double factor = p_intf->p_mi->getIntfScaleFactor();
    QSize initial(qRound(680 * factor), qRound(540 * factor));
    if (screen()) initial = initial.boundedTo(screen()->availableGeometry().size() - QSize(32, 32));
    resize(initial);
}

PodcastConfigDialog::~PodcastConfigDialog()
{
    disconnect(ui.podcastList->model(), nullptr, this, nullptr);
}

void PodcastConfigDialog::retranslate()
{
    ui.retranslateUi(this);
    ui.podcastURL->setPlaceholderText(QStringLiteral("https://example.com/feed.xml"));
    ui.podcastURL->setAccessibleName(qtr("Feed URL"));
    ui.podcastList->setAccessibleName(qtr("Podcast feeds"));
    ui.podcastDelete->setToolTip(qtr("Remove selected feeds (%1)").arg(QKeySequence(Qt::Key_Delete).toString(QKeySequence::NativeText)));
    ui.podcastUp->setToolTip(qtr("Move selected feed up (%1)").arg(QKeySequence(Qt::AltModifier | Qt::Key_Up).toString(QKeySequence::NativeText)));
    ui.podcastDown->setToolTip(qtr("Move selected feed down (%1)").arg(QKeySequence(Qt::AltModifier | Qt::Key_Down).toString(QKeySequence::NativeText)));
    saveButton->setToolTip(qtr("Save subscriptions (%1)").arg(QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
    emptyMessage->setText(qtr("No feeds are in the list. Add a feed URL below."));
    saveButton->setText(qtr("&Save"));
    showMessage({});
    updateState();
}

QStringList PodcastConfigDialog::urls() const
{
    QStringList result;
    for (int row = 0; row < ui.podcastList->count(); ++row) result << ui.podcastList->item(row)->text();
    return result;
}

bool PodcastConfigDialog::isDirty() const
{
    return urls() != originalUrls || !ui.podcastURL->text().trimmed().isEmpty();
}

void PodcastConfigDialog::showMessage(const QString &message)
{
    ui.entryMessage->setText(message);
    ui.entryMessage->setVisible(!message.isEmpty());
    ui.podcastURL->setAccessibleDescription(message);
}

void PodcastConfigDialog::updateState()
{
    if (loading) return;
    const bool dirty = isDirty();
    saveButton->setEnabled(dirty);
    cancelButton->setText(dirty ? qtr("&Cancel") : qtr("&Close"));
    ui.draftStatus->setText(dirty ? qtr("Changes are not saved.") : QString());
    ui.draftStatus->setVisible(dirty);
    ui.podcastAdd->setEnabled(!ui.podcastURL->text().trimmed().isEmpty());
    const auto selected = ui.podcastList->selectedItems();
    ui.podcastDelete->setEnabled(!selected.isEmpty());
    const int row = selected.size() == 1 ? ui.podcastList->row(selected.first()) : -1;
    ui.podcastUp->setEnabled(row > 0);
    ui.podcastDown->setEnabled(row >= 0 && row < ui.podcastList->count() - 1);
    emptyMessage->setGeometry(ui.podcastList->viewport()->rect().adjusted(16, 16, -16, -16));
    emptyMessage->setVisible(ui.podcastList->count() == 0);
}

void PodcastConfigDialog::showEvent(QShowEvent *event)
{
    loading = true;
    ui.podcastURL->clear();
    ui.podcastList->clear();
    char *value = config_GetPsz("podcast-urls");
    originalUrls = qfu(value).split(QLatin1Char('|'), Qt::SkipEmptyParts);
    free(value);
    for (const auto &url : originalUrls)
    {
        auto *item = new QListWidgetItem(url, ui.podcastList);
        item->setToolTip(QStringLiteral("<qt>%1</qt>").arg(url.toHtmlEscaped()));
    }
    loading = false;
    showMessage({});
    updateState();
    QVLCDialog::showEvent(event);
    ui.podcastURL->setFocus();
}

bool PodcastConfigDialog::addPendingFeed()
{
    const QString text = ui.podcastURL->text().trimmed();
    if (text.isEmpty()) return true;
    const QUrl url(text, QUrl::StrictMode);
    const auto scheme = url.scheme().toLower();
    const bool needsHost = scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("ftp");
    const bool space = std::any_of(text.cbegin(), text.cend(), [](QChar ch) { return ch.isSpace() || ch.category() == QChar::Other_Control; });
    if (!url.isValid() || url.isRelative() || scheme.isEmpty() || (needsHost && url.host().isEmpty())
        || (url.host().isEmpty() && url.path().isEmpty()) || space || text.contains(QLatin1Char('|')))
    {
        showMessage(qtr("Enter a full feed URL, such as https://example.com/feed.xml."));
        ui.podcastURL->setFocus();
        return false;
    }
    for (int row = 0; row < ui.podcastList->count(); ++row)
    {
        auto *item = ui.podcastList->item(row);
        if (QUrl(item->text()) == url)
        {
            ui.podcastList->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
            ui.podcastList->scrollToItem(item);
            ui.podcastURL->clear();
            showMessage(qtr("This feed is already in the list."));
            return true;
        }
    }
    const QString encoded = url.toString(QUrl::FullyEncoded);
    auto *item = new QListWidgetItem(encoded, ui.podcastList);
    item->setToolTip(QStringLiteral("<qt>%1</qt>").arg(encoded.toHtmlEscaped()));
    ui.podcastList->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
    ui.podcastList->scrollToItem(item);
    ui.podcastURL->clear();
    updateState();
    return true;
}

void PodcastConfigDialog::accept()
{
    if (!isDirty() || !addPendingFeed()) return;
    const auto current = urls();
    if (current != originalUrls)
    {
        const auto serialized = current.join(QLatin1Char('|'));
        config_PutPsz("podcast-urls", qtu(serialized));
        const QString request = QStringLiteral("UPDATE:") + serialized;
        var_SetString(vlc_object_instance(p_intf), "podcast-request", qtu(request));
    }
    QDialog::accept();
}

void PodcastConfigDialog::add()
{
    if (addPendingFeed()) ui.podcastURL->setFocus();
}

void PodcastConfigDialog::remove()
{
    for (auto *item : ui.podcastList->selectedItems()) delete ui.podcastList->takeItem(ui.podcastList->row(item));
    updateState();
}

void PodcastConfigDialog::moveSelected(int direction)
{
    const auto selected = ui.podcastList->selectedItems();
    if (selected.size() != 1) return;
    const int row = ui.podcastList->row(selected.first()), next = row + direction;
    if (next < 0 || next >= ui.podcastList->count()) return;
    auto *item = ui.podcastList->takeItem(row);
    ui.podcastList->insertItem(next, item);
    ui.podcastList->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
    updateState();
}

bool PodcastConfigDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui.podcastList->viewport() && event->type() == QEvent::Resize)
        emptyMessage->setGeometry(ui.podcastList->viewport()->rect().adjusted(16, 16, -16, -16));
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
    {
        const auto *key = static_cast<QKeyEvent *>(event);
        const bool enter = watched == ui.podcastURL && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter);
        const bool remove = watched == ui.podcastList && key->key() == Qt::Key_Delete;
        const bool copy = watched == ui.podcastList && key->matches(QKeySequence::Copy);
        const bool move = watched == ui.podcastList && key->modifiers() == Qt::AltModifier
            && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down);
        if (enter || remove || copy || move)
        {
            if (event->type() == QEvent::KeyPress)
            {
                if (enter) add();
                if (remove) this->remove();
                if (move) moveSelected(key->key() == Qt::Key_Up ? -1 : 1);
                if (copy)
                {
                    QStringList selected;
                    for (int row = 0; row < ui.podcastList->count(); ++row)
                        if (ui.podcastList->item(row)->isSelected()) selected << ui.podcastList->item(row)->text();
                    if (!selected.isEmpty()) QApplication::clipboard()->setText(selected.join(QLatin1Char('\n')));
                }
            }
            event->accept();
            return true;
        }
    }
    return QVLCDialog::eventFilter(watched, event);
}

bool PodcastConfigDialog::event(QEvent *event)
{
    // The main window also has an application-wide Ctrl+S streaming action.
    if (event->type() == QEvent::ShortcutOverride
        && static_cast<QKeyEvent *>(event)->matches(QKeySequence::Save))
    {
        event->accept();
        return true;
    }
    return QVLCDialog::event(event);
}

void PodcastConfigDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Save))
    {
        accept();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        if (auto *button = qobject_cast<QPushButton *>(focusWidget()))
        {
            if (button->isEnabled()) button->click();
            event->accept();
            return;
        }
    QVLCDialog::keyPressEvent(event);
}
