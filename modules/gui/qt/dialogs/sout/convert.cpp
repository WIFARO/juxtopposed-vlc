/*****************************************************************************
 * convert.cpp : Conversion dialogs
 ****************************************************************************
 * Copyright (C) 2009 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb (at) videolan.org>
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

#include "dialogs/sout/convert.hpp"
#include "dialogs/sout/profile_selector.hpp"
#include "maininterface/mainctx.hpp"
#include "util/interface_translator.hpp"
#include "util/redesign_widget_theme.hpp"
#include "util/qt_dirs.hpp"

#include <QLabel>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QScrollArea>
#include <QTreeWidget>
#include <QHeaderView>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QScreen>
#include <QKeyEvent>

namespace {
QString pathKey(const QString &path)
{
    QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef _WIN32
    key = key.toCaseFolded();
#endif
    return key;
}
QString displaySource(const QString &mrl)
{
    return toNativeSeparators(QUrl(mrl).toDisplayString(QUrl::RemovePassword | QUrl::PreferLocalFile));
}
}

ConvertDialog::ConvertDialog(QWindow *parent, qt_intf_t *intf, const QStringList &inputs)
    : QVLCDialog(parent, intf), incomingMRLs(inputs), singleFileSelected(inputs.size() == 1)
{
    setObjectName(QStringLiteral("redesignConvert"));
    setWindowRole("vlc-convert");
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(this);
    heading->setProperty("redesignHeading", true);
    auto *help = new QLabel(this);
    help->setWordWrap(true);
    layout->addWidget(heading);
    layout->addWidget(help);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("conversionScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *body = new QVBoxLayout(content);
    body->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    const auto card = [body, content](const char *name) {
        auto *frame = new QFrame(content);
        frame->setObjectName(QLatin1String(name));
        frame->setProperty("redesignCard", true);
        auto *box = new QVBoxLayout(frame);
        body->addWidget(frame);
        return box;
    };
    auto *source = card("conversionSource");
    auto *sourceHeading = new QLabel(content);
    sourceHeading->setProperty("redesignSectionHeading", true);
    auto *sourceText = new QLabel(content);
    sourceText->setWordWrap(true);
    sourceText->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    source->addWidget(sourceHeading);
    source->addWidget(sourceText);
    auto *settings = card("conversionSettings");
    auto *settingsHeading = new QLabel(content);
    settingsHeading->setProperty("redesignSectionHeading", true);
    settings->addWidget(settingsHeading);
    auto *modes = new QHBoxLayout;
    auto *convertRadio = new QRadioButton(content);
    convertRadio->setObjectName(QStringLiteral("conversionConvertMode"));
    dumpRadio = new QRadioButton(content);
    dumpRadio->setObjectName(QStringLiteral("conversionRawMode"));
    auto *group = new QButtonGroup(this);
    group->addButton(convertRadio);
    group->addButton(dumpRadio);
    convertRadio->setChecked(true);
    modes->addWidget(convertRadio);
    modes->addWidget(dumpRadio);
    modes->addStretch();
    settings->addLayout(modes);
    auto *convertPanel = new QWidget(content);
    auto *convertLayout = new QVBoxLayout(convertPanel);
    convertLayout->setContentsMargins(0, 0, 0, 0);
    profile = new VLCProfileSelector(convertPanel);
    profile->setObjectName(QStringLiteral("conversionProfile"));
    profile->setThemeContext(p_intf->p_mi);
    previousExtension = profile->getMux();
    auto *profileBox = profile->findChild<QComboBox *>();
    if (profileBox)
    {
        profileBox->setObjectName(QStringLiteral("conversionProfileChoice"));
        profileBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        profileBox->setMinimumContentsLength(1);
        profileBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    convertLayout->addWidget(profile);
    displayBox = new QCheckBox(convertPanel);
    displayBox->setObjectName(QStringLiteral("conversionDisplay"));
    deinterBox = new QCheckBox(convertPanel);
    deinterBox->setObjectName(QStringLiteral("conversionDeinterlace"));
    convertLayout->addWidget(displayBox);
    convertLayout->addWidget(deinterBox);
    settings->addWidget(convertPanel);
    auto *destination = card("conversionDestination");
    auto *destHeading = new QLabel(content);
    destHeading->setProperty("redesignSectionHeading", true);
    auto *destLabel = new QLabel(content);
    destination->addWidget(destHeading);
    destination->addWidget(destLabel);
    auto *entry = new QHBoxLayout;
    fileLine = new QLineEdit(content);
    fileLine->setObjectName(QStringLiteral("conversionDestinationPath"));
    fileLine->setClearButtonEnabled(true);
    destLabel->setBuddy(fileLine);
    auto *browse = new QPushButton(content);
    browse->setObjectName(QStringLiteral("conversionBrowse"));
    entry->addWidget(fileLine, 1);
    entry->addWidget(browse);
    destination->addLayout(entry);
    appendBox = new QCheckBox(content);
    appendBox->setObjectName(QStringLiteral("conversionAppendName"));
    appendBox->setChecked(true);
    appendBox->setVisible(!singleFileSelected);
    destination->addWidget(appendBox);
    outputPreview = new QLabel(content);
    outputPreview->setObjectName(QStringLiteral("conversionOutputPreview"));
    outputPreview->setWordWrap(true);
    outputPreview->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    outputPreview->setVisible(singleFileSelected);
    destination->addWidget(outputPreview);
    batchPreview = new QTreeWidget(content);
    batchPreview->setObjectName(QStringLiteral("conversionBatchPreview"));
    batchPreview->setColumnCount(2);
    batchPreview->setRootIsDecorated(false);
    batchPreview->setTextElideMode(Qt::ElideMiddle);
    batchPreview->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    batchPreview->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    batchPreview->setVisible(!singleFileSelected);
    destination->addWidget(batchPreview);
    body->addStretch();
    feedback = new QLabel(this);
    feedback->setObjectName(QStringLiteral("conversionFeedback"));
    feedback->setWordWrap(true);
    layout->addWidget(feedback);
    auto *buttons = new QDialogButtonBox(this);
    okButton = new QPushButton(this);
    okButton->setObjectName(QStringLiteral("conversionStart"));
    okButton->setProperty("redesignPrimary", true);
    auto *cancel = new QPushButton(this);
    cancel->setObjectName(QStringLiteral("conversionCancel"));
    buttons->addButton(okButton, QDialogButtonBox::AcceptRole);
    buttons->addButton(cancel, QDialogButtonBox::RejectRole);
    for (auto *button : findChildren<QPushButton *>()) button->setAutoDefault(false);
    layout->addWidget(buttons);
    for (auto *label : {heading, help, sourceHeading, sourceText, settingsHeading, destHeading, destLabel, outputPreview, feedback})
        label->setTextFormat(Qt::PlainText);
    validationTimer = new QTimer(this);
    validationTimer->setSingleShot(true);
    validationTimer->setInterval(200);
    connect(validationTimer, &QTimer::timeout, this, &ConvertDialog::validate);
    connect(fileLine, &QLineEdit::textChanged, this, [this] { okButton->setEnabled(false); validationTimer->start(); });
    connect(fileLine, &QLineEdit::editingFinished, this, &ConvertDialog::setDestinationFileExtension);
    connect(profile, &VLCProfileSelector::optionsChanged, this, &ConvertDialog::profileChanged);
    connect(dumpRadio, &QRadioButton::toggled, this, [this, convertPanel](bool raw) { convertPanel->setEnabled(!raw); profileChanged(); });
    connect(appendBox, &QCheckBox::toggled, this, &ConvertDialog::validate);
    connect(browse, &QPushButton::clicked, this, &ConvertDialog::fileBrowse);
    connect(buttons, &QDialogButtonBox::accepted, this, &ConvertDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ConvertDialog::reject);
    const auto translate = [this, heading, help, sourceHeading, sourceText, settingsHeading, convertRadio, destHeading, destLabel, browse, cancel, profileBox] {
        setWindowTitle(qtr("Convert"));
        heading->setText(qtr("Convert media"));
        help->setText(qtr("Start replaces the current queue with these files."));
        sourceHeading->setText(qtr("Source"));
        sourceText->setText(singleFileSelected ? displaySource(incomingMRLs.first()) : qtr("%1 sources selected.").arg(incomingMRLs.size()));
        settingsHeading->setText(qtr("Output settings"));
        convertRadio->setText(qtr("Convert"));
        dumpRadio->setText(qtr("Save raw input"));
        dumpRadio->setToolTip(qtr("Save the source data without conversion."));
        displayBox->setText(qtr("Show output"));
        displayBox->setToolTip(qtr("Show the media during conversion. This can slow the conversion."));
        deinterBox->setText(qtr("Deinterlace"));
        destHeading->setText(qtr("Destination"));
        const auto name = singleFileSelected ? qtr("Output file") : qtr("Output folder");
        destLabel->setText(name);
        fileLine->setAccessibleName(name);
        fileLine->setPlaceholderText(singleFileSelected ? qtr("Enter a full file path") : qtr("Use each source folder"));
        appendBox->setText(qtr("Add '-converted' to file names"));
        browse->setText(qtr("Browse"));
        okButton->setText(qtr("&Start"));
        cancel->setText(qtr("&Cancel"));
        batchPreview->setHeaderLabels({qtr("Source"), qtr("Output file")});
        batchPreview->setAccessibleName(qtr("Output files"));
        if (profileBox) profileBox->setAccessibleName(qtr("Conversion profile"));
        validate();
    };
    translate();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, translate);
    applyRedesignWidgetTheme(this, p_intf->p_mi);
    const auto scale = [this, layout, body, source, settings, destination, entry, modes, convertLayout] {
        const auto dp = [this](int value) { return qRound(value * p_intf->p_mi->getIntfScaleFactor()); };
        layout->setContentsMargins(dp(24), dp(24), dp(24), dp(24));
        layout->setSpacing(dp(12));
        body->setSpacing(dp(16));
        for (auto *box : {source, settings, destination}) { box->setContentsMargins(dp(16), dp(16), dp(16), dp(16)); box->setSpacing(dp(8)); }
        entry->setSpacing(dp(8));
        modes->setSpacing(dp(12));
        convertLayout->setSpacing(dp(8));
        batchPreview->setFixedHeight(dp(140));
    };
    scale();
    connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, scale);
    const double factor = p_intf->p_mi->getIntfScaleFactor();
    QSize initial(qRound(760 * factor), qRound(720 * factor));
    if (screen()) initial = initial.boundedTo(screen()->availableGeometry().size() - QSize(32, 32));
    resize(initial);
    fileLine->setFocus();
}

QString ConvertDialog::outputPathText() const
{
    const auto text = fileLine->text();
    const QUrl url(text);
    return url.isLocalFile() ? url.toLocalFile() : QDir::fromNativeSeparators(text);
}

QString ConvertDialog::extensionFor(const QString &input) const
{
    if (!dumpRadio->isChecked()) return profile->getMux();
    const auto url = QUrl(input);
    const auto suffix = QFileInfo(url.isLocalFile() ? url.toLocalFile() : url.path()).suffix();
    return suffix.isEmpty() ? QStringLiteral("bin") : suffix;
}

void ConvertDialog::profileChanged()
{
    const auto nextMux = extensionFor(incomingMRLs.value(0));
    if (nextMux.isEmpty())
    {
        validate();
        return;
    }
    if (singleFileSelected && !previousExtension.isEmpty())
    {
        auto path = outputPathText();
        if (QFileInfo(path).suffix().compare(previousExtension, Qt::CaseInsensitive) == 0)
        {
            path.chop(previousExtension.size());
            const QSignalBlocker block(fileLine);
            fileLine->setText(toNativeSeparators(path + nextMux));
        }
    }
    previousExtension = nextMux;
    setDestinationFileExtension();
}

void ConvertDialog::setDestinationFileExtension()
{
    if (singleFileSelected && !fileLine->text().isEmpty())
    {
        QString path = outputPathText();
        const QFileInfo info(path);
        if (!info.fileName().isEmpty() && !info.isDir() && info.suffix().isEmpty())
        {
            const auto ext = extensionFor(incomingMRLs.first());
            if (!ext.isEmpty())
            {
                const QSignalBlocker block(fileLine);
                fileLine->setText(toNativeSeparators(path + QLatin1Char('.') + ext));
            }
        }
    }
    validate();
}

void ConvertDialog::fileBrowse()
{
    QUrl initial = fileLine->text().isEmpty() ? p_intf->p_mi->getDialogFilePath() : QUrl::fromLocalFile(outputPathText());
    QUrl chosen;
    if (singleFileSelected)
    {
        const auto ext = extensionFor(incomingMRLs.first());
        chosen = QFileDialog::getSaveFileUrl(this, qtr("Select output file"), initial,
            qtr("Output files (*.%1);;All files (*)").arg(ext), nullptr, QFileDialog::DontConfirmOverwrite, {QStringLiteral("file")});
    }
    else chosen = QFileDialog::getExistingDirectoryUrl(this, qtr("Select output folder"), initial, QFileDialog::ShowDirsOnly, {QStringLiteral("file")});
    if (chosen.isEmpty()) return;
    fileLine->setText(toNativeSeparators(chosen.toLocalFile()));
    setDestinationFileExtension();
}

void ConvertDialog::validate()
{
    validationTimer->stop();
    plannedPaths.clear();
    batchPreview->clear();
    QString error;
    const auto fail = [&error](const QString &message) { if (error.isEmpty()) error = message; };
    if (incomingMRLs.isEmpty()) fail(qtr("Select at least one source file."));
    auto *choices = profile->findChild<QComboBox *>();
    if (!dumpRadio->isChecked() && (profile->getMux().isEmpty() || !choices || choices->count() == 0))
        fail(qtr("Select a conversion profile."));
    const QString entered = outputPathText();
    if ((singleFileSelected || !entered.isEmpty()) && !QFileInfo(entered).isAbsolute())
        fail(qtr("Enter a full output path or select Browse."));
    if (entered.contains(QChar::Null) || entered.contains(QLatin1Char('\n')) || entered.contains(QLatin1Char('\r')))
        fail(qtr("Enter a valid output path."));
    QSet<QString> sources, outputs;
    for (const auto &mrl : incomingMRLs)
    {
        const QUrl url(mrl);
        if (url.isLocalFile())
        {
            sources.insert(pathKey(url.toLocalFile()));
            const auto canonical = QFileInfo(url.toLocalFile()).canonicalFilePath();
            if (!canonical.isEmpty()) sources.insert(pathKey(canonical));
        }
    }
    for (int i = 0; i < incomingMRLs.size(); ++i)
    {
        const QUrl input(incomingMRLs[i]);
        QString target;
        if (singleFileSelected)
        {
            target = entered;
            QFileInfo info(target);
            if (!info.fileName().isEmpty() && !info.isDir() && info.suffix().isEmpty())
                target += QLatin1Char('.') + extensionFor(incomingMRLs[i]);
            if (entered.isEmpty() || entered.endsWith(QLatin1Char('/'))) fail(qtr("Choose an output file."));
        }
        else
        {
            QString folder = entered;
            if (folder.isEmpty())
            {
                if (!input.isLocalFile()) fail(qtr("Select an output folder for network sources."));
                else folder = QFileInfo(input.toLocalFile()).absolutePath();
            }
            const QFileInfo info(input.isLocalFile() ? input.toLocalFile() : input.path());
            QString base = info.completeBaseName();
            if (base.isEmpty()) base = info.fileName();
            if (base.isEmpty()) base = QStringLiteral("source-%1").arg(i + 1);
            if (appendBox->isChecked()) base += QStringLiteral("-converted");
            target = QDir(folder).filePath(base + QLatin1Char('.') + extensionFor(incomingMRLs[i]));
        }
        target = QDir::cleanPath(target);
        plannedPaths << target;
        const QFileInfo info(target);
        const auto key = pathKey(target);
        const auto canonical = info.canonicalFilePath();
        if (sources.contains(key) || (!canonical.isEmpty() && sources.contains(pathKey(canonical))))
            fail(qtr("An output file is also a source file. Change the output folder or file name."));
        else if (info.isDir())
            fail(qtr("Choose a file name, not a folder."));
        else if (info.exists() || info.isSymLink())
            fail(qtr("An output file already exists. Choose another folder or file name."));
        if (outputs.contains(key)) fail(qtr("Two sources have the same output path. Use separate output folders."));
        outputs.insert(key);
        const QFileInfo folder(info.absolutePath());
        if (!folder.isDir() || !folder.isWritable()) fail(qtr("Select an output folder that you can write to."));
        if (!singleFileSelected)
        {
            auto *row = new QTreeWidgetItem(batchPreview, {displaySource(incomingMRLs[i]), toNativeSeparators(target)});
            for (int column = 0; column < 2; ++column)
                row->setToolTip(column, QStringLiteral("<qt>%1</qt>").arg(row->text(column).toHtmlEscaped()));
        }
    }
    outputPreview->setText(singleFileSelected && !entered.isEmpty() && !plannedPaths.isEmpty() ? toNativeSeparators(plannedPaths.first()) : QString());
    feedback->setText(error.isEmpty() ? qtr("Ready to start.") : error);
    feedback->setAccessibleName(qtr("Output status"));
    fileLine->setAccessibleDescription(error);
    okButton->setEnabled(error.isEmpty() && !plannedPaths.isEmpty());
}

void ConvertDialog::accept()
{
    setDestinationFileExtension();
    if (!okButton->isEnabled()) return;
    outputOptions.clear();
    for (const auto &path : plannedPaths)
    {
        if (dumpRadio->isChecked())
            outputOptions.append({QStringLiteral("demux=dump"), QStringLiteral("demuxdump-file=") + path,
                                  QStringLiteral("no-sout-file-overwrite"), QStringLiteral("no-demuxdump-append")});
        else
        {
            SoutChain chain = profile->getTranscode();
            if (deinterBox->isChecked()) chain.option(QStringLiteral("deinterlace"));
            SoutModule destination(QStringLiteral("std"));
            SoutModule access(QStringLiteral("file"));
            access.option(QStringLiteral("no-overwrite"));
            destination.option(QStringLiteral("access"), access);
            destination.option(QStringLiteral("mux"), profile->getMux(), false);
            char *escaped = config_StringEscape(qtu(path));
            if (!escaped)
            {
                feedback->setText(qtr("Cannot prepare the output options."));
                return;
            }
            const QString quoted = QStringLiteral("'") + qfu(escaped) + QLatin1Char('\'');
            free(escaped);
            destination.option(QStringLiteral("dst"), quoted, true);
            if (displayBox->isChecked())
            {
                SoutModule duplicate(QStringLiteral("duplicate"));
                duplicate.option(QStringLiteral("dst"), QStringLiteral("display"), false);
                duplicate.option(QStringLiteral("dst"), destination);
                chain.module(duplicate);
            }
            else chain.module(destination);
            outputOptions.append({QStringLiteral("sout=#") + chain.to_string()});
        }
    }
    QVLCDialog::accept();
}

void ConvertDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        if (auto *button = qobject_cast<QPushButton *>(focusWidget()))
            if (button->isEnabled()) button->click();
        event->accept();
        return;
    }
    QVLCDialog::keyPressEvent(event);
}
