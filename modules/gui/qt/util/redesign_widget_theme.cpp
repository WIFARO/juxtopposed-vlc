// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "redesign_widget_theme.hpp"
#include "maininterface/mainctx.hpp"
#include "color_scheme_model.hpp"
#include "colorizedsvgicon.hpp"
#include <QApplication>
#include <QStyleHints>
#include <QWidget>
#include <QPointer>
#include <QMessageBox>
#include <QMenu>
#include <QAbstractButton>
#include <QToolTip>
#include <QFont>

void applyRedesignWidgetTheme(QWidget* widget, MainCtx* ctx)
{
    // Menus use RedesignMenuStyle; a dialog stylesheet overrides its item
    // painting (including hover states and submenu chevrons).
    if (!ctx || qobject_cast<QMenu*>(widget) || widget->property("redesignDialog").toBool()) return;
    widget->setProperty("redesignDialog", true);
    widget->setAttribute(Qt::WA_StyledBackground);
    const auto box = qobject_cast<QMessageBox*>(widget);
    const auto messageIcon = box ? box->icon() : QMessageBox::NoIcon;
    const auto applyTheme = [widget, messageIcon, ctx = QPointer<MainCtx>(ctx)] {
        if (!ctx) return;
        const auto scheme = ctx->getColorScheme()->currentScheme();
        const bool light = scheme == ColorSchemeModel::Day ||
            (scheme == ColorSchemeModel::System && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Light);
        const QString background = light ? "#acacac" : "#1b1b1b";
        const QString field = light ? "#dedede" : "#090909";
        const QString foreground = light ? "#404040" : "#8f8f8f";
        const QString bright = light ? "#2e2e2e" : "#e5e5e5";
        const QString border = light ? "#969696" : "#292929";
        const QString hover = light ? "#d5d5d5" : "#292929";
        const auto accent = ctx->redesignAccent().name();
        const auto dp = [ctx](int value) { return qRound(value * ctx->getIntfScaleFactor()); };
        QPalette colors = widget->palette();
        colors.setColor(QPalette::Window, QColor(background));
        colors.setColor(QPalette::WindowText, QColor(foreground));
        colors.setColor(QPalette::Base, QColor(field));
        colors.setColor(QPalette::AlternateBase, QColor(background));
        colors.setColor(QPalette::Text, QColor(bright));
        colors.setColor(QPalette::Button, QColor(field));
        colors.setColor(QPalette::ButtonText, QColor(foreground));
        colors.setColor(QPalette::Highlight, QColor(accent));
        colors.setColor(QPalette::HighlightedText, QColor("#161616"));
        const QColor linkColor = light ? ctx->redesignAccent().darker(200) : ctx->redesignAccent();
        colors.setColor(QPalette::Link, linkColor);
        colors.setColor(QPalette::LinkVisited, linkColor);
        colors.setColor(QPalette::Light, QColor(border));
        colors.setColor(QPalette::Midlight, QColor(border));
        colors.setColor(QPalette::Mid, QColor(border));
        colors.setColor(QPalette::Dark, QColor(border));
        colors.setColor(QPalette::Shadow, QColor(border));
        colors.setColor(QPalette::ToolTipBase, QColor(field));
        colors.setColor(QPalette::ToolTipText, QColor(bright));
        widget->setPalette(colors);
        QFont tooltipFont(QStringLiteral("Inter"));
        tooltipFont.setPixelSize(dp(14));
        QToolTip::setFont(tooltipFont);
        QToolTip::setPalette(colors);
        if (auto message = qobject_cast<QMessageBox*>(widget)) {
            for (auto button : message->buttons()) button->setIcon({});
            if (messageIcon == QMessageBox::Question || messageIcon == QMessageBox::Information) {
                const auto name = messageIcon == QMessageBox::Question ? QStringLiteral("help") : QStringLiteral("info");
                const auto icon = ColorizedSvgIcon(QStringLiteral(":/menu/%1.svg").arg(name), {}, {}, {},
                    {{QStringLiteral("#ff950d"), accent}, {QStringLiteral("#fafafa"), background}});
                message->setIconPixmap(icon.pixmap(QSize(dp(32), dp(32)), widget->devicePixelRatioF()));
            }
        }
        widget->setStyleSheet(QStringLiteral(
            "QWidget { font-family: Inter; font-size: %1px; color: %2; }"
            "QToolTip { background: %4; color: %5; border: 1px solid %6; border-radius: 4px; padding: 6px; }"
            "QWidget[redesignDialog=\"true\"], QDialog, QTabWidget::pane { background: %3; border: none; }"
            "QWidget#aboutWidget QFrame#footer { background: %4; }"
            "QWidget#aboutWidget QLabel#title { color: %5; }"
            "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QAbstractSpinBox, QAbstractItemView { background: %4; alternate-background-color: %3; color: %5; border: 1px solid %6; border-radius: 4px; padding: 5px; selection-background-color: %7; selection-color: #161616; }"
            "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QAbstractSpinBox:focus { border-color: %7; }"
            "QAbstractSpinBox { min-height: %9px; }"
            "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { background: transparent; border: none; }"
            "QAbstractSpinBox QLineEdit { background: transparent; border: none; padding: 0; }"
            "QComboBox::drop-down { background: transparent; border: none; width: %11px; }"
            "QComboBox::down-arrow { image: url(:/icons/chevron-down-%12.svg); width: %10px; height: %10px; border: none; }"
            "QComboBox::down-arrow:disabled { image: url(:/icons/chevron-down-%12-disabled.svg); }"
            "QCheckBox::indicator, QGroupBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; background: %4; border: 1px solid %6; }"
            "QCheckBox::indicator, QGroupBox::indicator { border-radius: 3px; }"
            "QRadioButton::indicator { border-radius: 7px; }"
            "QCheckBox::indicator:checked, QGroupBox::indicator:checked { background: %7; border-color: %7; image: url(:/icons/check.svg); }"
            "QRadioButton::indicator:checked { background: %7; border-color: %7; }"
            "QCheckBox::indicator:hover, QGroupBox::indicator:hover, QRadioButton::indicator:hover { border-color: %7; }"
            "QCheckBox::indicator:disabled, QGroupBox::indicator:disabled, QRadioButton::indicator:disabled { background: %8; border-color: %6; }"
            "QCheckBox::indicator:checked:disabled, QGroupBox::indicator:checked:disabled, QRadioButton::indicator:checked:disabled { background: %2; border-color: %2; }"
            "QPushButton { background: %4; border: 1px solid transparent; border-radius: 4px; padding: 6px 16px; min-height: 18px; }"
            "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 4px; }"
            "QToolButton:hover { background: %8; }"
            "QToolButton:focus { border-color: %7; }"
            "QToolButton::menu-button { background: %4; border: none; }"
            "QToolButton::menu-button:hover { background: %8; }"
            "QPushButton:hover { background: %8; }"
            "QPushButton:focus { border-color: %7; }"
            "QPushButton:pressed { background: %7; color: #161616; }"
            "QPushButton#saveMetadataButton, QPushButton[redesignPrimary=\"true\"] { background: %7; color: #161616; }"
            "QPushButton#saveMetadataButton:hover { border-color: %5; }"
            "QPushButton[redesignPrimary=\"true\"]:disabled { background: %8; color: %2; }"
            "QWidget:disabled { color: %6; }"
            "QTabBar::tab { background: %4; padding: 10px 16px; border-bottom: 2px solid transparent; }"
            "QTabBar::tab:selected { color: %5; border-bottom-color: %7; }"
            "QTabBar::tab:hover { background: %8; }"
            "QHeaderView { background: %8; border: none; padding: 0; border-radius: 0; }"
            "QHeaderView::section { background: %8; color: %5; border: none; padding: 6px; }"
            "QTableCornerButton::section { background: %8; border: none; }"
            "QGraphicsView { background: %4; border: none; }"
            "QScrollBar:vertical { background: %3; width: 6px; margin: 0; }"
            "QScrollBar:horizontal { background: %3; height: 6px; margin: 0; }"
            "QScrollBar::handle { background: %6; border-radius: 3px; }"
            "QScrollBar::handle:vertical { min-height: 24px; }"
            "QScrollBar::handle:horizontal { min-width: 24px; }"
            "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
            "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
            "QScrollArea, QScrollArea > QWidget > QWidget { background: %3; border: none; }")
            .arg(dp(14)).arg(foreground, background, field, bright, border, accent, hover)
            .arg(dp(18)).arg(dp(24)).arg(dp(32)).arg(light ? "light" : "dark"));
    };
    applyTheme();
    if (ctx) {
        QObject::connect(ctx, &MainCtx::redesignAccentChanged, widget, applyTheme);
        QObject::connect(ctx, &MainCtx::intfScaleFactorChanged, widget, applyTheme);
        QObject::connect(ctx->getColorScheme(), &ColorSchemeModel::currentChanged, widget, applyTheme);
        QObject::connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, widget, applyTheme);
    }
}
