// SPDX-License-Identifier: GPL-2.0-or-later
#include "embedded_file_browser.hpp"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFileSystemModel>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QSet>
#include <QSignalBlocker>

EmbeddedFileBrowser::EmbeddedFileBrowser(QWidget* parent)
    : QFileDialog(parent, Qt::Widget)
{
    // Set this before other properties so all platforms create widget controls.
    setOption(QFileDialog::DontUseNativeDialog);
    setOption(QFileDialog::DontUseCustomDirectoryIcons);
    setOption(QFileDialog::ReadOnly);
    setFileMode(QFileDialog::ExistingFiles);
    setAcceptMode(QFileDialog::AcceptOpen);
    setWindowFlags(Qt::Widget);
    setWindowModality(Qt::NonModal);
    setSizeGripEnabled(false);
    setObjectName(QStringLiteral("embeddedFileBrowser"));
    setMinimumHeight(250);
    layout()->setContentsMargins(0, 0, 0, 0);
    layout()->setSizeConstraint(QLayout::SetNoConstraint);

    for (auto* buttons : findChildren<QDialogButtonBox*>()) buttons->hide();
    // Use the public label/buddy relation rather than the order of Qt's edits.
    for (auto* label : findChildren<QLabel*>()) {
        if (label->text() == labelText(QFileDialog::FileName)) {
            m_nameEdit = qobject_cast<QLineEdit*>(label->buddy());
            if (m_nameEdit) break;
        }
    }
    const auto changed = [this] { m_explicitUrls.clear(); emit selectionChanged(); };
    if (m_nameEdit) connect(m_nameEdit, &QLineEdit::textChanged, this, changed);

    QSet<QItemSelectionModel*> selections;
    for (auto* view : findChildren<QAbstractItemView*>()) {
        if (!qobject_cast<QFileSystemModel*>(view->model())) continue;
        auto* selection = view->selectionModel();
        if (selection && !selections.contains(selection)) {
            selections.insert(selection);
            connect(selection, &QItemSelectionModel::selectionChanged,
                    this, changed);
        }
    }
    connect(this, &QFileDialog::directoryUrlEntered, this, changed);
}

void EmbeddedFileBrowser::clearSelection()
{
    {
        const QSignalBlocker blocker(this);
        for (auto* view : findChildren<QAbstractItemView*>()) {
            if (qobject_cast<QFileSystemModel*>(view->model()) && view->selectionModel()) {
                const QSignalBlocker selectionBlocker(view->selectionModel());
                view->selectionModel()->clear();
            }
        }
        if (m_nameEdit) {
            const QSignalBlocker editBlocker(m_nameEdit);
            m_nameEdit->clear();
        }
        m_explicitUrls.clear();
    }
    emit selectionChanged();
}

void EmbeddedFileBrowser::selectUrls(const QList<QUrl>& urls)
{
    clearSelection();
    QStringList names;
    for (const auto& url : urls)
        names.append(QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(
            url.toDisplayString(QUrl::PreferLocalFile | QUrl::RemovePassword))));
    if (m_nameEdit) m_nameEdit->setText(names.join(QLatin1Char(' ')));
    // Keep dropped URLs lossless: Qt's editable filename syntax cannot express
    // filenames containing quotes, and a drop can span multiple directories.
    m_explicitUrls = urls;
    emit selectionChanged();
}

QList<QUrl> EmbeddedFileBrowser::selectedMediaUrls() const
{
    if (!m_explicitUrls.isEmpty()) return m_explicitUrls;
    // A blank filename must not leave the host with a stale source selection.
    if (m_nameEdit && m_nameEdit->text().trimmed().isEmpty()) return {};
    return selectedUrls();
}

void EmbeddedFileBrowser::accept()
{
    if (!m_explicitUrls.isEmpty()) emit filesActivated();
    else QFileDialog::accept();
}

void EmbeddedFileBrowser::done(int result)
{
    // QFileDialog validates files and navigates folders before it reaches done.
    // The surrounding Open Media dialog decides when to close.
    if (result == QDialog::Accepted) emit filesActivated();
    else emit cancelRequested();
}
