/*****************************************************************************
 * info_panels.cpp : Panels for the information dialogs
 ****************************************************************************
 * Copyright (C) 2006-2007 the VideoLAN team
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *          Ilkka Ollakka <ileoo@videolan.org>
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
#include "info_panels.hpp"
#include "player/player_controller.hpp"
#include "maininterface/mainctx.hpp"
#include "widgets/native/interface_widgets.hpp"
#include "info_widgets.hpp"
#include "dialogs/fingerprint/fingerprintdialog.hpp"
#include "dialogs/fingerprint/chromaprint.hpp"

#include <cassert>
#include <vlc_url.h>
#include <vlc_meta.h>
#include <vlc_input_item.h>
#include <vlc_list.hpp>

#include "util/interface_translator.hpp"

#include <QTreeWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QStringList>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QLineEdit>
#include <QLabel>
#include <QTextEdit>
#include <QApplication>
#include <QPushButton>
#include <QRegularExpression>
#include <QMessageBox>
#include <QEvent>

namespace {
QLabel* translatedLabel(const char* source, QWidget* parent)
{
    auto* label = new QLabel(qtr(source), parent);
    if (auto* translator = InterfaceTranslator::instance())
        QObject::connect(translator, &InterfaceTranslator::languageChanged, label,
                         [label, source] { label->setText(qtr(source)); });
    return label;
}
}

/************************************************************************
 * Single panels
 ************************************************************************/

/**
 * First Panel - Meta Info
 * All the usual MetaData are displayed and can be changed.
 **/
MetaPanel::MetaPanel( QWidget *parent,
                      qt_intf_t *_p_intf )
                      : QWidget( parent ), p_intf( _p_intf )
{
    setObjectName( "metadataForm" );
    metaLayout = new QGridLayout( this );
    const auto addField = [this](const char *source, const char *name, QLineEdit *&field) {
        auto *block = new QWidget( this );
        auto *layout = new QVBoxLayout( block );
        layout->setContentsMargins( 0, 0, 0, 0 );
        auto *label = translatedLabel( source, block );
        field = new QLineEdit( block );
        field->setObjectName( QLatin1String(name) );
        label->setBuddy( field );
        label->setWordWrap( true );
        layout->addWidget( label );
        layout->addWidget( field );
        connect( field, &QLineEdit::textEdited, this, &MetaPanel::enterEditMode );
        return block;
    };
    const auto field = [&](vlc_meta_type_t type, const char *name, QLineEdit *&edit) {
        return addField(vlc_meta_TypeToString(type), name, edit);
    };
    auto *title = field( vlc_meta_Title, "metadataTitle", title_text );
    auto *artist = field( vlc_meta_Artist, "metadataArtist", artist_text );
    auto *album = field( vlc_meta_Album, "metadataAlbum", collection_text );
    auto *date = field( vlc_meta_Date, "metadataDate", date_text );
    // Tags can contain a year or a full release date. A year mask loses data
    // even when the user only changes a different field.
    auto *genre = field( vlc_meta_Genre, "metadataGenre", genre_text );

    auto *track = new QWidget( this );
    auto *trackLayout = new QVBoxLayout( track );
    trackLayout->setContentsMargins( 0, 0, 0, 0 );
    auto *trackLabel = translatedLabel( vlc_meta_TypeToString(vlc_meta_TrackNumber), track );
    auto *trackFields = new QHBoxLayout;
    seqnum_text = new QLineEdit( track );
    seqnum_text->setObjectName( "metadataTrackNumber" );
    seqtot_text = new QLineEdit( track );
    seqtot_text->setObjectName( "metadataTrackTotal" );
    trackLabel->setBuddy( seqnum_text );
    trackFields->addWidget( seqnum_text );
    trackFields->addWidget( new QLabel( QStringLiteral("/"), track ) );
    trackFields->addWidget( seqtot_text );
    trackLayout->addWidget( trackLabel );
    trackLayout->addLayout( trackFields );
    connect( seqnum_text, &QLineEdit::textEdited, this, &MetaPanel::enterEditMode );
    connect( seqtot_text, &QLineEdit::textEdited, this, &MetaPanel::enterEditMode );

    auto *language = field( vlc_meta_Language, "metadataLanguage", language_text );
    auto *publisher = field( vlc_meta_Publisher, "metadataPublisher", publisher_text );
    auto *copyright = field( vlc_meta_Copyright, "metadataCopyright", copyright_text );
    auto *encodedBy = field( vlc_meta_EncodedBy, "metadataEncodedBy", encodedby_text );
    auto *nowPlaying = field( vlc_meta_NowPlaying, "metadataNowPlaying", nowplaying_text );
    nowplaying_text->setReadOnly( true );

    commentsBlock = new QWidget( this );
    auto *commentsLayout = new QVBoxLayout( commentsBlock );
    commentsLayout->setContentsMargins( 0, 0, 0, 0 );
    auto *commentsLabel = translatedLabel( N_("Comments"), commentsBlock );
    description_text = new QTextEdit( commentsBlock );
    description_text->setObjectName( "metadataComments" );
    description_text->setAcceptRichText( false );
    commentsLabel->setBuddy( description_text );
    commentsLayout->addWidget( commentsLabel );
    commentsLayout->addWidget( description_text );
    connect( description_text, &QTextEdit::textChanged, this, &MetaPanel::enterEditMode );

    auto *cover = new QWidget( this );
    auto *coverLayout = new QVBoxLayout( cover );
    coverLayout->setContentsMargins( 0, 0, 0, 0 );
    coverLayout->addWidget( translatedLabel(N_("Cover art"), cover) );
    art_cover = new CoverArtLabel( cover, p_intf );
    art_cover->setObjectName( "metadataCover" );
    coverLayout->addWidget( art_cover, 0, Qt::AlignHCenter );
    changeCoverButton = new QPushButton( qtr("Change cover"), cover );
    changeCoverButton->setObjectName( "metadataChangeCover" );
    coverLayout->addWidget( changeCoverButton, 0, Qt::AlignHCenter );
    connect( changeCoverButton, &QPushButton::clicked, art_cover, &CoverArtLabel::setArtFromFile );
    fingerprintButton = new QPushButton( qtr("Find tags"), cover );
    fingerprintButton->setObjectName( "metadataFingerprint" );
    fingerprintButton->setToolTip( qtr("Find tags for this audio file.") );
    fingerprintButton->setEnabled( false );
    coverLayout->addWidget( fingerprintButton, 0, Qt::AlignHCenter );
    coverLayout->addStretch();
    connect( fingerprintButton, &QPushButton::clicked, this, &MetaPanel::fingerprint );
    connect( art_cover, &CoverArtLabel::editing, this, &MetaPanel::enterEditMode );
    connect( art_cover, &CoverArtLabel::artSelected, this, [this](const QString &url) { currentURL = url; } );

    fieldRows = {{title, artist}, {album, date}, {genre, track}, {language, publisher},
                 {copyright, encodedBy}, {nowPlaying, nullptr}, {commentsBlock, cover}};
    const auto translate = [this] {
        seqtot_text->setAccessibleName( qtr(vlc_meta_TypeToString(vlc_meta_TrackTotal)) );
        changeCoverButton->setText( qtr("Change cover") );
        fingerprintButton->setText( qtr("Find tags") );
        fingerprintButton->setToolTip( qtr("Find tags for this audio file.") );
    };
    translate();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, translate);
    if (p_intf->p_mi)
        connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, &MetaPanel::updateLayout);
    updateLayout();
    setEnabled( false );
}

void MetaPanel::resizeEvent( QResizeEvent *event )
{
    QWidget::resizeEvent( event );
    updateLayout();
}

void MetaPanel::updateLayout()
{
    const auto dp = [this](int value) { return qRound(value * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.)); };
    const bool compact = width() < dp(560);
    while (auto *item = metaLayout->takeAt(0)) delete item;
    for (int row = 0; row < metaLayout->rowCount(); ++row) metaLayout->setRowStretch(row, 0);
    metaLayout->setContentsMargins(0, dp(16), 0, 0);
    metaLayout->setHorizontalSpacing(dp(16));
    metaLayout->setVerticalSpacing(dp(16));
    metaLayout->setColumnStretch(0, compact ? 1 : 3);
    metaLayout->setColumnStretch(1, compact ? 0 : 2);
    int row = 0;
    for (const auto &fields : fieldRows)
    {
        if (!fields.first->isHidden())
        {
            metaLayout->addWidget(fields.first, row, 0, 1, compact || !fields.second ? 2 : 1);
            if (fields.first == commentsBlock) metaLayout->setRowStretch(row, 1);
            if (compact || !fields.second) ++row;
        }
        if (fields.second) metaLayout->addWidget(fields.second, row++, compact ? 0 : 1, 1, compact ? 2 : 1);
    }
    for (auto *layout : findChildren<QLayout *>())
        if (layout != metaLayout) layout->setSpacing(dp(6));
    for (auto *edit : findChildren<QLineEdit *>()) edit->setFixedHeight(dp(37));
    description_text->setMinimumHeight(dp(120));
    changeCoverButton->setFixedHeight(dp(32));
    fingerprintButton->setFixedHeight(dp(32));
    changeCoverButton->setMinimumWidth(dp(128));
    fingerprintButton->setMinimumWidth(dp(128));

}

/**
 * Update all the MetaData and art
 **/
void MetaPanel::update( const SharedInputItem& p_item )
{
    input_item_t * const inputItem = p_item.get();

    if( !inputItem )
    {
        clear();
        return;
    }

    /* Don't update if you are in edit mode */
    if( b_inEditMode ) return;
    p_input = p_item;
    setEnabled( true );

    char *psz_meta;
#define UPDATE_META( meta, widget ) {                                   \
    psz_meta = input_item_Get##meta( inputItem );                       \
    widget->setText( !EMPTY_STR( psz_meta ) ? qfu( psz_meta ) : "" );   \
    free( psz_meta ); }

#define UPDATE_META_INT( meta, widget ) {            \
    psz_meta = input_item_Get##meta( inputItem );    \
    if( !EMPTY_STR( psz_meta ) )                     \
        widget->setValue( atoi( psz_meta ) ); }      \
    free( psz_meta );

    /* Name / Title */
    psz_meta = input_item_GetTitleFbName( inputItem );
    if( psz_meta )
    {
        title_text->setText( qfu( psz_meta ) );
        free( psz_meta );
    }
    else
        title_text->setText( "" );

    /* URL / URI */
    psz_meta = input_item_GetURI( inputItem );
    if( !EMPTY_STR( psz_meta ) )
        emit uriSet( qfu( psz_meta ) );
    fingerprintButton->setEnabled( Chromaprint::isSupported( QString( psz_meta ) ) );
    free( psz_meta );

    /* Other classic though */
    UPDATE_META( Artist, artist_text );
    UPDATE_META( Genre, genre_text );
    UPDATE_META( Copyright, copyright_text );
    UPDATE_META( Album, collection_text );
    disconnect( description_text, &QTextEdit::textChanged, this, &MetaPanel::enterEditMode );
    psz_meta = input_item_GetDescription( inputItem );
    description_text->setPlainText( !EMPTY_STR(psz_meta) ? qfu(psz_meta) : QString() );
    free( psz_meta );
    connect( description_text, &QTextEdit::textChanged, this, &MetaPanel::enterEditMode );
    UPDATE_META( Language, language_text );
    UPDATE_META( Publisher, publisher_text );
    UPDATE_META( EncodedBy, encodedby_text );

    UPDATE_META( Date, date_text );
    UPDATE_META( TrackNum, seqnum_text );
    UPDATE_META( TrackTotal, seqtot_text );

    /* Now Playing || ES Now Playing */
    psz_meta = input_item_GetNowPlayingFb( inputItem );
    if( !EMPTY_STR( psz_meta ) )
        nowplaying_text->setText( qfu( psz_meta ) );
    else
        nowplaying_text->clear();
    free( psz_meta );
    nowplaying_text->parentWidget()->setVisible( !nowplaying_text->text().isEmpty() );
    updateLayout();

#undef UPDATE_META_INT
#undef UPDATE_META

    art_cover->setItem( p_item );
    char *psz_art = input_item_GetArtURL( inputItem );
    currentURL = qfu( psz_art ? psz_art : "" );
    free( psz_art );
}

/**
 * Save the MetaData, triggered by parent->save Button
 **/
bool MetaPanel::saveMeta()
{
    const auto input = p_input.get();

    if( input == NULL ) {
        QMessageBox::warning( this, qtr("Metadata"), qtr("Not editing a file") );
        return false;
    }

    // Keep the live item unchanged until the file write succeeds.
    const SharedInputItem edited { input_item_Copy( input ), false };
    if( !edited )
    {
        QMessageBox::warning( this, qtr("Metadata"), qtr("Failed to save metadata") );
        return false;
    }
    const auto applyEdits = [this]( input_item_t *item ) {
        input_item_SetTitle( item, qtu( title_text->text() ) );
        input_item_SetArtist( item, qtu( artist_text->text() ) );
        input_item_SetAlbum( item, qtu( collection_text->text() ) );
        input_item_SetGenre( item, qtu( genre_text->text() ) );
        input_item_SetTrackNum( item, qtu( seqnum_text->text() ) );
        input_item_SetTrackTotal( item, qtu( seqtot_text->text() ) );
        input_item_SetDate( item, qtu( date_text->text() ) );
        input_item_SetLanguage( item, qtu( language_text->text() ) );
        input_item_SetCopyright( item, qtu( copyright_text->text() ) );
        input_item_SetPublisher( item, qtu( publisher_text->text() ) );
        input_item_SetDescription( item, qtu( description_text->toPlainText() ) );
        input_item_SetEncodedBy( item, qtu( encodedby_text->text() ) );
        if( !currentURL.isEmpty() )
            input_item_SetArtURL( item, qtu( currentURL ) );
    };
    applyEdits( edited.get() );
    int vlcret = input_item_WriteMeta( VLC_OBJECT(p_intf), edited.get() );
    if (vlcret != VLC_SUCCESS) {
        QMessageBox::warning( this, qtr("Metadata"), qtr("Failed to save metadata") );
        return false;
    }
    applyEdits( input );
    art_cover->setItem( p_input );
    /* Refresh the player controller caches for the currently playing item so
     * the Qt UI sees the new artwork and metadata without reopening. */
    THEMIM->refreshMediaMeta( input );
    setEditMode( false );
    return true;
}


bool MetaPanel::isInEditMode()
{
    return b_inEditMode;
}

void MetaPanel::enterEditMode()
{
    setEditMode( true );
}

void MetaPanel::discardEdits()
{
    if( !b_inEditMode )
        return;
    b_inEditMode = false;
    update( p_input );
    emit editModeChanged( false );
}

void MetaPanel::setEditMode( bool b_editing )
{
    if( b_inEditMode == b_editing ) return;
    b_inEditMode = b_editing;
    emit editModeChanged( b_editing );
}

/*
 * Clear all the metadata widgets
 */
void MetaPanel::clear()
{
    p_input.reset();
    setEnabled( false );
    title_text->clear();
    artist_text->clear();
    genre_text->clear();
    copyright_text->clear();
    collection_text->clear();
    seqnum_text->clear();
    seqtot_text->clear();
    disconnect( description_text, &QTextEdit::textChanged, this, &MetaPanel::enterEditMode );
    description_text->clear();
    connect( description_text, &QTextEdit::textChanged, this, &MetaPanel::enterEditMode );
    date_text->clear();
    language_text->clear();
    nowplaying_text->clear();
    publisher_text->clear();
    encodedby_text->clear();
    art_cover->clear();
    currentURL.clear();
    fingerprintButton->setEnabled( false );

    nowplaying_text->parentWidget()->hide();
    updateLayout();
    setEditMode( false );
    emit uriSet( "" );
}

void MetaPanel::fingerprint()
{
    FingerprintDialog *dialog = new FingerprintDialog( this, p_intf, p_input.get() );
    connect( dialog, &FingerprintDialog::metaApplied, this, &MetaPanel::fingerprintUpdate );
    dialog->setAttribute( Qt::WA_DeleteOnClose, true );
    dialog->show();
}

void MetaPanel::fingerprintUpdate( const SharedInputItem& p_item )
{
    update( p_item );
    setEditMode( true );
}

/**
 * Second Panel - Shows the extra metadata in a table, non editable.
 **/
ExtraMetaPanel::ExtraMetaPanel( QWidget *parent ) : QWidget( parent )
{
     QGridLayout *layout = new QGridLayout(this);

     extraMeta = new QTableWidget( this );
     extraMeta->setAccessibleName( qtr("Metadata") );
     extraMeta->verticalHeader()->hide();
     extraMeta->setAlternatingRowColors( true );

     extraMeta->setColumnCount( 2 );
     extraMeta->setHorizontalHeaderLabels({ qtr( "Property" ),
                                            qtr( "Value" ) });
     extraMeta->horizontalHeader()->setStretchLastSection(true);
     extraMeta->resizeRowsToContents();

     extraMeta->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );
     extraMeta->setHorizontalScrollMode( QAbstractItemView::ScrollPerPixel );

     extraMeta->setSelectionBehavior( QAbstractItemView::SelectRows );

     extraMeta->setEditTriggers( QAbstractItemView::NoEditTriggers );
     extraMeta->setSelectionMode( QAbstractItemView::SingleSelection );

     layout->addWidget( extraMeta, 0, 0 );
     if (auto* translator = InterfaceTranslator::instance())
         connect(translator, &InterfaceTranslator::languageChanged, this, [this] {
             extraMeta->setHorizontalHeaderLabels({qtr("Property"), qtr("Value")});
             extraMeta->setAccessibleName(qtr("Metadata"));
         });
}

void ExtraMetaPanel::showEvent( QShowEvent * event )
{
    extraMeta->resizeRowsToContents();
    QWidget::showEvent( event );
}

void ExtraMetaPanel::resizeEvent( QResizeEvent * event )
{
    extraMeta->resizeRowsToContents();
    QWidget::resizeEvent( event );
}

/**
 * Update the Extra Metadata from p_meta->i_extras
 **/
void ExtraMetaPanel::update( input_item_t *p_item )
{
    extraMeta->setRowCount(0);

    if( !p_item )
        return;

    vlc_mutex_locker meta_lock( &p_item->lock );
    vlc_meta_t *p_meta = p_item->p_meta;

    if( !p_meta )
        return;

    struct AddRowHelper {
        AddRowHelper( QTableWidget* target ) : target( target ) { }

        void operator()( char const* psz_key, char const* psz_value )
        {
            int idx = target->rowCount();

            target->insertRow( idx );

            QTableWidgetItem *key = new QTableWidgetItem( qfu( psz_key ) );

            key->setTextAlignment( Qt::AlignRight );
            key->setFlags( key->flags() ^ Qt::ItemIsSelectable );

            target->setItem( idx, 0, key );
            target->setItem( idx, 1, new QTableWidgetItem( qfu( psz_value ) ) );
        }

        QTableWidget* target;

    } add_row ( extraMeta );

    if( char const* psz_disc = vlc_meta_Get( p_meta,  vlc_meta_DiscNumber ) )
        add_row( VLC_META_DISCNUMBER, psz_disc );

    if( char const* psz_url = vlc_meta_Get( p_meta, vlc_meta_URL ) )
        add_row( VLC_META_URL, psz_url );

    char ** ppsz_keys = vlc_meta_CopyExtraNames( p_meta );
    if( ppsz_keys )
    {
        for( int i = 0; ppsz_keys[i]; ++i )
        {
            add_row( ppsz_keys[i], vlc_meta_GetExtra( p_meta, ppsz_keys[i] ) );
            free( ppsz_keys[i] );
        }

        free( ppsz_keys );
    }

    extraMeta->verticalHeader()->resizeSections( QHeaderView::ResizeToContents );
}

/**
 * Clear the ExtraMetaData Tree
 **/
void ExtraMetaPanel::clear()
{
    extraMeta->setRowCount(0);
}

/**
 * Third panel - Stream info
 * Display all codecs and muxers info that we could gather.
 **/
InfoPanel::InfoPanel( QWidget *parent ) : QWidget( parent )
{
     QGridLayout *layout = new QGridLayout(this);

     InfoTree = new QTreeWidget(this);
     InfoTree->setAccessibleName( qtr("Codec") );
     InfoTree->setColumnCount( 1 );
     InfoTree->header()->hide();
     InfoTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
     layout->addWidget(InfoTree, 0, 0 );
     if (auto *translator = InterfaceTranslator::instance())
         connect(translator, &InterfaceTranslator::languageChanged, this, [this] {
             InfoTree->setAccessibleName(qtr("Codec"));
         });
}

/**
 * Update the Codecs information on parent->update()
 **/
void InfoPanel::update( input_item_t *p_item)
{
    if( !p_item )
    {
        clear();
        return;
    }

    InfoTree->clear();
    QTreeWidgetItem *current_item = NULL;
    QTreeWidgetItem *child_item = NULL;

    vlc_mutex_locker locker( &p_item->lock );

    for (auto &cat : vlc::from(p_item->categories, &info_category_t::node))
    {
        if (info_category_IsHidden(&cat))
            continue;

        current_item = new QTreeWidgetItem();
        current_item->setText(0, qfu(cat.psz_name));
        InfoTree->addTopLevelItem( current_item );

        for (auto &info : vlc::from(cat.infos, &info_t::node))
        {
            child_item = new QTreeWidgetItem ();
            child_item->setText(0, qfu(info.psz_name) + ": " + qfu(info.psz_value));
            current_item->addChild(child_item);
        }
        current_item->setExpanded( true );
    }
}

/**
 * Clear the tree
 **/
void InfoPanel::clear()
{
    InfoTree->clear();
}

/**
 * Save all the information to a file
 * Not yet implemented.
 **/
/*
void InfoPanel::saveCodecsInfo()
{}
*/

/**
 * Fourth Panel - Stats
 * Displays the Statistics for reading/displaying in a tree
 */
InputStatsPanel::InputStatsPanel( QWidget *parent ): QWidget( parent )
{
     QVBoxLayout *layout = new QVBoxLayout(this);

     QLabel *topLabel = translatedLabel( N_("Current media / stream statistics"), this );
     topLabel->setWordWrap( true );
     layout->addWidget( topLabel, 0 );

     StatsTree = new QTreeWidget(this);
     StatsTree->setColumnCount( 3 );
     StatsTree->setHeaderHidden( true );

#define CREATE_TREE_ITEM( itemName, itemText, itemValue, unit ) {              \
    itemName =                                                                 \
      new QTreeWidgetItem((QStringList () << itemText << itemValue << unit )); \
    itemName->setTextAlignment( 1 , Qt::AlignRight ) ; }

#define CREATE_CATEGORY( catName, itemText ) {                           \
    CREATE_TREE_ITEM( catName, itemText , "", "" );                      \
    catName->setExpanded( true );                                        \
    StatsTree->addTopLevelItem( catName );    }

#define CREATE_AND_ADD_TO_CAT( itemName, itemText, itemValue, catName, unit ){ \
    CREATE_TREE_ITEM( itemName, itemText, itemValue, unit );                   \
    catName->addChild( itemName ); }

    /* Create the main categories */
    CREATE_CATEGORY( audio, qtr("Audio") );
    CREATE_CATEGORY( video, qtr("Video") );
    CREATE_CATEGORY( input, qtr("Input/Read") );

    CREATE_AND_ADD_TO_CAT( read_media_stat, qtr("Media data size"),
                           "0", input , "KiB" );
    CREATE_AND_ADD_TO_CAT( input_bitrate_stat, qtr("Input bitrate"),
                           "0", input, "kb/s" );
    input_bitrate_graph = new QTreeWidgetItem();
    input_bitrate_stat->addChild( input_bitrate_graph );
    CREATE_AND_ADD_TO_CAT( demuxed_stat, qtr("Demuxed data size"), "0", input, "KiB") ;
    CREATE_AND_ADD_TO_CAT( stream_bitrate_stat, qtr("Content bitrate"),
                           "0", input, "kb/s" );
    CREATE_AND_ADD_TO_CAT( corrupted_stat, qtr("Discarded (corrupted)"),
                           "0", input, "" );
    CREATE_AND_ADD_TO_CAT( discontinuity_stat, qtr("Dropped (discontinued)"),
                           "0", input, "" );

    CREATE_AND_ADD_TO_CAT( vdecoded_stat, qtr("Decoded"),
                           "0", video, qtr("blocks") );
    CREATE_AND_ADD_TO_CAT( vdisplayed_stat, qtr("Displayed"),
                           "0", video, qtr("frames") );
    CREATE_AND_ADD_TO_CAT( vlate_stat, qtr("Late"),
                           "0", video, qtr("frames") );
    CREATE_AND_ADD_TO_CAT( vlost_frames_stat, qtr("Lost"),
                           "0", video, qtr("frames") );

    CREATE_AND_ADD_TO_CAT( adecoded_stat, qtr("Decoded"),
                           "0", audio, qtr("blocks") );
    CREATE_AND_ADD_TO_CAT( aplayed_stat, qtr("Played"),
                           "0", audio, qtr("buffers") );
    CREATE_AND_ADD_TO_CAT( alost_stat, qtr("Lost"), "0", audio, qtr("buffers") );

#undef CREATE_AND_ADD_TO_CAT
#undef CREATE_CATEGORY
#undef CREATE_TREE_ITEM

    input->setExpanded( true );
    video->setExpanded( true );
    audio->setExpanded( true );

    StatsTree->header()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
    StatsTree->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );

    layout->addWidget(StatsTree, 4 );

    statsView = new VLCStatsView( this );
    statsView->setFrameStyle( QFrame::NoFrame );
    statsView->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Fixed );
    input_bitrate_graph->setSizeHint( 1, QSize(0, 100) );
    graphLegend = new QLabel;
    StatsTree->setItemWidget( input_bitrate_graph, 0, graphLegend );
    StatsTree->setItemWidget( input_bitrate_graph, 1, statsView );
    updateGraphLegend();
    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &InputStatsPanel::retranslate);
}

void InputStatsPanel::retranslate()
{
    const struct { QTreeWidgetItem* item; const char* source; } labels[] = {
        {audio, N_("Audio")}, {video, N_("Video")}, {input, N_("Input/Read")},
        {read_media_stat, N_("Media data size")}, {input_bitrate_stat, N_("Input bitrate")},
        {demuxed_stat, N_("Demuxed data size")}, {stream_bitrate_stat, N_("Content bitrate")},
        {corrupted_stat, N_("Discarded (corrupted)")}, {discontinuity_stat, N_("Dropped (discontinued)")},
        {vdecoded_stat, N_("Decoded")}, {vdisplayed_stat, N_("Displayed")},
        {vlate_stat, N_("Late")}, {vlost_frames_stat, N_("Lost")},
        {adecoded_stat, N_("Decoded")}, {aplayed_stat, N_("Played")}, {alost_stat, N_("Lost")}
    };
    for (const auto& label : labels) label.item->setText(0, qtr(label.source));
    for (auto* item : {vdecoded_stat, adecoded_stat}) item->setText(2, qtr("blocks"));
    for (auto* item : {vdisplayed_stat, vlate_stat, vlost_frames_stat}) item->setText(2, qtr("frames"));
    for (auto* item : {aplayed_stat, alost_stat}) item->setText(2, qtr("buffers"));
    updateGraphLegend();
}

void InputStatsPanel::updateGraphLegend()
{
    if (!graphLegend) return;
    graphLegend->setText(QStringLiteral("<font style=\"color:%1\">%2</font><br/>%3")
        .arg(palette().color(QPalette::Highlight).name(),
             qtr("Last 60 seconds").toHtmlEscaped(), qtr("Overall").toHtmlEscaped()));
}

void InputStatsPanel::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) updateGraphLegend();
}

void InputStatsPanel::hideEvent( QHideEvent * event )
{
    statsView->reset();
    QWidget::hideEvent( event );
}

/**
 * Update the Statistics
 **/
void InputStatsPanel::update( const input_stats_t& stats )
{
    if ( !isVisible() ) return;


#define UPDATE_INT( widget, calc... ) \
    { widget->setText( 1, QString::number( (qulonglong)calc ) ); }

#define UPDATE_FLOAT( widget, calc ) \
    widget->setText( 1 , QString().setNum(calc, 'f', 0).leftJustified(6) )

    UPDATE_INT( read_media_stat, (stats.i_read_bytes / 1024 ) );
    UPDATE_FLOAT( input_bitrate_stat,  (float)(stats.f_input_bitrate *  8000 ));
    UPDATE_INT( demuxed_stat,    (stats.i_demux_read_bytes / 1024 ) );
    UPDATE_FLOAT( stream_bitrate_stat, (float)(stats.f_demux_bitrate *  8000 ));
    UPDATE_INT( corrupted_stat,      stats.i_demux_corrupted );
    UPDATE_INT( discontinuity_stat,  stats.i_demux_discontinuity );

    statsView->addValue( stats.f_input_bitrate * 8000 );

    /* Video */
    UPDATE_INT( vdecoded_stat,     stats.i_decoded_video );
    UPDATE_INT( vdisplayed_stat,   stats.i_displayed_pictures );
    UPDATE_INT( vlate_stat,        stats.i_late_pictures );
    UPDATE_INT( vlost_frames_stat, stats.i_lost_pictures );

    /* Audio*/
    UPDATE_INT( adecoded_stat, stats.i_decoded_audio );
    UPDATE_INT( aplayed_stat,  stats.i_played_abuffers );
    UPDATE_INT( alost_stat,    stats.i_lost_abuffers );

#undef UPDATE_INT
#undef UPDATE_FLOAT
}

void InputStatsPanel::clear()
{
}
