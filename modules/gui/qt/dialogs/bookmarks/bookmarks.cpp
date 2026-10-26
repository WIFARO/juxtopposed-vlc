/*****************************************************************************
 * bookmarks.cpp : Bookmarks
 ****************************************************************************
 * Copyright (C) 2007-2008 the VideoLAN team
 *
 * Authors: Antoine Lejeune <phytos@via.ecp.fr>
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

#include "bookmarks.hpp"
#include "player/player_controller.hpp"
#include "medialibrary/mlbookmarkmodel.hpp"
#include "maininterface/mainctx.hpp"


#include "util/interface_translator.hpp"
#include "util/redesign_widget_theme.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTreeView>
#include <QHeaderView>
#include <QLabel>
#include <QFrame>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QApplication>
#include <QClipboard>
#include <QScreen>
#include <QStyledItemDelegate>

namespace {
class BookmarkEditDelegate : public QStyledItemDelegate
{
public:
    BookmarkEditDelegate(MainCtx *context, QObject *parent) : QStyledItemDelegate(parent), ctx(context) {}
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), qRound(32 * ctx->getIntfScaleFactor())));
        return size;
    }
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress)
        {
            auto *key = static_cast<QKeyEvent *>(event);
            auto *editor = qobject_cast<QWidget *>(object);
            if (editor && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter))
            {
                emit commitData(editor);
                emit closeEditor(editor, NoHint);
                return true;
            }
            if (editor && key->key() == Qt::Key_Escape)
            {
                emit closeEditor(editor, RevertModelCache);
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }
private:
    MainCtx *ctx;
};
}

BookmarksDialog::BookmarksDialog(qt_intf_t *intf) : QVLCFrame(intf)
{
    setObjectName(QStringLiteral("redesignBookmarks"));
    setWindowFlags(Qt::Tool);
    setWindowOpacity(var_InheritFloat(p_intf, "qt-opacity"));
    setWindowRole("vlc-bookmarks");
    auto *layout = new QVBoxLayout(this);
    heading = new QLabel(this);
    heading->setProperty("redesignHeading", true);
    help = new QLabel(this);
    help->setWordWrap(true);
    source = new QLabel(this);
    source->setWordWrap(true);
    source->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    source->setObjectName(QStringLiteral("bookmarkSource"));
    layout->addWidget(heading);
    layout->addWidget(help);
    layout->addWidget(source);
    auto *actions = new QHBoxLayout;
    const auto button = [this](const char *name) {
        auto *result = new QPushButton(this);
        result->setObjectName(QLatin1String(name));
        result->setAutoDefault(false);
        return result;
    };
    addButton = button("bookmarkAdd");
    addButton->setProperty("redesignPrimary", true);
    goButton = button("bookmarkGoTo");
    editButton = button("bookmarkEdit");
    actions->addWidget(addButton);
    actions->addWidget(goButton);
    actions->addWidget(editButton);
    actions->addStretch();
    layout->addLayout(actions);
    empty = new QLabel(this);
    empty->setObjectName(QStringLiteral("bookmarkEmpty"));
    empty->setWordWrap(true);
    layout->addWidget(empty);
    bookmarksList = new QTreeView(this);
    bookmarksList->setObjectName(QStringLiteral("bookmarkList"));
    bookmarksList->setItemDelegate(new BookmarkEditDelegate(p_intf->p_mi, bookmarksList));
    bookmarksList->setUniformRowHeights(true);
    m_model = new MLBookmarkModel(bookmarksList);
    m_model->setPlayer(THEMIM);
    m_model->sort(1, Qt::AscendingOrder);
    m_model->setMl(p_intf->p_mi->getMediaLibrary());
    bookmarksList->setModel(m_model);
    bookmarksList->setRootIsDecorated(false);
    bookmarksList->setAlternatingRowColors(true);
    bookmarksList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    bookmarksList->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Go to is separate from editing: double-click and Return seek; Edit/F2 edits.
    bookmarksList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bookmarksList->setTextElideMode(Qt::ElideRight);
    bookmarksList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    bookmarksList->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    bookmarksList->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    bookmarksList->sortByColumn(1, Qt::AscendingOrder);
    bookmarksList->setSortingEnabled(true);
    connect(bookmarksList->header(), &QHeaderView::sortIndicatorChanged, this,
            [this, section = 1, direction = Qt::AscendingOrder](int column, Qt::SortOrder order) mutable {
        if (column == 2) bookmarksList->header()->setSortIndicator(section, direction);
        else { section = column; direction = order; }
    });
    bookmarksList->installEventFilter(this);
    bookmarksList->viewport()->installEventFilter(this);
    layout->addWidget(bookmarksList, 1);
    feedback = new QLabel(this);
    feedback->setObjectName(QStringLiteral("bookmarkFeedback"));
    feedback->setWordWrap(true);
    layout->addWidget(feedback);
    clearConfirmation = new QFrame(this);
    clearConfirmation->setObjectName(QStringLiteral("bookmarkClearConfirmation"));
    auto *confirmLayout = new QVBoxLayout(clearConfirmation);
    clearQuestion = new QLabel(clearConfirmation);
    clearQuestion->setWordWrap(true);
    confirmLayout->addWidget(clearQuestion);
    auto *confirmActions = new QHBoxLayout;
    confirmActions->addStretch();
    confirmClear = button("bookmarkConfirmClear");
    cancelClear = button("bookmarkCancelClear");
    confirmActions->addWidget(confirmClear);
    confirmActions->addWidget(cancelClear);
    confirmLayout->addLayout(confirmActions);
    layout->addWidget(clearConfirmation);
    clearConfirmation->hide();
    auto *footer = new QHBoxLayout;
    delButton = button("bookmarkRemove");
    clearButton = button("bookmarkRemoveAll");
    closeButton = button("bookmarkClose");
    footer->addWidget(delButton);
    footer->addWidget(clearButton);
    footer->addStretch();
    footer->addWidget(closeButton);
    layout->addLayout(footer);
    for (auto *label : {heading, help, source, empty, feedback, clearQuestion}) label->setTextFormat(Qt::PlainText);
    connect(addButton, &QPushButton::clicked, this, &BookmarksDialog::add);
    connect(goButton, &QPushButton::clicked, this, [this] { activateItem(bookmarksList->selectionModel()->selectedRows().value(0)); });
    connect(editButton, &QPushButton::clicked, this, &BookmarksDialog::edit);
    connect(delButton, &QPushButton::clicked, this, &BookmarksDialog::del);
    connect(clearButton, &QPushButton::clicked, this, &BookmarksDialog::clear);
    connect(closeButton, &QPushButton::clicked, this, &BookmarksDialog::close);
    connect(cancelClear, &QPushButton::clicked, this, &BookmarksDialog::cancelClearRequest);
    connect(confirmClear, &QPushButton::clicked, this, [this] {
        if (!clearConfirmation->isHidden() && clearUri == THEMIM->getUrl() && !THEMIM->getUrl().isEmpty())
            m_model->clear();
        cancelClearRequest();
    });
    connect(bookmarksList, &QTreeView::activated, this, &BookmarksDialog::activateItem);
    connect(bookmarksList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &BookmarksDialog::updateButtons);
    const auto refreshed = [this] {
        selectAdded();
        updateButtons();
    };
    connect(m_model, &QAbstractItemModel::modelReset, this, refreshed);
    connect(m_model, &QAbstractItemModel::dataChanged, this, refreshed);
    connect(THEMIM, &PlayerController::playingStateChanged, this, &BookmarksDialog::updateButtons);
    connect(THEMIM, &PlayerController::seekableChanged, this, &BookmarksDialog::updateButtons);
    connect(THEMIM, &PlayerController::timeChanged, this, [this] {
        const bool available = THEMIM->getTime().valid() && THEMIM->getTime().toVLCTick() >= 0;
        if (available != timeAvailable) updateButtons();
    });
    connect(THEMIM, &PlayerController::nameChanged, this, &BookmarksDialog::updateButtons);
    connect(THEMIM, &PlayerController::inputChanged, this, [this] {
        pendingAdd = false;
        bookmarksList->clearSelection();
        cancelClearRequest();
    });
    retranslate();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &BookmarksDialog::retranslate);
    applyRedesignWidgetTheme(this, p_intf->p_mi);
    const auto scale = [this, actions, footer, confirmLayout, confirmActions] {
        const auto dp = [this](int value) { return qRound(value * p_intf->p_mi->getIntfScaleFactor()); };
        updateMetrics();
        for (auto *row : {actions, footer, confirmActions}) row->setSpacing(dp(8));
        confirmLayout->setContentsMargins(0, 0, 0, 0);
        confirmLayout->setSpacing(dp(8));
        bookmarksList->setColumnWidth(1, dp(90));
        bookmarksList->doItemsLayout();
    };
    scale();
    connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, scale);
    const double factor = p_intf->p_mi->getIntfScaleFactor();
    restoreWidgetPosition("Bookmarks", QSize(qRound(760 * factor), qRound(560 * factor)));
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size() - QSize(32, 32)));
}

BookmarksDialog::~BookmarksDialog()
{
    saveWidgetPosition("Bookmarks");
}

void BookmarksDialog::updateMetrics()
{
    if (!help) return;
    const auto dp = [this](int n) { return qRound(n * p_intf->p_mi->getIntfScaleFactor()); };
    const bool compact = height() < dp(520);
    const int margin = dp(compact ? 16 : 24);
    layout()->setContentsMargins(margin, margin, margin, margin);
    layout()->setSpacing(dp(compact ? 8 : 12));
    help->setVisible(!compact);
    heading->setToolTip(help->text());
    heading->setAccessibleDescription(help->text());
}

void BookmarksDialog::resizeEvent(QResizeEvent *event)
{
    QVLCFrame::resizeEvent(event);
    updateMetrics();
}

void BookmarksDialog::retranslate()
{
    setWindowTitle(qtr("Bookmark details"));
    heading->setText(qtr("Bookmark details"));
    help->setText(qtr("Add a bookmark at the current time. Edit its name or description. Changes are saved automatically."));
    updateMetrics();
    addButton->setText(qtr("&Add at current time"));
    addButton->setToolTip(qtr("Add a bookmark here. Insert also adds a bookmark when the list has focus."));
    goButton->setText(qtr("&Go to"));
    goButton->setToolTip(qtr("Go to the selected bookmark. You can also press Return or double-click a row."));
    editButton->setText(qtr("&Edit"));
    editButton->setToolTip(qtr("Edit the selected name or description. You can also press F2."));
    delButton->setText(qtr("&Remove"));
    delButton->setToolTip(qtr("Remove the selected bookmarks. You can also press Delete."));
    clearButton->setText(qtr("Remove &all"));
    closeButton->setText(qtr("&Close"));
    confirmClear->setText(qtr("Remove all"));
    cancelClear->setText(qtr("Cancel"));
    clearQuestion->setText(qtr("Remove all bookmarks for this media?"));
    bookmarksList->header()->setToolTip(qtr("Select Name or Time to sort the bookmarks."));
    bookmarksList->setAccessibleName(qtr("Bookmarks"));
    bookmarksList->setAccessibleDescription(qtr("Select rows to go to, edit or remove bookmarks. Use Copy to copy the selected rows."));
    feedback->setAccessibleName(qtr("Bookmark status"));
    // The model's headers are translated on demand.
    bookmarksList->header()->viewport()->update();
    updateButtons();
}

void BookmarksDialog::updateButtons()
{
    const bool library = m_model->ml() != nullptr;
    const auto state = THEMIM->getPlayingState();
    const bool active = THEMIM->hasInput() && state != PlayerController::PLAYING_STATE_STOPPED && state != PlayerController::PLAYING_STATE_STOPPING;
    if (!library || !active || m_model->rowCount() == 0) clearConfirmation->hide();
    const bool armed = !clearConfirmation->isHidden();
    const auto selected = bookmarksList->selectionModel()->selectedRows();
    const int count = m_model->rowCount();
    timeAvailable = THEMIM->getTime().valid() && THEMIM->getTime().toVLCTick() >= 0;
    addButton->setEnabled(library && active && timeAvailable && !armed);
    goButton->setEnabled(active && THEMIM->isSeekable() && selected.size() == 1 && !armed);
    editButton->setEnabled(library && active && selected.size() == 1 && !armed);
    delButton->setEnabled(library && active && !selected.isEmpty() && !armed);
    clearButton->setEnabled(library && active && count > 0 && !armed);
    empty->setVisible(count == 0);
    if (!library) empty->setText(qtr("Turn on the media library to save bookmarks."));
    else if (!active) empty->setText(qtr("Open and play media to use bookmarks."));
    else empty->setText(qtr("No bookmarks for this media. Select Add at current time to create one."));
    const auto url = THEMIM->getUrl();
    source->setText(url.isEmpty() ? QString() : qtr("Media: %1").arg(THEMIM->getName()));
    source->setToolTip(QStringLiteral("<qt>%1</qt>").arg(url.toDisplayString(QUrl::RemovePassword | QUrl::PreferLocalFile).toHtmlEscaped()));
    const QString total = count == 1 ? qtr("1 bookmark.") : qtr("%1 bookmarks.").arg(count);
    if (!library || !active) feedback->setText(QString());
    else if (armed) feedback->setText(qtr("No bookmarks are removed until you confirm."));
    else if (!THEMIM->isSeekable()) feedback->setText(total + QLatin1Char(' ') + qtr("This media does not support seeking."));
    else feedback->setText(total + QLatin1Char(' ') + (selected.isEmpty() ? qtr("Select a bookmark to use it.") : selected.size() == 1 ? qtr("1 bookmark selected.") : qtr("%1 bookmarks selected.").arg(selected.size())));
}

void BookmarksDialog::add()
{
    if (!addButton->isEnabled()) return;
    pendingTimes.clear();
    for (int row = 0; row < m_model->rowCount(); ++row)
        pendingTimes.insert(m_model->index(row, 0).data(MLBookmarkModel::TimeMSRole).toLongLong());
    pendingUri = THEMIM->getUrl();
    pendingTime = THEMIM->getTime().toMilliseconds();
    pendingAdd = true;
    m_model->add();
}

void BookmarksDialog::selectAdded()
{
    if (!pendingAdd || pendingUri != THEMIM->getUrl()) return;
    for (int row = 0; row < m_model->rowCount(); ++row)
    {
        const auto index = m_model->index(row, 0);
        const auto time = index.data(MLBookmarkModel::TimeMSRole).toLongLong();
        if (pendingTimes.contains(time) && time != pendingTime) continue;
        pendingAdd = false;
        bookmarksList->setCurrentIndex(index);
        bookmarksList->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        bookmarksList->scrollTo(index);
        break;
    }
}

void BookmarksDialog::edit()
{
    if (!editButton->isEnabled()) return;
    auto index = bookmarksList->selectionModel()->selectedRows().first().siblingAtColumn(bookmarksList->currentIndex().column());
    if (index.column() == 1) index = index.siblingAtColumn(0);
    bookmarksList->setFocus();
    bookmarksList->edit(index);
}

void BookmarksDialog::del()
{
    if (!delButton->isEnabled()) return;
    pendingAdd = false;
    m_model->remove(bookmarksList->selectionModel()->selectedRows());
}

void BookmarksDialog::clear()
{
    if (!clearButton->isEnabled()) return;
    pendingAdd = false;
    clearUri = THEMIM->getUrl();
    clearConfirmation->show();
    updateButtons();
    cancelClear->setFocus();
}

void BookmarksDialog::cancelClearRequest()
{
    clearConfirmation->hide();
    updateButtons();
}

void BookmarksDialog::activateItem(const QModelIndex &index)
{
    if (goButton->isEnabled() && index.isValid() && bookmarksList->selectionModel()->selectedRows().first().row() == index.row()) m_model->select(index);
}

bool BookmarksDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == bookmarksList && event->type() == QEvent::ShortcutOverride)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool plainAction = key->modifiers() == Qt::NoModifier
            && (key->key() == Qt::Key_F2 || key->key() == Qt::Key_Delete || key->key() == Qt::Key_Insert);
        if (key->matches(QKeySequence::Copy) || plainAction || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
        {
            event->accept();
            return true;
        }
    }
    if (watched == bookmarksList && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { activateItem(bookmarksList->selectionModel()->selectedRows().value(0)); return true; }
        if (key->key() == Qt::Key_F2 && key->modifiers() == Qt::NoModifier) { edit(); return true; }
        if (key->key() == Qt::Key_Delete && key->modifiers() == Qt::NoModifier) { del(); return true; }
        if (key->key() == Qt::Key_Insert && key->modifiers() == Qt::NoModifier) { add(); return true; }
        if (key->matches(QKeySequence::Copy))
        {
            QStringList rows;
            for (const auto &index : bookmarksList->selectionModel()->selectedRows())
            {
                QStringList cells;
                for (int column = 0; column < m_model->columnCount(); ++column)
                    cells << index.siblingAtColumn(column).data().toString().replace(QLatin1Char('\t'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' '));
                rows << cells.join(QLatin1Char('\t'));
            }
            if (!rows.isEmpty()) QApplication::clipboard()->setText(rows.join(QLatin1Char('\n')));
            return true;
        }
    }
    if (watched == bookmarksList->viewport() && event->type() == QEvent::ToolTip)
    {
        auto *help = static_cast<QHelpEvent *>(event);
        const auto index = bookmarksList->indexAt(help->pos());
        if (index.isValid()) QToolTip::showText(help->globalPos(), QStringLiteral("<qt>%1</qt>").arg(index.data().toString().toHtmlEscaped()), bookmarksList);
        else QToolTip::hideText();
        return true;
    }
    return QVLCFrame::eventFilter(watched, event);
}

void BookmarksDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && !clearConfirmation->isHidden())
    {
        cancelClearRequest();
        bookmarksList->setFocus();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        if (auto *button = qobject_cast<QPushButton *>(focusWidget()))
            if (button->isEnabled()) button->click();
        event->accept();
        return;
    }
    QVLCFrame::keyPressEvent(event);
}

void BookmarksDialog::hideEvent(QHideEvent *event)
{
    pendingAdd = false;
    cancelClearRequest();
    QVLCFrame::hideEvent(event);
}

void BookmarksDialog::toggleVisible()
{
    updateButtons();
    QVLCFrame::toggleVisible();
}
