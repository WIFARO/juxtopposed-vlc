// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "scroll_focus_tracker.hpp"
#include "maininterface/mainctx.hpp"

#include <QApplication>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QGroupBox>
#include <QItemSelectionModel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QSet>
#include <QTextEdit>
#include <QTimer>

namespace {
class ScrollFocusTracker final : public QObject
{
public:
    ScrollFocusTracker(QWidget *root, MainCtx *ctx) : QObject(root), root(root), ctx(ctx)
    {
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, this, [this] {
            if (!this->root) return;
            const bool reveal = revealRequested;
            revealRequested = false;
            if (refreshWidgets) registerWidgets();
            if (reveal) revealFocus();
        });
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *current) {
            if (this->root && current && this->root->isAncestorOf(current)) schedule();
        });
        connect(ctx, &MainCtx::intfScaleFactorChanged, this, [this] { schedule(); });
        registerWidgets();
        schedule();
    }

private:
    QPointer<QWidget> root;
    QPointer<MainCtx> ctx;
    QTimer timer;
    QSet<QWidget*> widgets;
    QSet<QItemSelectionModel*> models;
    bool refreshWidgets = true;
    bool revealRequested = false;

    void schedule(bool reveal = true)
    {
        revealRequested |= reveal;
        // Coalesce focus and layout changes, after each control updates itself.
        if (!timer.isActive()) timer.start(0);
    }

    void registerWidgets()
    {
        refreshWidgets = false;
        auto children = root->findChildren<QWidget*>();
        children.prepend(root);
        for (auto *widget : children) {
            if (!widgets.contains(widget)) {
                widgets.insert(widget);
                widget->installEventFilter(this);
                connect(widget, &QObject::destroyed, this, [this, widget] { widgets.remove(widget); });
                if (auto *text = qobject_cast<QTextEdit*>(widget))
                    connect(text, &QTextEdit::cursorPositionChanged, this, [this, text] { if (text->hasFocus()) schedule(); });
                if (auto *text = qobject_cast<QPlainTextEdit*>(widget))
                    connect(text, &QPlainTextEdit::cursorPositionChanged, this, [this, text] { if (text->hasFocus()) schedule(); });
            }
            if (auto *view = qobject_cast<QAbstractItemView*>(widget)) {
                auto *model = view->selectionModel();
                if (model && !models.contains(model)) {
                    models.insert(model);
                    connect(model, &QItemSelectionModel::currentChanged, this, [this, model] {
                        auto *active = qobject_cast<QAbstractItemView*>(QApplication::focusWidget());
                        if (active && active->selectionModel() == model) schedule();
                    });
                    connect(model, &QObject::destroyed, this, [this, model] { models.remove(model); });
                }
            }
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ChildAdded) {
            // New destination/source controls may not be constructed yet.
            refreshWidgets = true;
            schedule(false);
        } else if (event->type() == QEvent::Show || event->type() == QEvent::Resize) {
            auto *widget = qobject_cast<QWidget*>(watched);
            auto *focus = QApplication::focusWidget();
            auto *area = qobject_cast<QAbstractScrollArea*>(focus);
            if (widget == root || widget == focus || (widget && focus && widget->isAncestorOf(focus))
                || (area && widget == area->viewport())) schedule();
        }
        return QObject::eventFilter(watched, event);
    }

    void revealFocus()
    {
        auto *widget = QApplication::focusWidget();
        if (!root || !ctx || !root->isVisible() || !widget || !root->isAncestorOf(widget)
            || widget->window() != root->window()) return;
        if (auto *parent = widget->parentWidget())
            if (qobject_cast<QAbstractSpinBox*>(parent) || qobject_cast<QComboBox*>(parent)) widget = parent;
        const int margin = qRound(6 * ctx->getIntfScaleFactor());
        for (auto *parent = widget->parentWidget(); parent && parent != root; parent = parent->parentWidget()) {
            auto *scroll = qobject_cast<QScrollArea*>(parent);
            if (!scroll || !scroll->widget()->isAncestorOf(widget)) continue;
            QRect rect(widget->mapTo(scroll->widget(), QPoint()), widget->size());
            if (auto *text = qobject_cast<QTextEdit*>(widget)) {
                text->ensureCursorVisible();
                if (text->height() > scroll->viewport()->height()) {
                    rect = text->cursorRect();
                    rect.moveTopLeft(text->viewport()->mapTo(scroll->widget(), rect.topLeft()));
                }
            } else if (auto *text = qobject_cast<QPlainTextEdit*>(widget)) {
                text->ensureCursorVisible();
                if (text->height() > scroll->viewport()->height()) {
                    rect = text->cursorRect();
                    rect.moveTopLeft(text->viewport()->mapTo(scroll->widget(), rect.topLeft()));
                }
            } else if (auto *view = qobject_cast<QAbstractItemView*>(widget)) {
                if (view->currentIndex().isValid()) {
                    view->scrollTo(view->currentIndex());
                    if (view->height() > scroll->viewport()->height()) {
                        rect = view->visualRect(view->currentIndex());
                        rect.moveTopLeft(view->viewport()->mapTo(scroll->widget(), rect.topLeft()));
                    }
                }
            } else if (qobject_cast<QGroupBox*>(widget)) {
                rect.setHeight(widget->fontMetrics().height() + 2 * margin);
            }
            const auto viewport = scroll->viewport()->size();
            scroll->ensureVisible(rect.center().x(), rect.center().y(),
                qMin(rect.width() / 2 + margin, viewport.width() / 2),
                qMin(rect.height() / 2 + margin, viewport.height() / 2));
        }
    }
};
}

void trackScrollFocus(QWidget *root, MainCtx *ctx)
{
    if (root && ctx) new ScrollFocusTracker(root, ctx);
}
