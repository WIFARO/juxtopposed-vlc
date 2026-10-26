// SPDX-License-Identifier: GPL-2.0-or-later
#include "embedded_file_browser.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileSystemModel>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QScreen>
#include <QSet>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStyle>
#include <QTextBrowser>
#include <QTextDocument>
#include <QtMath>

namespace {
class FileWarningReader final : public QTextBrowser
{
public:
    using QTextBrowser::QTextBrowser;

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->matches(QKeySequence::Copy) || key->matches(QKeySequence::SelectAll)) {
                if (event->type() == QEvent::KeyPress) {
                    if (key->matches(QKeySequence::Copy)) copy(); else selectAll();
                }
                key->accept();
                return true;
            }
        }
        return QTextBrowser::event(event);
    }
};

class FileWarningTextFilter final : public QObject
{
public:
    explicit FileWarningTextFilter(QWidget* owner) : m_owner(owner)
    {
        qApp->installEventFilter(this);
    }

    ~FileWarningTextFilter() override
    {
        if (qApp) qApp->removeEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Polish || event->type() == QEvent::Show) {
            auto* message = qobject_cast<QMessageBox*>(watched);
            // These warnings are separate windows, directly parented to the
            // chooser. QWidget::isAncestorOf stops at window boundaries.
            if (message && m_owner && message->parentWidget() == m_owner) {
                if (message->textFormat() != Qt::PlainText)
                    message->setTextFormat(Qt::PlainText);
                if (event->type() == QEvent::Show) addReadingArea(message);
            }
        }
        return false;
    }

private:
    static void addReadingArea(QMessageBox* message)
    {
        if (message->findChild<QTextBrowser*>(QStringLiteral("fileValidationMessage"))) return;
        for (auto* button : message->buttons()) button->setIcon({});
        QLabel* label = nullptr;
        for (auto* candidate : message->findChildren<QLabel*>())
            if (candidate->text() == message->text()) { label = candidate; break; }
        if (!label || !message->layout()) return;

        // Keep Qt's validation result and buttons, with the same bounded plain
        // reading area used by other notices. Long filenames must not be cut.
        auto* body = new FileWarningReader(message);
        body->setObjectName(QStringLiteral("fileValidationMessage"));
        body->setAccessibleName(message->windowTitle());
        body->setPlainText(message->text());
        body->setOpenLinks(false);
        body->setUndoRedoEnabled(false);
        body->setTabChangesFocus(true);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        body->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        body->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; padding: 0; }"));
        body->document()->setDocumentMargin(0);
        body->ensurePolished();
        const auto available = message->screen()->availableGeometry().size();
        const int width = qMax(1, qMin(body->fontMetrics().averageCharWidth() * 36, available.width() * 2 / 3));
        body->setMinimumWidth(width);
        QTextDocument measured;
        measured.setDefaultFont(body->font());
        measured.setDefaultTextOption(body->document()->defaultTextOption());
        measured.setDocumentMargin(0);
        measured.setPlainText(message->text());
        measured.setTextWidth(qMax(1, width - body->style()->pixelMetric(QStyle::PM_ScrollBarExtent)));
        const int maximumHeight = qMax(1, available.height() / 2);
        const int minimumHeight = qMin(maximumHeight, body->fontMetrics().lineSpacing() * 2);
        body->setFixedHeight(qBound(minimumHeight, qCeil(measured.size().height()) + 2, maximumHeight));
        auto* previous = message->layout()->replaceWidget(label, body);
        if (!previous) { delete body; return; }
        delete previous;
        label->hide();
        body->show();
    }

    QPointer<QWidget> m_owner;
};
}

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
    const auto changed = [this] {
        m_explicitUrls.clear();
        m_selectionError = false;
        m_activationValidated = false;
        emit selectionChanged();
    };
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
        m_selectionError = false;
        m_activationValidated = false;
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

bool EmbeddedFileBrowser::validateSelection()
{
    if (m_activationValidated) return true;
    // Dropped URLs are lossless and may span directories or use other schemes.
    if (!m_explicitUrls.isEmpty()) return true;
    if (m_validating) return false;
    const QScopedValueRollback<bool> validating(m_validating, true);
    m_selectionAccepted = false;
    m_selectionError = false;
    const auto previousDirectory = directoryUrl();
    if (m_nameEdit && m_nameEdit->isModified()) {
        // Autocomplete can select only the existing subset of typed filenames.
        // Let Qt parse the full edited text instead of silently dropping a name.
        for (auto* view : findChildren<QAbstractItemView*>()) {
            if (!qobject_cast<QFileSystemModel*>(view->model())) continue;
            if (auto* selection = view->selectionModel()) {
                const QSignalBlocker blocker(selection);
                selection->clearSelection();
            }
        }
    }
    // Use the same filename parsing, existence checks and folder navigation for
    // the host buttons as for Return and activation in the file view.
    // Qt's validation warnings include filenames, which must remain literal.
    FileWarningTextFilter warningText(this);
    QFileDialog::accept();
    m_selectionError = !m_selectionAccepted && directoryUrl() == previousDirectory
        && m_nameEdit && !m_nameEdit->text().trimmed().isEmpty()
        && m_nameEdit->text() != QLatin1String("..");
    // Refresh the host's source list as well as its persistent error feedback.
    emit selectionChanged();
    return m_selectionAccepted;
}

void EmbeddedFileBrowser::accept()
{
    // Folder navigation belongs to the chooser even if another host field is
    // invalid. The host can reuse this validation during the synchronous action.
    if (!validateSelection()) return;
    const QScopedValueRollback<bool> activated(m_activationValidated, true);
    emit filesActivated();
}

void EmbeddedFileBrowser::done(int result)
{
    if (m_validating) {
        m_selectionAccepted = result == QDialog::Accepted;
        return;
    }
    // QFileDialog validates files and navigates folders before it reaches done.
    // The surrounding Open Media dialog decides when to close.
    if (result == QDialog::Accepted) emit filesActivated();
    else emit cancelRequested();
}
