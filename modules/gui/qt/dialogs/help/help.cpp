/*****************************************************************************
 * help.cpp : Help and About dialogs
 ****************************************************************************
 * Copyright (C) 2007 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb (at) videolan.org>
 *          Rémi Duraffort <ivoire (at) via.ecp.fr>
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

#include "qt.hpp"
#include "help.hpp"
#include "aboutmodel.hpp"
#include "maininterface/mainctx.hpp"
#include "util/color_scheme_model.hpp"
#include "util/qt_dirs.hpp"
#include "util/interface_translator.hpp"


#include <vlc_intf_strings.h>

#ifdef UPDATE_CHECK
# include "util/github_release_service.hpp"
# include <QDesktopServices>
#endif

#include <QTextBrowser>
#include <QString>
#include <QDialogButtonBox>
#include <QEvent>
#include <QDate>
#include <QPushButton>
#include <QApplication>
#include <QStyleHints>
#include <QScrollBar>
#include <QLineEdit>
#include <QTextBlock>
#include <QClipboard>
#include <QKeyEvent>
#include <QToolButton>
#include <QTimer>
#include <QScreen>

#include <cassert>

namespace {
int helpDp(qt_intf_t *intf, int value)
{
    return qRound(value * (intf->p_mi ? intf->p_mi->getIntfScaleFactor() : 1));
}

void colorBrowserLinks(QTextBrowser *browser, const QColor &linkColor)
{
    // Change only the anchor format; keep selections and the reading position.
    QList<QTextCursor> anchors;
    for (auto block = browser->document()->firstBlock(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
        {
            const auto fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isAnchor()) continue;
            QTextCursor cursor(browser->document());
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            anchors.append(cursor);
        }
    QTextCharFormat color;
    color.setForeground(linkColor);
    for (auto &cursor : anchors) cursor.mergeCharFormat(color);
}

bool activateHelpButton(QKeyEvent *event, QWidget *focused)
{
    if (event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter) return false;
    if (auto *button = qobject_cast<QAbstractButton*>(focused)) button->click();
    event->accept();
    return true;
}
}

HelpDialog::HelpDialog(qt_intf_t *intf) : QVLCFrame(intf)
{
    setObjectName(QStringLiteral("redesignHelp"));
    setWindowRole("vlc-help");
    auto *root = new QVBoxLayout(this);
    auto *heading = new QLabel(this);
    heading->setProperty("redesignHeading", true);
    root->addWidget(heading);
    auto *findRow = new QHBoxLayout;
    auto *findLabel = new QLabel(this);
    m_find = new QLineEdit(this);
    m_find->setObjectName(QStringLiteral("helpFind"));
    m_find->setClearButtonEnabled(true);
    findLabel->setBuddy(m_find);
    m_previous = new QPushButton(this);
    m_previous->setObjectName(QStringLiteral("helpPrevious"));
    m_next = new QPushButton(this);
    m_next->setObjectName(QStringLiteral("helpNext"));
    findRow->addWidget(findLabel);
    findRow->addWidget(m_find, 1);
    findRow->addWidget(m_previous);
    findRow->addWidget(m_next);
    root->addLayout(findRow);
    m_browser = new QTextBrowser(this);
    m_browser->setObjectName(QStringLiteral("helpBrowser"));
    m_browser->setOpenExternalLinks(true);
    m_browser->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByKeyboard);
    m_browser->setUndoRedoEnabled(false);
    m_browser->setTabChangesFocus(true);
    root->addWidget(m_browser, 1);
    auto *footer = new QHBoxLayout;
    m_findInfo = new QLabel(this);
    m_findInfo->setObjectName(QStringLiteral("helpFindInfo"));
    m_findInfo->setWordWrap(true);
    footer->addWidget(m_findInfo, 1);
    auto *closeButton = new QPushButton(this);
    closeButton->setObjectName(QStringLiteral("helpClose"));
    footer->addWidget(closeButton);
    root->addLayout(footer);
    for (auto *button : {m_previous, m_next, closeButton}) button->setAutoDefault(false);
    connect(closeButton, &QPushButton::clicked, this, &HelpDialog::close);
    connect(m_find, &QLineEdit::textChanged, this, &HelpDialog::updateSearch);
    connect(m_previous, &QPushButton::clicked, this, [this] { showMatch(m_currentMatch - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] { showMatch(m_currentMatch + 1); });
    const auto reloadText = [this, heading, findLabel, closeButton] {
        const auto scroll = m_browser->verticalScrollBar()->value();
        setWindowTitle(qtr("Help"));
        heading->setText(qtr("Help"));
        findLabel->setText(qtr("&Find:"));
        m_find->setPlaceholderText(qtr("Find in this page"));
        m_find->setAccessibleName(qtr("Find in this page"));
        m_find->setToolTip(QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText));
        m_previous->setText(qtr("&Previous"));
        m_next->setText(qtr("&Next"));
        m_previous->setToolTip(QKeySequence(QKeySequence::FindPrevious).toString(QKeySequence::NativeText));
        m_next->setToolTip(QKeySequence(QKeySequence::FindNext).toString(QKeySequence::NativeText));
        closeButton->setText(qtr("&Close"));
        m_browser->setHtml(qfut(I_LONGHELP));
        colorBrowserLinks(m_browser, palette().color(QPalette::Link));
        updateSearch();
        m_browser->verticalScrollBar()->setValue(scroll);
    };
    const auto metrics = [this, root] {
        root->setContentsMargins(helpDp(p_intf, 24), helpDp(p_intf, 24), helpDp(p_intf, 24), helpDp(p_intf, 24));
        root->setSpacing(helpDp(p_intf, 12));
    };
    reloadText();
    metrics();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, reloadText);
    if (auto *ctx = p_intf->p_mi)
    {
        connect(ctx, &MainCtx::intfScaleFactorChanged, this, metrics);
        const auto links = [this] { colorBrowserLinks(m_browser, palette().color(QPalette::Link)); };
        connect(ctx, &MainCtx::redesignAccentChanged, this, links);
        connect(ctx->getColorScheme(), &ColorSchemeModel::currentChanged, this, links);
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, links);
    }
    restoreWidgetPosition("Help", QSize(helpDp(p_intf, 720), helpDp(p_intf, 580)));
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    setTabOrder(m_find, m_previous);
    setTabOrder(m_previous, m_next);
    setTabOrder(m_next, m_browser);
    setTabOrder(m_browser, closeButton);
    for (auto *child : findChildren<QWidget*>()) child->installEventFilter(this);
}

HelpDialog::~HelpDialog()
{
    saveWidgetPosition("Help");
}

void HelpDialog::updateSearch()
{
    m_matches.clear();
    if (!m_find->text().isEmpty())
    {
        auto cursor = m_browser->document()->find(m_find->text());
        while (!cursor.isNull())
        {
            m_matches.append(cursor);
            cursor = m_browser->document()->find(m_find->text(), cursor);
        }
    }
    showMatch(0);
}

void HelpDialog::showMatch(int index)
{
    m_previous->setEnabled(!m_matches.isEmpty());
    m_next->setEnabled(!m_matches.isEmpty());
    if (m_matches.isEmpty())
    {
        m_currentMatch = -1;
        auto cursor = m_browser->textCursor();
        cursor.clearSelection();
        m_browser->setTextCursor(cursor);
        m_findInfo->setText(m_find->text().isEmpty() ? QString() : qtr("No text found."));
        return;
    }
    m_currentMatch = (index % m_matches.size() + m_matches.size()) % m_matches.size();
    m_browser->setTextCursor(m_matches.at(m_currentMatch));
    m_browser->ensureCursorVisible();
    m_findInfo->setText(qtr("Match %1 of %2").arg(m_currentMatch + 1).arg(m_matches.size()));
}

bool HelpDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent*>(event);
        const bool find = key->matches(QKeySequence::Find);
        const bool next = key->matches(QKeySequence::FindNext) ||
            (watched == m_find && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter));
        const bool previous = key->matches(QKeySequence::FindPrevious);
        const bool reset = watched == m_find && key->key() == Qt::Key_Escape && !m_find->text().isEmpty();
        const bool copy = watched == m_browser && key->matches(QKeySequence::Copy);
        const bool selectAll = watched == m_browser && key->matches(QKeySequence::SelectAll);
        if (find || next || previous || reset || copy || selectAll)
        {
            if (event->type() == QEvent::KeyPress)
            {
                if (find) { m_find->setFocus(); m_find->selectAll(); }
                else if (reset) m_find->clear();
                else if (copy) m_browser->copy();
                else if (selectAll) m_browser->selectAll();
                else showMatch(m_currentMatch + (previous ? -1 : 1));
            }
            key->accept();
            return true;
        }
    }
    return QVLCFrame::eventFilter(watched, event);
}

void HelpDialog::keyPressEvent(QKeyEvent *event)
{
    if (!activateHelpButton(event, focusWidget())) QVLCFrame::keyPressEvent(event);
}

AboutDialog::AboutDialog(qt_intf_t *intf) : QVLCDialog(nullptr, intf)
{
    ui.setupUi(this);
    setWindowRole("vlc-about");
    setWindowModality(Qt::WindowModal);
    ui.version->setText(AboutModel::getVersion());
    ui.licensePage->setPlainText(AboutModel::getLicense());
    ui.authorsPage->setPlainText(AboutModel::getAuthors());
    ui.creditPage->setPlainText(AboutModel::getThanks());
    ui.MainBlabla->setUndoRedoEnabled(false);
    ui.MainBlabla->setTabChangesFocus(true);
    ui.buildDetails->hide();
    connect(ui.buildDetailsButton, &QToolButton::toggled, this, [this](bool show) {
        ui.buildDetails->setVisible(show);
        ui.buildDetailsButton->setArrowType(show ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(ui.closeButton, &QPushButton::clicked, this, &QDialog::reject);
    auto *copyTimer = new QTimer(this);
    copyTimer->setSingleShot(true);
    copyTimer->setInterval(2500);
    connect(copyTimer, &QTimer::timeout, ui.copyFeedback, &QLabel::clear);
    connect(ui.copyVersionButton, &QPushButton::clicked, this, [this, copyTimer] {
        QGuiApplication::clipboard()->setText(QStringLiteral("VLC media player ") + AboutModel::getVersion());
        ui.copyFeedback->setText(qtr("Version copied."));
        copyTimer->start();
    });
    const auto updateText = [this, copyTimer] {
        ui.retranslateUi(this);
        setWindowTitle(qtr("About"));
        ui.title->setText(qtr("VLC media player"));
        ui.closeButton->setText(qtr("&Close"));
        ui.aboutTabs->setTabText(ui.aboutTabs->indexOf(ui.authorsPage), qtr("Authors"));
        ui.aboutTabs->setTabText(ui.aboutTabs->indexOf(ui.creditPage), qtr("Credits"));
        ui.aboutTabs->setTabText(ui.aboutTabs->indexOf(ui.licensePage), qtr("License"));
        ui.copyVersionButton->setToolTip(qtr("Copy this version to the clipboard."));
        ui.buildDetailsButton->setToolTip(qtr("Show information about this build."));
        ui.buildDetails->setText(qtr("Build user: %1\nBuild host: %2\nBuild date: %3 %4")
            .arg(qfu(VLC_CompileBy()), qfu(VLC_CompileHost()), QStringLiteral(__DATE__), QStringLiteral(__TIME__)));
        copyTimer->stop();
        ui.copyFeedback->clear();
        ui.MainBlabla->setHtml("<html><head/><body>" +
        qtr( "<p>VLC media player is a free and open source media player, encoder, and streamer made by the volunteers of the <a href=\"https://www.videolan.org/\"><span style=\" text-decoration: underline; color:#0057ae;\">VideoLAN</span></a> community.</p><p>VLC uses its internal codecs, works on essentially every popular platform, and can read almost all files, CDs, DVDs, network streams, capture cards and other media formats!</p><p><a href=\"https://www.videolan.org/contribute/\"><span style=\" text-decoration: underline; color:#0057ae;\">Help and join us!</span></a>" ).remove(QStringLiteral("color:#0057ae;")) +
        "</p>" + AboutModel::getDesignAttribution() + "</body> </html>");
        colorBrowserLinks(ui.MainBlabla, palette().color(QPalette::Link));
    };
    updateText();
    const auto metrics = [this] {
        const int margin = helpDp(p_intf, 24);
        layout()->setContentsMargins(margin,margin,margin,margin);
        layout()->setSpacing(helpDp(p_intf, 12));
        const auto today = QDate::currentDate();
        const bool april = today.month() == 4 && today.day() == 1 && var_InheritBool(p_intf,"qt-icon-change");
        QPixmap cone(april ? QStringLiteral(":/logo/vlc128-aprilfools.png") : QStringLiteral(":/logo/vlc256.png"));
        cone = cone.scaled(QSize(helpDp(p_intf, 64),helpDp(p_intf, 64)) * devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        cone.setDevicePixelRatio(devicePixelRatioF());
        ui.VLCcone->setPixmap(cone);
        ui.VLCcone->setFixedSize(helpDp(p_intf, 64),helpDp(p_intf, 64));
    };
    metrics();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, updateText);
    if (auto *ctx = p_intf->p_mi)
    {
        connect(ctx, &MainCtx::intfScaleFactorChanged, this, metrics);
        const auto links = [this] { colorBrowserLinks(ui.MainBlabla, palette().color(QPalette::Link)); };
        connect(ctx, &MainCtx::redesignAccentChanged, this, links);
        connect(ctx->getColorScheme(), &ColorSchemeModel::currentChanged, this, links);
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, links);
    }
    QVLCTools::restoreWidgetPosition(p_intf,"About",this,QSize(helpDp(p_intf, 720),helpDp(p_intf, 560)));
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    setTabOrder(ui.copyVersionButton, ui.closeButton);
    // Read-only text shortcuts belong to the dialog, before player shortcuts.
    for (auto *view : {static_cast<QWidget*>(ui.MainBlabla), static_cast<QWidget*>(ui.authorsPage),
                      static_cast<QWidget*>(ui.creditPage), static_cast<QWidget*>(ui.licensePage),
                      static_cast<QWidget*>(ui.version)}) view->installEventFilter(this);
}

AboutDialog::~AboutDialog()
{
    QVLCTools::saveWidgetPosition(p_intf,"About",this);
}

bool AboutDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent*>(event);
        const bool copy = key->matches(QKeySequence::Copy);
        const bool selectAll = key->matches(QKeySequence::SelectAll);
        if (copy || selectAll)
        {
            if (event->type() == QEvent::KeyPress)
            {
                if (auto *plain = qobject_cast<QPlainTextEdit*>(watched))
                {
                    if (copy) plain->copy(); else plain->selectAll();
                }
                else if (auto *rich = qobject_cast<QTextEdit*>(watched))
                {
                    if (copy) rich->copy(); else rich->selectAll();
                }
                else if (watched == ui.version)
                {
                    if (selectAll) ui.version->setSelection(0, ui.version->text().size());
                    else if (ui.version->hasSelectedText()) QGuiApplication::clipboard()->setText(ui.version->selectedText());
                }
            }
            key->accept();
            return true;
        }
    }
    return QVLCDialog::eventFilter(watched, event);
}

void AboutDialog::keyPressEvent(QKeyEvent *event)
{
    if (!activateHelpButton(event, focusWidget())) QVLCDialog::keyPressEvent(event);
}

#ifdef UPDATE_CHECK

class UpdateModelPrivate
{
public:
    GithubReleaseService* service = nullptr;
    std::optional<vlc::updates::Release> release;
    UpdateModel::Status status = UpdateModel::Unchecked;
    bool explicitCheck = false;
};

UpdateModel::UpdateModel(qt_intf_t * p_intf)
    : d_ptr(new UpdateModelPrivate)
{
    Q_UNUSED(p_intf);
    Q_D(UpdateModel);
    d->service = new GithubReleaseService(this);
    connect(d->service, &GithubReleaseService::finished, this, [this](bool success) {
        Q_D(UpdateModel);
        d->release = d->service->release();
        d->status = !success ? CheckFailed : d->release ? NeedUpdate : UpToDate;
        emit updateStatusChanged();
    });
}

UpdateModel::~UpdateModel() = default;

void UpdateModel::checkUpdate(bool explicitCheck)
{
    Q_D(UpdateModel);
    // A manual request during an automatic check should still show its result.
    if (explicitCheck && !d->explicitCheck)
    {
        d->explicitCheck = true;
        emit explicitCheckChanged();
    }
    if (d->status == Checking)
        return;
    d->release.reset();
    const auto installed = vlc::updates::Version::parse(QStringLiteral(PACKAGE_VERSION));
    d->status = installed ? Checking : CheckFailed;
    emit updateStatusChanged();
    if (installed)
        d->service->check(*installed, vlc::updates::Target::current());
}

bool UpdateModel::openReleasePage()
{
    Q_D(UpdateModel);
    return d->status == NeedUpdate && d->release &&
           QDesktopServices::openUrl(d->release->pageUrl);
}

// Keep the existing invokables for interface compatibility. GitHub discovery
// never hands an unsigned asset to the core's signed download/install backend.
bool UpdateModel::download(QString destDir)
{
    Q_UNUSED(destDir);
    return openReleasePage();
}

bool UpdateModel::download()
{
    return openReleasePage();
}

void UpdateModel::resetStatus()
{
    Q_D(UpdateModel);
    if (d->status == Unchecked || d->status == Checking)
        return;
    d->release.reset();
    d->status = Unchecked;
    emit updateStatusChanged();
}

UpdateModel::Status UpdateModel::updateStatus() const
{
    Q_D(const UpdateModel);
    return d->status;
}

int UpdateModel::getMajor() const
{
    Q_D(const UpdateModel);
    return d->release ? int(d->release->version.major) : 0;
}
int UpdateModel::getMinor() const
{
    Q_D(const UpdateModel);
    return d->release ? int(d->release->version.minor) : 0;
}
int UpdateModel::getRevision() const
{
    Q_D(const UpdateModel);
    return d->release ? int(d->release->version.patch) : 0;
}
int UpdateModel::getExtra() const
{
    return 0;
}
QString UpdateModel::getVersion() const
{
    Q_D(const UpdateModel);
    return d->release ? d->release->version.text : QString();
}
QString UpdateModel::getDescription() const
{
    Q_D(const UpdateModel);
    return d->release ? d->release->notes : QString();
}
QString UpdateModel::getUrl() const
{
    Q_D(const UpdateModel);
    return d->release ? d->release->pageUrl.toString() : QString();
}
QString UpdateModel::getAssetUrl() const
{
    Q_D(const UpdateModel);
    return d->release ? d->release->asset.url.toString() : QString();
}

double UpdateModel::getProgress() const
{
    return 0.0;
}

bool UpdateModel::explicitCheck() const
{
    Q_D(const UpdateModel);
    return d->explicitCheck;
}

void UpdateModel::resetExplicitCheck()
{
    Q_D(UpdateModel);
    if (!d->explicitCheck)
        return;
    d->explicitCheck = false;
    emit explicitCheckChanged();
}

/*****************************************************************************
 * UpdateDialog
 *****************************************************************************/

UpdateDialog::UpdateDialog( qt_intf_t *_p_intf ) : QVLCFrame( _p_intf )
{
    /* build Ui */
    ui.setupUi( this );
    ui.updateDialogButtonBox->addButton( new QPushButton( qtr("&Close"), this ),
                                         QDialogButtonBox::RejectRole );
    QPushButton *recheckButton = new QPushButton( qtr("&Recheck version"), this );
    ui.updateDialogButtonBox->addButton( recheckButton, QDialogButtonBox::ActionRole );

    ui.updateNotifyButtonBox->addButton( new QPushButton( qtr("View release on &GitHub"), this ),
                                         QDialogButtonBox::AcceptRole );
    ui.updateNotifyButtonBox->addButton( new QPushButton( qtr("&Dismiss"), this ),
                                         QDialogButtonBox::RejectRole );

    setWindowTitle( qtr( "VLC media player updates" ) );
    setWindowRole( "vlc-update" );

    BUTTONACT( recheckButton, &UpdateDialog::checkOrDownload );
    connect( ui.updateDialogButtonBox, &QDialogButtonBox::rejected, this, &UpdateDialog::close );

    connect( ui.updateNotifyButtonBox, &QDialogButtonBox::accepted, this, &UpdateDialog::checkOrDownload );
    connect( ui.updateNotifyButtonBox, &QDialogButtonBox::rejected, this, &UpdateDialog::close );

    setMinimumSize( 300, 300 );
    setMaximumSize( 500, 300 );

    restoreWidgetPosition( "Update", maximumSize() );

    m_model = p_intf->p_mi->getUpdateModel();
    connect(m_model, &UpdateModel::updateStatusChanged, this, &UpdateDialog::updateUI);
    /* update status*/
    updateUI();
}

UpdateDialog::~UpdateDialog()
{
    saveWidgetPosition( "Update" );
}

bool UpdateDialog::event(QEvent* event)
{
    assert(event);

    if (Q_LIKELY(m_model))
    {
        switch(event->type())
        {
        case QEvent::Hide:
        case QEvent::Show:
        case QEvent::Close:
        case QEvent::Destroy:
            m_model->resetExplicitCheck();
            break;
        default:
            break;
        }
    }

    return QVLCFrame::event(event);
}

/* Check for updates */
void UpdateDialog::checkOrDownload()
{
    switch (m_model->updateStatus()) {
    case UpdateModel::Unchecked:
    case UpdateModel::UpToDate:
    case UpdateModel::CheckFailed:
    {
        ui.stackedWidget->setCurrentWidget( ui.updateRequestPage );
        m_model->checkUpdate(true);
        break;
    }
    case UpdateModel::NeedUpdate:
    {
        if (m_model->openReleasePage())
            toggleVisible();
        break;
    }
    default: // Checking
        break;
    }
}

/* Show the result of release discovery. */
void UpdateDialog::updateUI( )
{
    switch (m_model->updateStatus()) {
    case UpdateModel::NeedUpdate:
    {
        ui.stackedWidget->setCurrentWidget( ui.updateNotifyPage );
        ui.updateNotifyLabel->setText(qtr("A new version of VLC (%1) is available.")
                                     .arg(m_model->getVersion()));
        ui.updateNotifyTextEdit->setPlainText(m_model->getDescription());
        break;
    }
    case UpdateModel::UpToDate:
    {
        ui.stackedWidget->setCurrentWidget( ui.updateDialogPage );
        ui.updateDialogLabel->setText(
            qtr("No newer compatible release is available for this build.") );
        break;
    }
    case UpdateModel::CheckFailed:
    {
        ui.stackedWidget->setCurrentWidget( ui.updateDialogPage );
        ui.updateDialogLabel->setText(
            qtr( "An error occurred while checking for updates..." ) );
        break;
    }
    case UpdateModel::Checking:
    {
        ui.stackedWidget->setCurrentWidget( ui.updateDialogPage );
        ui.updateDialogLabel->setText(
            qtr( "Checking for updates..." ) );
        break;
    }
    case UpdateModel::Unchecked:
        // do nothing
        break;
    case UpdateModel::Downloading:
        // NOTE: It is not planned to implement this in the legacy dialog.
        //       The new update pane already respects it, we are only
        //       waiting for the core to provide this information.
        break;
    }

    if (isVisible())
    {
        if (const auto w = windowHandle())
        {
            w->alert(0);
        }
    }
}

#endif
