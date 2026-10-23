/*****************************************************************************
 * Copyright (C) 2021 VLC authors and VideoLAN
 *
 * Authors: Benjamin Arnaud <bunjee@omega.gg>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "dialogmodel.hpp"

// VLC includes
#include <vlc_dialog.h>
#include <vlc_variables.h>
#include <vlc_log_observer.h>
#include "qt.hpp"
#include <QThread>
#include <QScopeGuard>

#include "maininterface/mainctx.hpp"

//=================================================================================================
// DialogErrorModel
//=================================================================================================

DialogErrorModel::DialogErrorModel(qt_intf_t * intf, QObject * parent)
    : QAbstractListModel(parent)
    , m_intf(intf)
{
    vlc_dialog_provider_set_error_callback(intf, &DialogErrorModel::onError, this);
    m_warningTimer.setInterval(250);
    connect(&m_warningTimer, &QTimer::timeout, this, &DialogErrorModel::flushWarnings);
    refreshNotificationPreference();
}

DialogErrorModel::~DialogErrorModel()
{
    stopWarningObserver();
    vlc_dialog_provider_set_error_callback(m_intf, nullptr, nullptr);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    assert(qGuiApp);
    qGuiApp->setBadgeNumber(0);
#endif
}

//-------------------------------------------------------------------------------------------------
// QAbstractItemModel implementation
//-------------------------------------------------------------------------------------------------

QVariant DialogErrorModel::data(const QModelIndex & index, int role) const /* override */
{
    int row = index.row();

    if (row < 0 || row >= m_data.count())
        return QVariant();

    switch (role)
    {
        case DIALOG_TITLE:
            return QVariant::fromValue(m_data.at(row).title);
        case DIALOG_TEXT:
            return QVariant::fromValue(m_data.at(row).text);
        case DIALOG_WARNING:
            return m_data.at(row).warning;
        default:
            return QVariant();
    }
}

int DialogErrorModel::rowCount(const QModelIndex &) const /* override */
{
    return count();
}

//-------------------------------------------------------------------------------------------------
// QAbstractItemModel reimplementation
//-------------------------------------------------------------------------------------------------

QHash<int, QByteArray> DialogErrorModel::roleNames() const /* override */
{
    return
    {
        { DialogErrorModel::DIALOG_TITLE, QByteArrayLiteral("title") },
        { DialogErrorModel::DIALOG_TEXT, QByteArrayLiteral("text") },
        { DialogErrorModel::DIALOG_WARNING, QByteArrayLiteral("warning") }
    };
}

//-------------------------------------------------------------------------------------------------
// Private functions
//-------------------------------------------------------------------------------------------------

void DialogErrorModel::onError(void * p_data,
                               const char * psz_title, const char * psz_text)
{
    auto model = static_cast<DialogErrorModel *>(p_data);

    DialogErrorModel::DialogError error { psz_title, psz_text };

    QMetaObject::invokeMethod(model, [model, error = std::move(error)]()
    {
        model->pushError(error);
    });
}

void DialogErrorModel::pushError(const QString & title, const QString& message)
{
    DialogError error;
    error.title = title;
    error.text = message;
    pushError(error);
}

void DialogErrorModel::pushError(const DialogError & error)
{
    appendHistory(error);

    if (!notificationsEnabled())
    {
        // Keep the error in history and in the log without showing a popup.
        msg_Err(m_intf, "%s: %s", qtu(error.title), qtu(error.text));
        emit countChanged();
        return;
    }

    if (!m_notificationWarning && lastNotificationText == error.title)
        repeatedNotificationCount += 1;
    else{
        lastNotificationText = error.title;
        repeatedNotificationCount = 1;
    }

    m_notificationWarning = false;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    assert(qGuiApp);

#if QT_VERSION >= QT_VERSION_CHECK(6, 13, 0)
    const qint64 badgeNumber = qGuiApp->badgeNumber() + 1;
#else
    const int badgeNumber = qGuiApp->property("badgeNumber").toInt() + 1;
    qGuiApp->setProperty("badgeNumber", badgeNumber);
#endif

    qGuiApp->setBadgeNumber(badgeNumber);
#endif

    emit countChanged();
    emit notificationChanged();
    emit notificationRequested();
}

void DialogErrorModel::appendHistory(const DialogError &error)
{
    // Bound optional warning history without discarding existing error details.
    if (error.warning && m_warningHistoryCount >= 200)
    {
        for (int row = 0; row < m_data.size(); ++row)
        {
            if (!m_data.at(row).warning) continue;
            beginRemoveRows({}, row, row);
            m_data.removeAt(row);
            endRemoveRows();
            --m_warningHistoryCount;
            break;
        }
    }
    const int row = m_data.size();
    beginInsertRows({}, row, row);
    m_data.append(error);
    endInsertRows();
    if (error.warning) ++m_warningHistoryCount;
}

void DialogErrorModel::onLog(void *data, int level, const vlc_log_t *context,
                             const char *format, va_list args)
{
    if (level != VLC_MSG_WARN) return;
    static_cast<DialogErrorModel *>(data)->m_warnings.append(context->psz_module, format, args);
}

void DialogErrorModel::flushWarnings()
{
    auto batch = m_warnings.take();
    if (!batch.count || !warningNotificationsEnabled()) return;
    const QString title = batch.module.isEmpty() ? qtr("Warning")
        : qtr("Warning: %1").arg(batch.module);
    const QString text = batch.count == 1 ? batch.text
        : qtr("%1\n%2 warnings. This entry shows the first warning.").arg(batch.text).arg(batch.count);
    appendHistory({title, text, true});
    emit countChanged();
    // A minor warning must not replace an active error notice.
    if (!m_notificationWarning && repeatedNotificationCount > 0) return;
    lastNotificationText = title + QStringLiteral(": ") + batch.text;
    repeatedNotificationCount = batch.count;
    m_notificationWarning = true;
    emit notificationChanged();
    emit notificationRequested();
}

void DialogErrorModel::stopWarningObserver()
{
    // Unsubscribe before locking the buffer: an in-flight producer can hold it.
    vlc_LogUnsubscribe(std::exchange(m_logSubscription, nullptr));
    m_warningTimer.stop();
    m_warnings.clear();
}

//-------------------------------------------------------------------------------------------------
// Properties
//-------------------------------------------------------------------------------------------------

bool DialogErrorModel::notificationsEnabled() const
{
    return var_InheritBool(m_intf, "qt-error-dialogs");
}

bool DialogErrorModel::warningNotificationsEnabled() const
{
    return var_InheritBool(m_intf, "qt-minor-warnings");
}

bool DialogErrorModel::notificationAllowed() const
{
    return m_notificationWarning ? warningNotificationsEnabled() : notificationsEnabled();
}

void DialogErrorModel::refreshNotificationPreference()
{
    if (warningNotificationsEnabled())
    {
        if (!m_logSubscription)
        {
            static const vlc_logger_operations ops = {onLog, nullptr};
            m_logSubscription = vlc_LogSubscribe(vlc_object_instance(m_intf), &ops, this);
            if (m_logSubscription) m_warningTimer.start();
            else msg_Err(m_intf, "Cannot read warning messages.");
        }
    }
    else stopWarningObserver();
    if (!notificationAllowed()) resetRepeatedMessageCount();
    emit notificationChanged();
    emit notificationsEnabledChanged();
}

int DialogErrorModel::count() const
{
    return m_data.count();
}

QString DialogErrorModel::notificationText() const
{
    return lastNotificationText;
}

int DialogErrorModel::repeatedMessageCount() const
{
    return repeatedNotificationCount;
}

void DialogErrorModel::resetRepeatedMessageCount()
{
   repeatedNotificationCount = 0;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Errors are dismissed, or error dialog is opened.
    assert(qGuiApp);
    qGuiApp->setBadgeNumber(0);
#if QT_VERSION < QT_VERSION_CHECK(6, 13, 0)
    qGuiApp->setProperty("badgeNumber", 0);
#endif
#endif
   emit notificationChanged();
}

//=================================================================================================
// DialogModel
//=================================================================================================

// Native callback strings only remain valid until their callback returns.
namespace {
void onDialogLogin(void *data, vlc_dialog_id *id, const char *title,
                   const char *text, const char *username, bool askStore)
{
    auto *model = static_cast<VLCDialogModel *>(data);
    model->dialogCallback(id, VLCDialogModel::DialogEvent::Request,
        [title = qfu(title), text = qfu(text), username = qfu(username), askStore]
        (VLCDialog *provider, DialogId handle) {
            emit provider->login(handle, title, text, username, askStore);
        });
}

void onDialogQuestion(void *data, vlc_dialog_id *id, const char *title,
                      const char *text, vlc_dialog_question_type type,
                      const char *cancel, const char *action1, const char *action2)
{
    auto *model = static_cast<VLCDialogModel *>(data);
    if (!model->shouldShowQuestion(type))
    {
        vlc_dialog_id_dismiss(id);
        return;
    }
    model->dialogCallback(id, VLCDialogModel::DialogEvent::Request,
        [title = qfu(title), text = qfu(text), type, cancel = qfu(cancel),
         action1 = qfu(action1), action2 = qfu(action2)]
        (VLCDialog *provider, DialogId handle) {
            emit provider->question(handle, title, text,
                static_cast<VLCDialog::QuestionType>(type), cancel, action1, action2);
        });
}

void onDialogProgress(void *data, vlc_dialog_id *id, const char *title,
                      const char *text, bool indeterminate, float position,
                      const char *cancel)
{
    auto *model = static_cast<VLCDialogModel *>(data);
    model->dialogCallback(id, VLCDialogModel::DialogEvent::Request,
        [title = qfu(title), text = qfu(text), indeterminate, position,
         cancel = qfu(cancel)](VLCDialog *provider, DialogId handle) {
            emit provider->progress(handle, title, text, indeterminate, position, cancel);
        });
}

void onDialogProgressUpdated(void *data, vlc_dialog_id *id, float value,
                            const char *text)
{
    auto *model = static_cast<VLCDialogModel *>(data);
    model->dialogCallback(id, VLCDialogModel::DialogEvent::Update,
        [value, text = qfu(text)](VLCDialog *provider, DialogId handle) {
            emit provider->progressUpdated(handle, value, text);
        });
}

void onDialogCancelled(void *data, vlc_dialog_id *id)
{
    auto *model = static_cast<VLCDialogModel *>(data);
    model->dialogCallback(id, VLCDialogModel::DialogEvent::Cancel,
        [](VLCDialog *provider, DialogId handle) { emit provider->cancelled(handle); });
}
}

VLCDialogModel::VLCDialogModel(qt_intf_t *intf, QObject *parent)
    : QObject(parent), m_intf(intf)
{
    const vlc_dialog_cbs callbacks = {
        onDialogLogin, onDialogQuestion, onDialogProgress,
        onDialogCancelled, onDialogProgressUpdated
    };
    vlc_dialog_provider_set_callbacks(intf, &callbacks, this);
}

VLCDialogModel::~VLCDialogModel()
{
    {
        QMutexLocker lock(&m_lock);
        m_shuttingDown = true;
        m_providerWait.wakeAll();
    }
    setProvider(nullptr);
    QList<DialogId> waiting;
    {
        QMutexLocker lock(&m_lock);
        for (const auto &request : m_dialogs) waiting.append(request.id);
        m_dialogs.clear();
    }
    for (const auto &id : waiting) vlc_dialog_id_dismiss(id.m_id);
    vlc_dialog_provider_set_callbacks(m_intf, nullptr, nullptr);
    QMutexLocker lock(&m_lock);
    while (m_pendingDialog > 0) m_pendingDialogCond.wait(&m_lock);
}

bool VLCDialogModel::shouldShowQuestion(vlc_dialog_question_type type) const
{
    return type == VLC_DIALOG_QUESTION_CRITICAL || var_InheritBool(m_intf, "qt-error-dialogs");
}

VLCDialog *VLCDialogModel::getProvider() const
{
    return m_provider.data();
}

bool VLCDialogModel::takeDialog(DialogId id, quint64 generation)
{
    QMutexLocker lock(&m_lock);
    auto request = m_dialogs.find(id.m_id);
    if (request == m_dialogs.end() || !(request->id == id)
        || request->providerGeneration != generation) return false;
    m_dialogs.erase(request);
    return true;
}

void VLCDialogModel::setProvider(VLCDialog *provider)
{
    Q_ASSERT(QThread::currentThread() == thread());
    Q_ASSERT(!provider || provider->thread() == thread());
    QPointer<VLCDialog> previous;
    QList<DialogId> abandoned;
    {
        QMutexLocker lock(&m_lock);
        if (m_providerIdentity == provider) return;
        previous = m_provider;
        const auto previousGeneration = m_providerGeneration;
        for (auto it = m_dialogs.begin(); it != m_dialogs.end(); )
        {
            if (it->providerGeneration && it->providerGeneration == previousGeneration)
            {
                abandoned.append(it->id);
                it = m_dialogs.erase(it);
            }
            else ++it;
        }
        if (previous) disconnect(previous, nullptr, this, nullptr);
        m_provider = provider;
        m_providerIdentity = provider;
        const auto generation = ++m_providerGeneration;
        if (provider)
        {
            connect(provider, &VLCDialog::post_login, this,
                [this, generation](DialogId id, const QString &user,
                                   const QString &password, bool store) {
                    if (takeDialog(id, generation))
                        vlc_dialog_id_post_login(id.m_id, qtu(user), qtu(password), store);
                });
            connect(provider, &VLCDialog::post_action1, this,
                [this, generation](DialogId id) {
                    if (takeDialog(id, generation)) vlc_dialog_id_post_action(id.m_id, 1);
                });
            connect(provider, &VLCDialog::post_action2, this,
                [this, generation](DialogId id) {
                    if (takeDialog(id, generation)) vlc_dialog_id_post_action(id.m_id, 2);
                });
            connect(provider, &VLCDialog::dismiss, this,
                [this, generation](DialogId id) {
                    if (takeDialog(id, generation)) vlc_dialog_id_dismiss(id.m_id);
                });
            connect(provider, &QObject::destroyed, this,
                [this, provider, generation] {
                    if (m_providerIdentity == provider && m_providerGeneration == generation)
                        setProvider(nullptr);
                });
        }
        m_providerWait.wakeAll();
    }
    // The model owns all native references, including requests whose initial
    // GUI delivery has not run. UI cleanup may safely send redundant replies.
    if (previous) emit previous->cancelAll();
    for (const auto &id : abandoned) vlc_dialog_id_dismiss(id.m_id);
    emit providerChanged();
}

void VLCDialogModel::dialogCallback(vlc_dialog_id *nativeId, DialogEvent event,
                                   std::function<void(VLCDialog*, DialogId)> callback)
{
    {
        QMutexLocker lock(&m_lock);
        ++m_pendingDialog;
    }
    const auto finished = qScopeGuard([this] {
        QMutexLocker lock(&m_lock);
        --m_pendingDialog;
        m_pendingDialogCond.wakeAll();
    });
    const bool onGuiThread = QThread::currentThread() == thread();
    DialogId handle;
    QPointer<VLCDialog> owner;
    quint64 generation = 0;
    bool dismiss = false;
    {
        QMutexLocker lock(&m_lock);
        if (event == DialogEvent::Request)
        {
            Q_ASSERT(!m_dialogs.contains(nativeId));
            handle = DialogId(nativeId, ++m_dialogSerial);
            m_dialogs.insert(nativeId, {handle, 0});
            while (!m_provider && !m_shuttingDown && !onGuiThread)
                m_providerWait.wait(&m_lock);
            auto request = m_dialogs.find(nativeId);
            if (request == m_dialogs.end() || !(request->id == handle)) return;
            if (m_shuttingDown || !m_provider)
            {
                m_dialogs.erase(request);
                dismiss = true;
            }
            else
            {
                owner = m_provider;
                generation = m_providerGeneration;
                request->providerGeneration = generation;
            }
        }
        else
        {
            auto request = m_dialogs.find(nativeId);
            if (request == m_dialogs.end()) return;
            handle = request->id;
            generation = request->providerGeneration;
            owner = m_provider;
            if (event == DialogEvent::Cancel)
            {
                m_dialogs.erase(request);
                dismiss = true;
            }
        }
    }
    if (dismiss) vlc_dialog_id_dismiss(nativeId);
    if (!owner) return;
    const auto deliver = [this, owner, generation, handle, event,
                          callback = std::move(callback)] {
        {
            QMutexLocker lock(&m_lock);
            if (m_shuttingDown || !owner || m_provider != owner
                || m_providerGeneration != generation) return;
            if (event != DialogEvent::Cancel)
            {
                const auto request = m_dialogs.constFind(handle.m_id);
                if (request == m_dialogs.cend() || !(request->id == handle)) return;
            }
        }
        callback(owner.data(), handle);
    };
    // Same-thread requests must be answerable before their native wait begins.
    // Worker requests reach QML only through this guarded GUI-thread delivery.
    if (onGuiThread) deliver();
    else QMetaObject::invokeMethod(this, deliver, Qt::QueuedConnection);
}
