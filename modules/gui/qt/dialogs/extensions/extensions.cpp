/*****************************************************************************
 * extensions.cpp: Extensions manager for Qt: dialogs manager
 ****************************************************************************
 * Copyright (C) 2009-2010 VideoLAN and authors
 *
 * Authors: Jean-Philippe André < jpeg # videolan.org >
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

#include "extensions.hpp"
#include "extensions_manager.hpp" // for isUnloading()
#include "maininterface/mainctx.hpp"
#include "util/redesign_widget_theme.hpp"
#include "util/interface_translator.hpp"
#include "util/color_scheme_model.hpp"
#include "widgets/native/qvlcframe.hpp"

#include <vlc_dialog.h>
#include <vlc_extensions.h>

#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextCursor>
#include <QApplication>
#include <QScreen>
#include <QPushButton>
#include <QSignalMapper>
#include <QLabel>
#include <QPixmap>
#include <QLineEdit>
#include <QTextBrowser>
#include <QCheckBox>
#include <QListWidget>
#include <QComboBox>
#include <QCloseEvent>
#include <QKeyEvent>
#include "widgets/native/customwidgets.hpp"

#include <algorithm>

namespace {
void colorExtensionLinks(QTextBrowser *browser)
{
    QList<QPair<int, int>> anchors;
    for (auto block = browser->document()->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
        {
            const auto fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isAnchor())
                anchors.append({fragment.position(), fragment.length()});
        }
    for (const auto &anchor : anchors)
    {
        QTextCursor cursor(browser->document());
        cursor.setPosition(anchor.first);
        cursor.setPosition(anchor.first + anchor.second, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setForeground(browser->palette().color(QPalette::Link));
        cursor.mergeCharFormat(format);
    }
}
}

static void DialogCallback( extension_dialog_t *p_ext_dialog,
                            void *p_data );


ExtensionsDialogProvider::ExtensionsDialogProvider(qt_intf_t *_p_intf)
        : QObject( NULL ), p_intf( _p_intf )
{
    assert(p_intf);
    vlc_dialog_provider_set_ext_callback( p_intf, DialogCallback, this );
}

ExtensionsDialogProvider::~ExtensionsDialogProvider()
{
    msg_Dbg( p_intf, "ExtensionsDialogProvider is quitting..." );
    for (ExtensionDialog* dialog: m_dialogs)
        delete dialog;
    vlc_dialog_provider_set_ext_callback( p_intf, nullptr, nullptr );
}

/** Create a dialog
 * Note: Lock on p_dialog->lock must be held. */
ExtensionDialog* ExtensionsDialogProvider::CreateExtDialog(
        extension_dialog_t *p_dialog )
{
    ExtensionDialog *dialog = new ExtensionDialog( p_intf,
                                                   p_dialog );
    m_dialogs.insert(dialog);
    return dialog;
}

/** Destroy a dialog
 * Note: Lock on p_dialog->lock must be held. */
int ExtensionsDialogProvider::DestroyExtDialog( extension_dialog_t *p_dialog )
{
    assert( p_dialog );
    ExtensionDialog *dialog = ( ExtensionDialog* ) p_dialog->p_sys_intf;
    if( !dialog )
        return VLC_EGENERIC;
    m_dialogs.erase(dialog);
    delete dialog;
    vlc_cond_signal( &p_dialog->cond );
    return VLC_SUCCESS;
}

/**
 * Update/Create/Destroy a dialog
 **/
void ExtensionsDialogProvider::UpdateExtDialog(extension_dialog_t *p_dialog )
{
    assert( p_dialog );

    ExtensionDialog *dialog = ( ExtensionDialog* ) p_dialog->p_sys_intf;
    if( p_dialog->b_kill && !dialog )
    {
        /* This extension could not be activated properly but tried
           to create a dialog. We must ignore it. */
        return;
    }

    vlc_mutex_lock( &p_dialog->lock );
    if( !p_dialog->b_kill )
    {
        if (!dialog)
        {
            dialog = CreateExtDialog( p_dialog );
        }
        dialog->has_lock = true;
        dialog->UpdateWidgets();
        dialog->has_lock = false;
        dialog->setVisible( !p_dialog->b_hide );
    }
    else
    {
        DestroyExtDialog( p_dialog );
    }
    vlc_cond_signal( &p_dialog->cond );
    vlc_mutex_unlock( &p_dialog->lock );
}

/**
 * Ask the dialogs provider to create a new dialog
 **/
static void DialogCallback( extension_dialog_t *p_ext_dialog,
                            void *p_data )
{
    (void) p_data;

    auto p_edp = static_cast<ExtensionsDialogProvider*>(p_data);
    if (!p_edp)
        return;

    //use auto connection here
    // * either we are in extension thread and this is safe to carry the extension_dialog_t
    //   through queued connection as the extension wait that we signal its condition variable
    // * either we are in Qt thread (during destruction for instance) and we want direct connection
    QMetaObject::invokeMethod(p_edp, [p_edp, p_ext_dialog]() {
        p_edp->UpdateExtDialog( p_ext_dialog );
    });

}


ExtensionDialog::ExtensionDialog( qt_intf_t *_p_intf,
                                  extension_dialog_t *_p_dialog )
    : QDialog( NULL )
    , p_intf( _p_intf )
    , p_dialog( _p_dialog )
    , has_lock(true)
{
    assert( p_dialog );

    msg_Dbg( p_intf, "Creating a new dialog: '%s'", p_dialog->psz_title );
    this->setWindowFlags( Qt::WindowMinMaxButtonsHint
                        | Qt::WindowCloseButtonHint );
    this->setWindowTitle( qfu( p_dialog->psz_title ) );

    setObjectName(QStringLiteral("redesignExtension"));
    setWindowRole("vlc-extension");
    QVLCDialog::setWindowTransientParent(this, nullptr, p_intf);
    rootLayout = new QVBoxLayout(this);
    heading = new QLabel(qfu(p_dialog->psz_title), this);
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    heading->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    heading->setProperty("redesignHeading", true);
    rootLayout->addWidget(heading);
    scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *body = new QWidget(scroll);
    layout = new QGridLayout(body);
    scroll->setWidget(body);
    rootLayout->addWidget(scroll, 1);
    auto *footer = new QHBoxLayout;
    footer->addStretch();
    auto *closeButton = new QPushButton(this);
    closeButton->setObjectName(QStringLiteral("extensionClose"));
    closeButton->setAutoDefault(false);
    footer->addWidget(closeButton);
    rootLayout->addLayout(footer);
    connect(closeButton, &QPushButton::clicked, this, &ExtensionDialog::close);
    const auto text = [closeButton] { closeButton->setText(qtr("&Close")); };
    text();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, text);
    if (p_intf->p_mi) connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, &ExtensionDialog::updateMetrics);
    updateMetrics();
    p_dialog->p_sys_intf = this;
}

void ExtensionDialog::updateMetrics()
{
    const auto dp = [this](int n) { return qRound(n * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.)); };
    const int margin = dp(24);
    rootLayout->setContentsMargins(margin, margin, margin, margin);
    rootLayout->setSpacing(dp(12));
    layout->setContentsMargins(dp(8), dp(8), dp(8), dp(8));
    layout->setHorizontalSpacing(dp(12));
    layout->setVerticalSpacing(dp(8));
    for (auto *list : findChildren<QListWidget*>()) list->setMinimumHeight(dp(100));
    for (auto *browser : findChildren<QTextBrowser*>()) browser->setMinimumHeight(dp(100));
    const auto available = screen() ? screen()->availableGeometry().size() : QSize(1280, 720);
    setMinimumSize(QSize(dp(320), dp(280)).boundedTo(available));
}

ExtensionDialog::~ExtensionDialog()
{
    msg_Dbg( p_intf, "Deleting extension dialog '%s'", qtu(windowTitle()) );
    p_dialog->p_sys_intf = nullptr;
    vlc_cond_signal( &p_dialog->cond );

    //we need to manually disconnect the objects as we are listenning to their destroyed
    //signals or they will be emitted after m_widgetMapping is destroyed and cause use after free
    for (QObject* obj: m_widgetMapping.keys())
        disconnect(obj, nullptr, this, nullptr);
}

QWidget* ExtensionDialog::CreateWidget( extension_widget_t *p_widget )
{
    assert( p_widget->p_sys_intf == NULL );

    switch( p_widget->type )
    {
        case EXTENSION_WIDGET_LABEL:
        {
            auto label = new QLabel( qfu( p_widget->psz_text ), this );
            p_widget->p_sys_intf = label;
            label->setTextFormat( Qt::RichText );
            label->setOpenExternalLinks( true );
            return label;
        }
        case EXTENSION_WIDGET_BUTTON:
        {
            auto button = new QPushButton( qfu( p_widget->psz_text ), this );
            button->setAutoDefault(false);
            button->setDefault(false);
            setWidgetMapping(button, p_widget);
            connect( button, &QPushButton::clicked,
                    this, &ExtensionDialog::TriggerClick );
            p_widget->p_sys_intf = button;
            return button;
        }
        case EXTENSION_WIDGET_IMAGE:
        {
            auto label = new QLabel( this );
            label->setPixmap( QPixmap( qfu( p_widget->psz_text ) ) );
            if( p_widget->i_width > 0 )
                label->setMaximumWidth( p_widget->i_width );
            if( p_widget->i_height > 0 )
                label->setMaximumHeight( p_widget->i_height );
            label->setScaledContents( true );
            p_widget->p_sys_intf = label;
            return label;
        }
        case EXTENSION_WIDGET_HTML:
        {
            auto textArea = new QTextBrowser( this );
            textArea->setOpenExternalLinks( true );
            textArea->setHtml( qfu( p_widget->psz_text ) );
            textArea->setProperty("extensionHtml", qfu(p_widget->psz_text));
            textArea->setTabChangesFocus(true);
            const auto links = [this, textArea] {
                auto colors = textArea->palette();
                colors.setColor(QPalette::Link, palette().color(QPalette::Link));
                colors.setColor(QPalette::LinkVisited, palette().color(QPalette::LinkVisited));
                textArea->setPalette(colors);
                colorExtensionLinks(textArea);
            };
            links();
            if (auto *ctx = p_intf->p_mi)
            {
                connect(ctx, &MainCtx::redesignAccentChanged, textArea, links);
                connect(ctx->getColorScheme(), &ColorSchemeModel::currentChanged, textArea, links);
                connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, textArea, links);
            }
            p_widget->p_sys_intf = textArea;
            return textArea;
        }
        case EXTENSION_WIDGET_TEXT_FIELD:
        {
            auto textInput = new QLineEdit( this );
            textInput->setText( qfu( p_widget->psz_text ) );
            textInput->setReadOnly( false );
            textInput->setEchoMode( QLineEdit::Normal );
            setWidgetMapping(textInput, p_widget);
            connect( textInput, &QLineEdit::textChanged,
                     this, &ExtensionDialog::SyncInput );
            p_widget->p_sys_intf = textInput;
            return textInput;
        }
        case EXTENSION_WIDGET_PASSWORD:
        {
            auto textInput = new QLineEdit( this );
            textInput->setText( qfu( p_widget->psz_text ) );
            textInput->setReadOnly( false );
            textInput->setEchoMode( QLineEdit::Password );
            setWidgetMapping(textInput, p_widget);
            /// @note: maybe it would be wiser to use textEdited here?
            connect( textInput, &QLineEdit::textChanged,
                    this, &ExtensionDialog::SyncInput );
            p_widget->p_sys_intf = textInput;
            return textInput;
        }
        case EXTENSION_WIDGET_CHECK_BOX:
        {
            auto checkBox = new QCheckBox( this );
            checkBox->setText( qfu( p_widget->psz_text ) );
            checkBox->setChecked( p_widget->b_checked );
            setWidgetMapping(checkBox, p_widget);
            connect( checkBox, &QtCheckboxChanged,
                     this, &ExtensionDialog::TriggerClick );
            p_widget->p_sys_intf = checkBox;
            return checkBox;
        }
        case EXTENSION_WIDGET_DROPDOWN:
        {
            auto comboBox = new QComboBox( this );
            comboBox->setEditable( false );
            p_widget->p_sys_intf = comboBox;
            populateDropdown(p_widget);
            setWidgetMapping(comboBox, p_widget);
            connect( comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     this, &ExtensionDialog::SyncSelection );
            return comboBox;
        }
        case EXTENSION_WIDGET_LIST:
        {
            auto list = new QListWidget( this );
            list->setSelectionMode( QAbstractItemView::ExtendedSelection );
            p_widget->p_sys_intf = list;
            populateList(p_widget);
            setWidgetMapping(list, p_widget);
            connect( list, &QListWidget::itemSelectionChanged,
                     this, &ExtensionDialog::SyncSelection );
            return list;
        }
        case EXTENSION_WIDGET_SPIN_ICON:
        {
            auto spinIcon = new SpinningIcon( this );
            spinIcon->play( p_widget->i_spin_loops );
            p_widget->p_sys_intf = spinIcon;
            return spinIcon;
        }
        default:
            msg_Err( p_intf, "Widget type %d unknown", p_widget->type );
            return NULL;
    }
}

/**
 * Forward click event to the extension
 * @param object A WidgetMapper, whose data() is the p_widget
 **/
int ExtensionDialog::TriggerClick()
{
    extension_widget_t *p_widget = getWidgetMapping(sender());
    assert(p_widget);

    int i_ret = VLC_EGENERIC;

    bool lockedHere = false;
    if( !has_lock )
    {
        vlc_mutex_lock( &p_dialog->lock );
        has_lock = true;
        lockedHere = true;
    }

    switch( p_widget->type )
    {
        case EXTENSION_WIDGET_BUTTON:
            i_ret = extension_WidgetClicked( p_dialog, p_widget );
            break;

        case EXTENSION_WIDGET_CHECK_BOX:
        {
            auto checkBox = static_cast< QCheckBox* >( p_widget->p_sys_intf );
            p_widget->b_checked = checkBox->isChecked();
            i_ret = VLC_SUCCESS;
            break;
        }
        default:
            msg_Dbg( p_intf, "A click event was triggered by a wrong widget" );
            break;
    }

    if( lockedHere )
    {
        vlc_mutex_unlock( &p_dialog->lock );
        has_lock = false;
    }

    return i_ret;
}

/**
 * Synchronize psz_text with the widget's text() value on update
 * @param object A WidgetMapper
 **/
void ExtensionDialog::SyncInput()
{
    extension_widget_t *p_widget = getWidgetMapping(sender());
    assert(p_widget);

    bool lockedHere = false;
    if( !has_lock )
    {
        vlc_mutex_lock( &p_dialog->lock );
        has_lock = true;
        lockedHere = true;
    }

    assert( p_widget->type == EXTENSION_WIDGET_TEXT_FIELD
            || p_widget->type == EXTENSION_WIDGET_PASSWORD );
    /* Synchronize psz_text with the new value */
    QLineEdit *widget = static_cast< QLineEdit* >( p_widget->p_sys_intf );
    char *psz_text = widget->text().isNull() ? NULL : strdup( qtu( widget->text() ) );
    free( p_widget->psz_text );
    p_widget->psz_text =  psz_text;

    if( lockedHere )
    {
        vlc_mutex_unlock( &p_dialog->lock );
        has_lock = false;
    }
}

/**
 * Synchronize parameter b_selected in the values list
 * @param object A WidgetMapper
 **/
void ExtensionDialog::SyncSelection()
{
    auto *widget = getWidgetMapping(sender());
    assert(widget);
    syncSelection(widget);
}

void ExtensionDialog::syncSelection(extension_widget_t *p_widget)
{
    struct extension_widget_t::extension_widget_value_t *p_value;
    bool lockedHere = false;
    if( !has_lock )
    {
        vlc_mutex_lock( &p_dialog->lock );
        has_lock = true;
        lockedHere = true;
    }

    assert( p_widget->type == EXTENSION_WIDGET_DROPDOWN
            || p_widget->type == EXTENSION_WIDGET_LIST );

    if( p_widget->type == EXTENSION_WIDGET_DROPDOWN )
    {
        QComboBox *combo = static_cast< QComboBox* >( p_widget->p_sys_intf );
        for( p_value = p_widget->p_values;
             p_value != NULL;
             p_value = p_value->p_next )
        {
//             if( !qstrcmp( p_value->psz_text, qtu( combo->currentText() ) ) )
            if( combo->itemData( combo->currentIndex(), Qt::UserRole ).toInt()
                == p_value->i_id )
            {
                p_value->b_selected = true;
            }
            else
            {
                p_value->b_selected = false;
            }
        }
        free( p_widget->psz_text );
        p_widget->psz_text = strdup( qtu( combo->currentText() ) );
    }
    else if( p_widget->type == EXTENSION_WIDGET_LIST )
    {
        QListWidget *list = static_cast<QListWidget*>( p_widget->p_sys_intf );
        QList<QListWidgetItem *> selection = list->selectedItems();
        for( p_value = p_widget->p_values;
             p_value != NULL;
             p_value = p_value->p_next )
        {
            bool b_selected = false;
            foreach( const QListWidgetItem *item, selection )
            {
//                 if( !qstrcmp( qtu( item->text() ), p_value->psz_text ) )
                if( item->data( Qt::UserRole ).toInt() == p_value->i_id )
                {
                    b_selected = true;
                    break;
                }
            }
            p_value->b_selected = b_selected;
        }
    }

    if( lockedHere )
    {
        vlc_mutex_unlock( &p_dialog->lock );
        has_lock = false;
    }
}

void ExtensionDialog::populateDropdown(extension_widget_t *widget)
{
    auto *combo = static_cast<QComboBox*>(widget->p_sys_intf);
    const QSignalBlocker blocker(combo);
    const QString requested = qfu(widget->psz_text);
    const QString previous = combo->currentText();
    combo->clear();
    int selected = -1;
    for (auto *value = widget->p_values; value; value = value->p_next)
    {
        combo->addItem(qfu(value->psz_text), value->i_id);
        if (value->b_selected) selected = combo->count() - 1;
    }
    if (selected < 0 || requested != previous)
    {
        const int requestedIndex = combo->findText(requested);
        if (requestedIndex >= 0) selected = requestedIndex;
    }
    combo->setCurrentIndex(selected >= 0 ? selected : (combo->count() ? 0 : -1));
    syncSelection(widget);
}

void ExtensionDialog::populateList(extension_widget_t *widget)
{
    auto *list = static_cast<QListWidget*>(widget->p_sys_intf);
    const QSignalBlocker blocker(list);
    const int position = list->verticalScrollBar()->value();
    list->clear();
    for (auto *value = widget->p_values; value; value = value->p_next)
    {
        auto *item = new QListWidgetItem(qfu(value->psz_text), list);
        item->setData(Qt::UserRole, value->i_id);
        item->setSelected(value->b_selected);
    }
    list->verticalScrollBar()->setValue(position);
}

void ExtensionDialog::UpdateWidgets()
{
    assert( p_dialog );
    vlc_mutex_assert(&p_dialog->lock);

    if( strcmp( qtu( windowTitle() ), p_dialog->psz_title ) != 0 )
    {
        setWindowTitle( qfu( p_dialog->psz_title ) );
        heading->setText(qfu(p_dialog->psz_title));
    }

    extension_widget_t *p_widget;
    ARRAY_FOREACH( p_widget, p_dialog->widgets )
    {
        if( !p_widget ) continue; /* Some widgets may be NULL at this point */
        QWidget *widget;
        int row = p_widget->i_row - 1;
        int col = p_widget->i_column - 1;
        if (p_widget->p_sys_intf && (row < 0 || col < 0))
        {
            const int index = layout->indexOf(static_cast<QWidget*>(p_widget->p_sys_intf));
            if (index >= 0)
            {
                int oldRow, oldColumn, oldRowSpan, oldColumnSpan;
                layout->getItemPosition(index, &oldRow, &oldColumn, &oldRowSpan, &oldColumnSpan);
                if (row < 0) row = oldRow;
                if (col < 0) col = oldColumn;
            }
        }
        if( row < 0 )
        {
            row = layout->rowCount();
            col = 0;
        }
        else if( col < 0 )
            col = layout->columnCount();
        int hsp = std::max( 1, p_widget->i_horiz_span );
        int vsp = std::max( 1, p_widget->i_vert_span );
        if( !p_widget->p_sys_intf && !p_widget->b_kill )
        {
            widget = CreateWidget( p_widget );
            if( !widget )
            {
                msg_Warn( p_intf, "Could not create a widget for dialog %s",
                          p_dialog->psz_title );
                continue;
            }
            widget->setVisible( !p_widget->b_hide );
            layout->addWidget( widget, row, col, vsp, hsp );
            if( ( p_widget->i_width > 0 ) && ( p_widget->i_height > 0 ) )
                widget->resize( p_widget->i_width, p_widget->i_height );
            p_widget->p_sys_intf = widget;
            /* If an update was required, cancel it as we just created the widget */
            p_widget->b_update = false;
        }
        else if( p_widget->p_sys_intf && !p_widget->b_kill
                 && p_widget->b_update )
        {
            widget = UpdateWidget( p_widget );
            if( !widget )
            {
                msg_Warn( p_intf, "Could not update a widget for dialog %s",
                          p_dialog->psz_title );
                return;
            }
            widget->setVisible( !p_widget->b_hide );
            layout->addWidget( widget, row, col, vsp, hsp );
            if( ( p_widget->i_width > 0 ) && ( p_widget->i_height > 0 ) )
                widget->resize( p_widget->i_width, p_widget->i_height );
            p_widget->p_sys_intf = widget;

            /* Do not update again */
            p_widget->b_update = false;
        }
        else if( p_widget->p_sys_intf && p_widget->b_kill )
        {
            DestroyWidget( p_widget );
            p_widget->p_sys_intf = NULL;
        }
    }
    updateMetrics();
    if (!initialSizeSet)
    {
        const auto scale = p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.;
        const auto available = screen() ? screen()->availableGeometry().size() : QSize(1280, 720);
        for (auto *widget : findChildren<QWidget*>()) widget->ensurePolished();
        const auto content = layout->sizeHint();
        const double width = p_dialog->i_width > 0 ? p_dialog->i_width * scale
            : qBound(480. * scale, content.width() + 64. * scale, 800. * scale);
        const double height = p_dialog->i_height > 0 ? p_dialog->i_height * scale
            : qBound(280. * scale, content.height() + heading->sizeHint().height() + 104. * scale, 680. * scale);
        const QSize preferred(qRound(qMin(width, double(available.width()))),
                              qRound(qMin(height, double(available.height()))));
        resize(preferred.boundedTo(available));
        initialSizeSet = true;
    }

}

QWidget* ExtensionDialog::UpdateWidget( extension_widget_t *p_widget )
{
    assert( p_widget->p_sys_intf != NULL );

    switch( p_widget->type )
    {
        case EXTENSION_WIDGET_LABEL:
        {
            auto label = static_cast< QLabel* >( p_widget->p_sys_intf );
            label->setText( qfu( p_widget->psz_text ) );
            return label;
        }
        case EXTENSION_WIDGET_BUTTON:
        {
            auto button = static_cast<QPushButton*>(p_widget->p_sys_intf);
            button->setText(qfu(p_widget->psz_text));
            return button;
        }
        case EXTENSION_WIDGET_IMAGE:
        {
            auto label = static_cast< QLabel* >( p_widget->p_sys_intf );
            label->setPixmap( QPixmap( qfu( p_widget->psz_text ) ) );
            return label;
        }
        case EXTENSION_WIDGET_HTML:
        {
            auto textArea = static_cast< QTextBrowser* >( p_widget->p_sys_intf );
            const QString text = qfu(p_widget->psz_text);
            if (textArea->property("extensionHtml").toString() != text)
            {
                const int position = textArea->verticalScrollBar()->value();
                textArea->setHtml(text);
                textArea->setProperty("extensionHtml", text);
                textArea->verticalScrollBar()->setValue(position);
                colorExtensionLinks(textArea);
            }
            return textArea;
        }
        case EXTENSION_WIDGET_TEXT_FIELD:
        {
            auto textInput = static_cast< QLineEdit* >( p_widget->p_sys_intf );
            if (textInput->text() != qfu(p_widget->psz_text)) textInput->setText(qfu(p_widget->psz_text));
            return textInput;
        }
        case EXTENSION_WIDGET_PASSWORD:
        {
            auto textInput = static_cast< QLineEdit* >( p_widget->p_sys_intf );
            if (textInput->text() != qfu(p_widget->psz_text)) textInput->setText(qfu(p_widget->psz_text));
            return textInput;
        }
        case EXTENSION_WIDGET_CHECK_BOX:
        {
            auto checkBox = static_cast< QCheckBox* >( p_widget->p_sys_intf );
            checkBox->setText( qfu( p_widget->psz_text ) );
            checkBox->setChecked( p_widget->b_checked );
            return checkBox;
        }
        case EXTENSION_WIDGET_DROPDOWN:
        {
            auto comboBox = static_cast< QComboBox* >( p_widget->p_sys_intf );
            populateDropdown(p_widget);
            return comboBox;
        }
        case EXTENSION_WIDGET_LIST:
        {
            auto list = static_cast< QListWidget* >( p_widget->p_sys_intf );
            populateList(p_widget);
            return list;
        }
        case EXTENSION_WIDGET_SPIN_ICON:
        {
            auto spinIcon = static_cast< SpinningIcon* >( p_widget->p_sys_intf );
            if( !spinIcon->isPlaying() && p_widget->i_spin_loops != 0 )
                spinIcon->play( p_widget->i_spin_loops );
            else if( spinIcon->isPlaying() && p_widget->i_spin_loops == 0 )
                spinIcon->stop();
            p_widget->i_height = p_widget->i_width = 16;
            return spinIcon;
        }
        default:
            msg_Err( p_intf, "Widget type %d unknown", p_widget->type );
            return NULL;
    }
}

void ExtensionDialog::DestroyWidget( extension_widget_t *p_widget,
                                     bool b_cond )
{
    assert( p_widget && p_widget->b_kill );
    QWidget *widget = static_cast< QWidget* >( p_widget->p_sys_intf );
    delete widget;
    p_widget->p_sys_intf = NULL;
    if( b_cond )
        vlc_cond_signal( &p_dialog->cond );
}

void ExtensionDialog::setWidgetMapping(QObject* object, extension_widget_t *ext_widget)
{
    if (!ext_widget) {
        m_widgetMapping.remove(object);
        return;
    }

    m_widgetMapping.insert(object, ext_widget);
    connect(object, &QWidget::destroyed, this, [this](QObject* obj){
        m_widgetMapping.remove(obj);
    });
}

extension_widget_t* ExtensionDialog::getWidgetMapping(QObject* obj) const
{
    return m_widgetMapping.value(obj, nullptr);
}

/** Implement closeEvent() in order to intercept the event */
void ExtensionDialog::closeEvent( QCloseEvent * )
{
    assert( p_dialog != NULL );
    msg_Dbg( p_intf, "Dialog '%s' received a closeEvent",
             p_dialog->psz_title );
    extension_DialogClosed( p_dialog );
}

/** Grab some keyboard input (ESC, ...) and handle actions manually */
void ExtensionDialog::keyPressEvent( QKeyEvent *event )
{
    assert( p_dialog != NULL );
    switch( event->key() )
    {
    case Qt::Key_Escape:
        close();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (auto *button = qobject_cast<QPushButton*>(focusWidget())) button->click();
        event->accept();
        return;
    default:
        QDialog::keyPressEvent( event );
        return;
    }
}
