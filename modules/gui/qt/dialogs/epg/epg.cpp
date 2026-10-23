/*****************************************************************************
 * epg.cpp : Epg Viewer dialog
 ****************************************************************************
 * Copyright © 2010 VideoLAN and AUTHORS
 *
 * Authors:    Jean-Baptiste Kempf <jb@videolan.org>
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
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "epg.hpp"

#include "EPGWidget.hpp"
#include "EPGItem.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QSplitter>
#include <QTimer>
#include <QDateTime>
#include <QLocale>
#include <QScreen>

#include "qt.hpp"
#include "player/player_controller.hpp"
#include "maininterface/mainctx.hpp"
#include "util/interface_translator.hpp"
#include "util/redesign_widget_theme.hpp"

EpgDialog::EpgDialog(qt_intf_t *intf) : QVLCFrame(intf)
{
    setObjectName(QStringLiteral("redesignProgramGuide"));
    setWindowRole("vlc-program-guide");
    auto *layout = new QVBoxLayout(this);
    heading = new QLabel(this);
    heading->setProperty("redesignHeading", true);
    heading->setTextFormat(Qt::PlainText);
    layout->addWidget(heading);
    help = new QLabel(this);
    help->setTextFormat(Qt::PlainText);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *tools = new QHBoxLayout;
    nowButton = new QPushButton(this);
    nowButton->setObjectName(QStringLiteral("guideNowButton"));
    zoomLabel = new QLabel(this);
    zoomOut = new QPushButton(QStringLiteral("−"), this);
    zoomOut->setObjectName(QStringLiteral("guideZoomOut"));
    zoomIn = new QPushButton(QStringLiteral("+"), this);
    zoomIn->setObjectName(QStringLiteral("guideZoomIn"));
    tools->addWidget(nowButton);
    tools->addStretch();
    tools->addWidget(zoomLabel);
    tools->addWidget(zoomOut);
    tools->addWidget(zoomIn);
    layout->addLayout(tools);
    epg = new EPGWidget(this);
    epg->setObjectName(QStringLiteral("programGuide"));
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setObjectName(QStringLiteral("guideSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(epg);
    auto *details = new QWidget(splitter);
    auto *detailLayout = new QVBoxLayout(details);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    title = new QLabel(details);
    title->setObjectName(QStringLiteral("guideProgramTitle"));
    title->setProperty("redesignSectionHeading", true);
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    time = new QLabel(details);
    time->setObjectName(QStringLiteral("guideProgramTime"));
    time->setTextFormat(Qt::PlainText);
    time->setWordWrap(true);
    description = new QTextEdit(details);
    description->setObjectName(QStringLiteral("guideProgramDescription"));
    description->setReadOnly(true);
    description->setAcceptRichText(false);
    detailLayout->addWidget(title);
    detailLayout->addWidget(time);
    detailLayout->addWidget(description, 1);
    splitter->addWidget(details);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    auto *actions = new QHBoxLayout;
    actions->addStretch();
    switchButton = new QPushButton(this);
    switchButton->setObjectName(QStringLiteral("guideSwitchChannel"));
    switchButton->setProperty("redesignPrimary", true);
    closeButton = new QPushButton(this);
    closeButton->setObjectName(QStringLiteral("closeProgramGuide"));
    actions->addWidget(switchButton);
    actions->addWidget(closeButton);
    layout->addLayout(actions);
    connect(closeButton, &QPushButton::clicked, this, &EpgDialog::close);
    connect(nowButton, &QPushButton::clicked, epg, &EPGWidget::goToNow);
    connect(switchButton, &QPushButton::clicked, epg, &EPGWidget::activateSelectedProgram);
    const auto zoom = [this](int step) {
        zoomLevel = qBound(1, zoomLevel + step, 8);
        if (step) epg->setZoom(zoomLevel);
        zoomOut->setEnabled(epg->hasValidData() && zoomLevel > 1);
        zoomIn->setEnabled(epg->hasValidData() && zoomLevel < 8);
    };
    connect(zoomOut, &QPushButton::clicked, this, [zoom] { zoom(-1); });
    connect(zoomIn, &QPushButton::clicked, this, [zoom] { zoom(1); });
    connect(epg, &EPGWidget::dataChanged, this, [this, zoom] {
        nowButton->setEnabled(epg->hasValidData());
        zoom(0);
    });
    connect(epg, &EPGWidget::itemSelectionChanged, this, &EpgDialog::displayEvent);
    connect(epg, &EPGWidget::programActivated, THEMIM, &PlayerController::changeProgram);
    connect(THEMIM, &PlayerController::epgChanged, this, &EpgDialog::scheduleUpdate);
    connect(THEMIM, &PlayerController::inputChanged, this, &EpgDialog::inputChanged);
    timer = new QTimer(this);
    timer->setObjectName(QStringLiteral("guideUpdateTimer"));
    timer->setSingleShot(true);
    timer->setInterval(5000);
    connect(timer, &QTimer::timeout, this, &EpgDialog::updateInfos);
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &EpgDialog::retranslate);
    applyRedesignWidgetTheme(this, p_intf->p_mi);
    const auto scaleLayout = [this, layout, tools, actions, detailLayout, splitter] {
        const auto dp = [this](int value) { return qRound(value * p_intf->p_mi->getIntfScaleFactor()); };
        layout->setContentsMargins(dp(24), dp(24), dp(24), dp(24));
        layout->setSpacing(dp(12));
        tools->setSpacing(dp(8));
        actions->setSpacing(dp(8));
        detailLayout->setSpacing(dp(8));
        splitter->setHandleWidth(dp(12));
        epg->setMinimumHeight(dp(120));
        description->setMinimumHeight(dp(72));
        for (auto *button : {nowButton, zoomOut, zoomIn, switchButton, closeButton}) button->setFixedHeight(dp(32));
        for (auto *button : {zoomOut, zoomIn}) button->setFixedWidth(dp(32));
        epg->setZoom(zoomLevel);
    };
    const auto accent = [this] { epg->setAccent(p_intf->p_mi->redesignAccent()); };
    accent();
    connect(p_intf->p_mi, &MainCtx::redesignAccentChanged, this, accent);
    scaleLayout();
    connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, scaleLayout);
    retranslate();
    updateInfos();
    const double factor = p_intf->p_mi->getIntfScaleFactor();
    QSize initial(qRound(880 * factor), qRound(650 * factor));
    if (screen()) initial = initial.boundedTo(screen()->availableGeometry().size() - QSize(32, 32));
    restoreWidgetPosition("EPGDialog", initial);
}

EpgDialog::~EpgDialog()
{
    saveWidgetPosition("EPGDialog");
}

void EpgDialog::retranslate()
{
    setWindowTitle(qtr("Program Guide"));
    heading->setText(qtr("Program guide"));
    help->setText(qtr("Select a program to see its details. Double-click to switch channels."));
    nowButton->setText(qtr("Now"));
    nowButton->setToolTip(qtr("Show the current time"));
    zoomLabel->setText(qtr("Zoom"));
    zoomOut->setAccessibleName(qtr("Zoom out"));
    zoomOut->setToolTip(qtr("Zoom out"));
    zoomIn->setAccessibleName(qtr("Zoom in"));
    zoomIn->setToolTip(qtr("Zoom in"));
    switchButton->setText(qtr("Switch channel"));
    closeButton->setText(qtr("&Close"));
    description->setAccessibleName(qtr("Program description"));
    displayEvent(epg->selectedItem());
}

void EpgDialog::showEvent(QShowEvent *event)
{
    QVLCFrame::showEvent(event);
    updateInfos();
}

void EpgDialog::hideEvent(QHideEvent *event)
{
    timer->stop();
    QVLCFrame::hideEvent(event);
}

void EpgDialog::inputChanged()
{
    epg->reset();
    if (isVisible()) updateInfos();
}

void EpgDialog::scheduleUpdate()
{
    if (isVisible() && !timer->isActive()) timer->start();
}

void EpgDialog::displayEvent(EPGItem *item)
{
    const bool selectionChanged = item != displayedItem;
    displayedItem = item;
    switchButton->setEnabled(item != nullptr);
    title->setText(item ? item->name() : qtr("Select a program"));
    time->setVisible(item != nullptr);
    QString details;
    if (item)
    {
        const QLocale locale;
        const auto format = [&locale](const QDateTime &date) {
            return date.date() == QDate::currentDate() ? locale.toString(date.time(), QLocale::ShortFormat)
                                                      : locale.toString(date, QLocale::ShortFormat);
        };
        QString hours = format(item->start()) + QStringLiteral(" – ") + format(item->end());
        if (item->rating()) hours += qtr(" (Ages %1 and above)").arg(item->rating());
        time->setText(hours);
        details = item->description();
        for (const auto &entry : item->descriptionItems())
        {
            if (!details.isEmpty()) details += QStringLiteral("\n\n");
            details += entry.first + QStringLiteral(": ") + entry.second;
        }
    }
    description->setPlaceholderText(item ? qtr("No description is available.") : qtr("Select a program to see its details."));
    // Keep the reading position when an unchanged broadcast update arrives.
    if (selectionChanged || description->toPlainText() != details) description->setPlainText(details);
}

void EpgDialog::updateInfos()
{
    input_item_t *media;
    {
        vlc_player_locker lock{p_intf->p_player};
        media = vlc_player_HoldCurrentMedia(p_intf->p_player);
    }
    if (media)
    {
        epg->updateEPG(media);
        input_item_Release(media);
    }
    else epg->reset();
}
