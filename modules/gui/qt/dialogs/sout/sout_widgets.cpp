/*****************************************************************************
 * sout_widgets.cpp : Widgets for stream output destination boxes
 ****************************************************************************
 * Copyright (C) 2007-2009 the VideoLAN team
 * Copyright (C) 2007 Société des arts technologiques
 * Copyright (C) 2007 Savoir-faire Linux
 *
 * Authors: Jean-Baptiste Kempf <jb@videolan.org>
 *          Pierre-Luc Beaudoin <pierre-luc.beaudoin@savoirfairelinux.com>
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

#include "dialogs/sout/sout_widgets.hpp"
#include "dialogs/sout/sout.hpp"
#include "maininterface/mainctx.hpp"
#include "util/soutchain.hpp"
#include "util/qt_dirs.hpp"
#include "util/interface_translator.hpp"
#include <vlc_intf_strings.h>

#include <QGroupBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QUrl>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QHostAddress>

#define I_FILE_SLASH_DIR \
    I_DIR_OR_FOLDER( N_("File/Directory"), N_("File/Folder") )

namespace {
template<typename Widget>
void watchDestinationText(Widget* widget, const char* source)
{
    const QByteArray text(source);
    const auto update = [widget, text] { widget->setText(qtr(text.constData())); };
    update();
    if (auto* translator = InterfaceTranslator::instance())
        QObject::connect(translator, &InterfaceTranslator::languageChanged, widget, update);
}

template<typename Widget>
Widget* translatedDestination(const char* source, QWidget* parent)
{
    auto* widget = new Widget(QString(), parent);
    watchDestinationText(widget, source);
    return widget;
}
}

namespace {
QString streamHost(QString value)
{
    value = value.trimmed();
    QHostAddress address;
    if (address.setAddress(value) && address.protocol() == QAbstractSocket::IPv6Protocol)
        return QLatin1Char('[') + value + QLatin1Char(']');
    return value;
}
QString streamMount(QString value)
{
    value = value.trimmed();
    while (value.startsWith(QLatin1Char('/'))) value.remove(0, 1);
    return value;
}
}

QString VirtualDestBox::validationError(const QString &mux)
{
    for (auto *field : findChildren<QLineEdit *>())
    {
        const auto text = field->text();
        if (text.contains(QChar::Null) || text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r')))
            return qtr("Enter a destination without line breaks.");
        if (field->objectName().endsWith(QStringLiteral("Address")))
        {
            const QUrl url(QStringLiteral("http://") + streamHost(text));
            if (text.trimmed().isEmpty() || !url.isValid() || url.host().isEmpty() || url.port() != -1 || !url.path().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
                return qtr("Enter a host name or IP address without a port or URL.");
        }
    }
    if (objectName() == QStringLiteral("streamICEDestination"))
        if (streamMount(findChild<QLineEdit *>(QStringLiteral("streamICEMount"))->text()).isEmpty())
            return qtr("Enter an Icecast mount point.");
    if (getMRL(mux).isEmpty()) return qtr("Enter a destination path or address.");
    return {};
}

QString VirtualDestBox::summary(const QString &)
{
    auto *port = findChild<QSpinBox *>();
    auto *field = findChild<QLineEdit *>();
    if (!port || !field) return {};
    if (objectName() == QStringLiteral("streamHTTPDestination") || objectName() == QStringLiteral("streamRTSPDestination"))
        return qtr("Port %1, path %2").arg(port->value()).arg(field->text().startsWith(QLatin1Char('/')) ? field->text() : QLatin1Char('/') + field->text());
    QString value = streamHost(field->text()) + QLatin1Char(':') + QString::number(port->value());
    if (objectName() == QStringLiteral("streamICEDestination"))
        value += QLatin1Char('/') + streamMount(findChild<QLineEdit *>(QStringLiteral("streamICEMount"))->text());
    return value;
}

SoutInputBox::SoutInputBox( QWidget *_parent, const QString& mrl ) : QGroupBox( _parent )
{
    /**
     * Source Block
     **/
    setTitle( qtr( "Source" ) );
    QGridLayout *sourceLayout = new QGridLayout( this );
    sourceLayout->setColumnStretch(1, 1);

    QLabel *sourceLabel = new QLabel( qtr( "Source:" ) );
    sourceLayout->addWidget( sourceLabel, 0, 0 );

    sourceLine = new QLabel;
    sourceLine->setTextFormat(Qt::PlainText);
    sourceLine->setWordWrap(true);
    sourceLine->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    sourceLine->setObjectName(QStringLiteral("streamSourcePath"));
    sourceLine->setText( mrl );
    sourceLabel->setBuddy( sourceLine );
    sourceLayout->addWidget( sourceLine, 0, 1 );

    QLabel *sourceTypeLabel = new QLabel( qtr( "Type:" ) );
    sourceLayout->addWidget( sourceTypeLabel, 1, 0 );
    sourceValueLabel = new QLabel;
    sourceLayout->addWidget( sourceValueLabel, 1, 1 );

    /* Line */
    QFrame *line = new QFrame;
    line->setFrameStyle( QFrame::HLine |QFrame::Sunken );
    sourceLayout->addWidget( line, 2, 0, 1, -1 );

    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this,
                [this, sourceLabel, sourceTypeLabel] {
            setTitle(qtr("Source"));
            sourceLabel->setText(qtr("Source:"));
            sourceTypeLabel->setText(qtr("Type:"));
            if (localFileType)
                sourceValueLabel->setText(qfut(I_FILE_SLASH_DIR));
        });
}

void SoutInputBox::setMRL( const QString& mrl )
{
    QUrl uri( mrl );
    QString type = uri.scheme();
    localFileType = false;

    if( !uri.isValid() &&
        !mrl.startsWith("http") &&
        !mrl.startsWith("ftp") &&
        !mrl.startsWith("/") )
    {
        int pos = mrl.indexOf("://");
        if( pos != -1 )
        {
            sourceValueLabel->setText( mrl.left( pos ) );
            sourceLine->setText( mrl );
        }
    }
    else if ( type == "window" )
    {
        /* QUrl mangles X11 Window identifiers so use the raw mrl */
        sourceLine->setText( mrl );
    }
    else
    {
        sourceLine->setText(
            toNativeSeparators(uri.toDisplayString(
                QUrl::RemovePassword | QUrl::PreferLocalFile | QUrl::NormalizePathSegments
            )));
        localFileType = type.isEmpty() || uri.isLocalFile();
        if (localFileType) type = qfut(I_FILE_SLASH_DIR);
        sourceValueLabel->setText( type );
    }
}

VirtualDestBox::VirtualDestBox( QWidget *_parent ) : QWidget( _parent )
{
    label = new QLabel( this );
    label->setWordWrap( true );
    label->setTextFormat(Qt::PlainText);
    layout = new QGridLayout( this );
    layout->addWidget( label, 0, 0, 1, -1);
}

VirtualDestBox::~VirtualDestBox()
{
    delete label;
    delete layout;
}

/* FileDest Box */
FileDestBox::FileDestBox( QWidget *_parent, qt_intf_t * _p_intf ) : VirtualDestBox( _parent )
{
    setObjectName(QStringLiteral("streamFileDestination"));
    p_intf = _p_intf;

    QPushButton *fileSelectButton;

    watchDestinationText(label, N_("Save the stream to a file."));

    QLabel *fileLabel = translatedDestination<QLabel>(N_("Filename"), this);
    layout->addWidget(fileLabel, 1, 0, 1, 1);

    fileEdit = new QLineEdit(this);
    fileEdit->setObjectName(QStringLiteral("streamFilePath"));
    layout->addWidget(fileEdit, 1, 1, 1, 1);
    fileEdit->setClearButtonEnabled(true);
    fileLabel->setBuddy(fileEdit);

    fileSelectButton = translatedDestination<QPushButton>(N_("Browse..."), this);
    QSizePolicy sizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    fileSelectButton->setSizePolicy(sizePolicy);

    layout->addWidget(fileSelectButton, 1, 2, 1, 1);
    fileSelectButton->setObjectName(QStringLiteral("streamFileBrowse"));
    connect( fileEdit, &QLineEdit::textChanged, this, &FileDestBox::mrlUpdated );
    BUTTONACT( fileSelectButton, &FileDestBox::fileBrowse );
}

QString FileDestBox::outputPath(const QString &mux) const
{
    const QUrl url(fileEdit->text());
    QString path = url.isLocalFile() ? url.toLocalFile() : QDir::fromNativeSeparators(fileEdit->text());
    if (!path.isEmpty() && !path.endsWith(QLatin1Char('/')) && !QFileInfo(path).isDir() && QFileInfo(path).suffix().isEmpty() && !mux.isEmpty())
        path += QLatin1Char('.') + mux;
    return path;
}

QString FileDestBox::validationError(const QString &mux)
{
    const auto path = outputPath(mux);
    if (path.isEmpty() || !QFileInfo(path).isAbsolute()) return qtr("Enter a full output file path.");
    if (path.contains(QChar::Null) || path.contains(QLatin1Char('\n')) || path.contains(QLatin1Char('\r')))
        return qtr("Enter a valid output file path.");
    const QFileInfo file(path);
    if (file.isDir() || path.endsWith(QLatin1Char('/'))) return qtr("Choose a file name, not a folder.");
    if (file.exists() || file.isSymLink()) return qtr("The output file already exists. Choose another file name.");
    const QFileInfo parent(file.absolutePath());
    if (!parent.isDir() || !parent.isWritable()) return qtr("Select an output folder that you can write to.");
    return {};
}

QString FileDestBox::summary(const QString &mux)
{
    return toNativeSeparators(outputPath(mux));
}

QString FileDestBox::getMRL(const QString &mux)
{
    const auto path = outputPath(mux);
    if (path.isEmpty()) return {};
    char *escaped = config_StringEscape(qtu(path));
    if (!escaped) return {};
    const auto quoted = QStringLiteral("'") + qfu(escaped) + QLatin1Char('\'');
    free(escaped);
    SoutModule file(QStringLiteral("std"));
    SoutModule access(QStringLiteral("file"));
    access.option(QStringLiteral("no-overwrite"));
    file.option(QStringLiteral("access"), access);
    file.option(QStringLiteral("dst"), quoted, true);
    if (!mux.isEmpty()) file.option(QStringLiteral("mux"), mux, false);
    return file.to_string();
}

void FileDestBox::fileBrowse()
{
    const auto initial = fileEdit->text().isEmpty() ? p_intf->p_mi->getDialogFilePath() : QUrl::fromLocalFile(outputPath({}));
    const auto chosen = QFileDialog::getSaveFileUrl(this, qtr("Select output file"), initial,
        qtr("All files (*)"), nullptr, QFileDialog::DontConfirmOverwrite, {QStringLiteral("file")});
    if (chosen.isEmpty()) return;
    fileEdit->setText(toNativeSeparators(chosen.toLocalFile()));
}



HTTPDestBox::HTTPDestBox( QWidget *_parent ) : VirtualDestBox( _parent )
{
    setObjectName(QStringLiteral("streamHTTPDestination"));
    watchDestinationText(label, N_("Set a port and path. Other devices can open the stream with this device's address."));

    QLabel *HTTPLabel = translatedDestination<QLabel>(N_("Path"), this);
    QLabel *HTTPPortLabel = translatedDestination<QLabel>(N_("Port"), this);
    layout->addWidget(HTTPLabel, 2, 0, 1, 1);
    layout->addWidget(HTTPPortLabel, 1, 0, 1, 1);

    HTTPEdit = new QLineEdit(this);
    HTTPEdit->setObjectName(QStringLiteral("streamHTTPPath"));
    HTTPEdit->setText( "/" );

    HTTPPort = new QSpinBox(this);
    HTTPPort->setObjectName(QStringLiteral("streamHTTPPort"));
    HTTPPort->setMaximumSize(QSize(90, 16777215));
    HTTPPort->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);
    HTTPPort->setMinimum(1);
    HTTPPort->setMaximum(65535);
    HTTPPort->setValue(8080);

    layout->addWidget(HTTPEdit, 2, 1, 1, 1);
    layout->addWidget(HTTPPort, 1, 1, 1, 1);
    connect( HTTPPort, QOverload<int>::of(&QSpinBox::valueChanged), this, &HTTPDestBox::mrlUpdated );
    connect( HTTPEdit, &QLineEdit::textChanged, this, &HTTPDestBox::mrlUpdated );
}

QString HTTPDestBox::getMRL( const QString& mux )
{
    if( HTTPEdit->text().isEmpty() ) return "";

    QString path = HTTPEdit->text();
    if( path[0] != '/' )
        path.prepend( qfu("/") );
    QString port;
    port.setNum( HTTPPort->value() );
    QString dst = ":" + port + path;

    SoutChain m;
    m.begin( "http" );
    /* Path-extension is primary muxer to use if possible,
       otherwise check for mux-choise and see that it isn't mp4
       then fallback to flv*/
    if ( !path.contains(QRegularExpression(QStringLiteral("\\..{2,3}$")) ) )
    {
        if( !mux.isEmpty() && mux.compare("mp4") )
           m.option( "mux", mux );
        else
           m.option( "mux", "ffmpeg{mux=flv}" );
    }
    m.option( "dst", dst );
    m.end();

    return m.to_string();
}

MMSHDestBox::MMSHDestBox( QWidget *_parent ) : VirtualDestBox( _parent )
{
    setObjectName(QStringLiteral("streamMMSHDestination"));
    watchDestinationText(label, N_("Serve this stream over MMSH."));

    QLabel *MMSHLabel = translatedDestination<QLabel>(N_("Address"), this);
    QLabel *MMSHPortLabel = translatedDestination<QLabel>(N_("Port"), this);
    layout->addWidget(MMSHLabel, 1, 0, 1, 1);
    layout->addWidget(MMSHPortLabel, 2, 0, 1, 1);

    MMSHEdit = new QLineEdit(this);
    MMSHEdit->setObjectName(QStringLiteral("streamMMSHAddress"));
    MMSHEdit->setText( "0.0.0.0" );

    MMSHPort = new QSpinBox(this);
    MMSHPort->setObjectName(QStringLiteral("streamMMSHPort"));
    MMSHPort->setMaximumSize(QSize(90, 16777215));
    MMSHPort->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);
    MMSHPort->setMinimum(1);
    MMSHPort->setMaximum(65535);
    MMSHPort->setValue(8080);

    layout->addWidget(MMSHEdit, 1, 1, 1, 1);
    layout->addWidget(MMSHPort, 2, 1, 1, 1);
    connect( MMSHPort, QOverload<int>::of(&QSpinBox::valueChanged), this, &MMSHDestBox::mrlUpdated );
    connect( MMSHEdit, &QLineEdit::textChanged, this, &MMSHDestBox::mrlUpdated );
}

QString MMSHDestBox::getMRL( const QString& )
{
    if( MMSHEdit->text().isEmpty() ) return "";

    SoutChain m;
    m.begin( "std" );
    m.option(  "access", "mmsh" );
    m.option( "mux", "asfh" );
    m.option( "dst", streamHost(MMSHEdit->text()), MMSHPort->value() );
    m.end();

    return m.to_string();
}


RTSPDestBox::RTSPDestBox( QWidget *_parent ) : VirtualDestBox( _parent )
{
    setObjectName(QStringLiteral("streamRTSPDestination"));
    watchDestinationText(label, N_("Serve this stream over RTSP."));

    QLabel *RTSPLabel = translatedDestination<QLabel>(N_("Path"), this);
    QLabel *RTSPPortLabel = translatedDestination<QLabel>(N_("Port"), this);
    layout->addWidget( RTSPLabel, 2, 0, 1, 1 );
    layout->addWidget( RTSPPortLabel, 1, 0, 1, 1 );

    RTSPEdit = new QLineEdit( this );
    RTSPEdit->setObjectName(QStringLiteral("streamRTSPPath"));
    RTSPEdit->setText( "/" );

    RTSPPort = new QSpinBox( this );
    RTSPPort->setObjectName(QStringLiteral("streamRTSPPort"));
    RTSPPort->setMaximumSize( QSize( 90, 16777215 ) );
    RTSPPort->setAlignment( Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter );
    RTSPPort->setMinimum( 1 );
    RTSPPort->setMaximum( 65535 );
    RTSPPort->setValue( 8554 );

    layout->addWidget( RTSPEdit, 2, 1, 1, 1 );
    layout->addWidget( RTSPPort, 1, 1, 1, 1 );
    connect( RTSPPort, QOverload<int>::of(&QSpinBox::valueChanged), this, &RTSPDestBox::mrlUpdated );
    connect( RTSPEdit, &QLineEdit::textChanged, this, &RTSPDestBox::mrlUpdated );
}

QString RTSPDestBox::getMRL( const QString& )
{
    if( RTSPEdit->text().isEmpty() ) return "";

    QString path = RTSPEdit->text();
    if( path[0] != '/' )
        path.prepend( qfu("/") );
    QString port;
    port.setNum( RTSPPort->value() );
    QString sdp = "rtsp://:" + port + path;

    SoutChain m;
    m.begin( "rtp" );
    m.option( "sdp", sdp );
    m.end();

    return m.to_string();
}


UDPDestBox::UDPDestBox( QWidget *_parent ) : VirtualDestBox( _parent )
{
    setObjectName(QStringLiteral("streamUDPDestination"));
    watchDestinationText(label, N_("Send this stream to the destination address and port."));

    QLabel *UDPLabel = translatedDestination<QLabel>(N_("Address"), this);
    QLabel *UDPPortLabel = translatedDestination<QLabel>(N_("Port"), this);
    layout->addWidget(UDPLabel, 1, 0, 1, 1);
    layout->addWidget(UDPPortLabel, 2, 0, 1, 1);

    UDPEdit = new QLineEdit(this);
    UDPEdit->setObjectName(QStringLiteral("streamUDPAddress"));

    UDPPort = new QSpinBox(this);
    UDPPort->setObjectName(QStringLiteral("streamUDPPort"));
    UDPPort->setMaximumSize(QSize(90, 16777215));
    UDPPort->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);
    UDPPort->setMinimum(1);
    UDPPort->setMaximum(65535);
    UDPPort->setValue(1234);

    layout->addWidget(UDPEdit, 1, 1, 1, 1);
    layout->addWidget(UDPPort, 2, 1, 1, 1);
    connect( UDPPort, QOverload<int>::of(&QSpinBox::valueChanged), this, &UDPDestBox::mrlUpdated );
    connect( UDPEdit, &QLineEdit::textChanged, this, &UDPDestBox::mrlUpdated );
}

QString UDPDestBox::getMRL( const QString& mux )
{
    if( UDPEdit->text().isEmpty() ) return "";

    SoutChain m;
    m.begin( "udp" );
    /* udp output, ts-mux is really only reasonable one to use*/
    if( !mux.isEmpty() && !mux.compare("ts" ) )
        m.option( "mux", mux );
    m.option( "dst", streamHost(UDPEdit->text()), UDPPort->value() );
    m.end();

    return m.to_string();
}

SRTDestBox::SRTDestBox(QWidget *_parent, const char *_mux) :
        VirtualDestBox( _parent ), mux( qfu( _mux ) )
{
    setObjectName(QStringLiteral("streamSRTDestination"));
    watchDestinationText(label, N_("Send this stream over SRT."));

    QLabel *SRTLabel = translatedDestination<QLabel>(N_("Address"), this);
    SRTEdit = new QLineEdit( this );
    SRTEdit->setObjectName(QStringLiteral("streamSRTAddress"));
    layout->addWidget( SRTLabel, 1, 0, 1, 1 );
    layout->addWidget( SRTEdit, 1, 1, 1, 1 );

    QLabel *SRTPortLabel = translatedDestination<QLabel>(N_("Base port"), this);
    SRTPort = new QSpinBox( this );
    SRTPort->setObjectName(QStringLiteral("streamSRTPort"));
    SRTPort->setMaximumSize( QSize( 90, 16777215 ) );
    SRTPort->setAlignment(
            Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter );
    SRTPort->setMinimum( 1 );
    SRTPort->setMaximum( 65535 );
    SRTPort->setValue( 7001 );
    layout->addWidget( SRTPortLabel, 2, 0, 1, 1 );
    layout->addWidget( SRTPort, 2, 1, 1, 1 );

    QLabel *SAPNameLabel = translatedDestination<QLabel>(N_("Stream name"), this);
    SAPName = new QLineEdit( this );
    layout->addWidget( SAPNameLabel, 3, 0, 1, 1 );
    layout->addWidget( SAPName, 3, 1, 1, 1 );

    connect( SRTEdit, &QLineEdit::textChanged, this, &SRTDestBox::mrlUpdated );
    connect( SRTPort, QOverload<int>::of(&QSpinBox::valueChanged), this, &SRTDestBox::mrlUpdated );
    connect( SAPName, &QLineEdit::textChanged, this, &SRTDestBox::mrlUpdated );
}

QString SRTDestBox::getMRL(const QString&)
{
    QString addr = streamHost(SRTEdit->text());
    QString name = SAPName->text();

    if (addr.isEmpty())
        return qfu( "" );
    QString destination = addr + ":" + QString::number( SRTPort->value() );
    SoutChain m;
    m.begin( "srt" );
    m.option( "dst", destination );
    /* mp4-mux ain't usable in rtp-output either */
    if (!mux.isEmpty())
        m.option( "mux", mux );
    if (!name.isEmpty()) {
        m.option( "sap" );
        m.option( "name", name );
    }
    m.end();

    return m.to_string();
}

RISTDestBox::RISTDestBox( QWidget *_parent, const char * )
    : VirtualDestBox( _parent )
{
    setObjectName(QStringLiteral("streamRISTDestination"));
    watchDestinationText(label, N_("Send this stream over RIST."));

    QLabel *RISTAddressLabel = translatedDestination<QLabel>(N_("Destination Address"), this);
    RISTAddress = new QLineEdit(this);
    RISTAddress->setObjectName(QStringLiteral("streamRISTAddress"));
    layout->addWidget(RISTAddressLabel, 1, 0, 1, 1);
    layout->addWidget(RISTAddress, 1, 1, 1, 1);

    QLabel *RISTPortLabel = translatedDestination<QLabel>(N_("Destination Port"), this);
    RISTPort = new QSpinBox(this);
    RISTPort->setObjectName(QStringLiteral("streamRISTPort"));
    RISTPort->setMaximumSize(QSize(90, 16777215));
    RISTPort->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);
    RISTPort->setMinimum(1);
    RISTPort->setMaximum(65535);
    RISTPort->setValue(1968);
    layout->addWidget(RISTPortLabel, 2, 0, 1, 1);
    layout->addWidget(RISTPort, 2, 1, 1, 1);

    QLabel *RISTNameLabel = translatedDestination<QLabel>(N_("Stream Name"), this);
    RISTName = new QLineEdit(this);
    layout->addWidget(RISTNameLabel, 3, 0, 1, 1);
    layout->addWidget(RISTName, 3, 1, 1, 1);

    connect( RISTAddress, &QLineEdit::textChanged, this, &RISTDestBox::mrlUpdated );
    connect( RISTPort, QOverload<int>::of(&QSpinBox::valueChanged), this, &RISTDestBox::mrlUpdated );
    connect( RISTName, &QLineEdit::textChanged, this, &RISTDestBox::mrlUpdated );
}

QString RISTDestBox::getMRL( const QString& )
{
    QString addr = streamHost(RISTAddress->text());
    QString name = RISTName->text();

    if( addr.isEmpty() ) return qfu("");
    QString destination = addr + ":" + QString::number(RISTPort->value());
    SoutChain m;
    m.begin( "std" );
    if( !name.isEmpty() )
    {
        m.option( "access", "rist{cname=" + name + "}" );
    }
    else
    {
        m.option( "access", "rist" );
    }
    m.option( "mux", "ts" );
    m.option( "dst", destination );
    m.end();

    return m.to_string();
}


RTPDestBox::RTPDestBox( QWidget *_parent, const char *_mux )
    : VirtualDestBox( _parent ), mux( qfu(_mux) )
{
    setObjectName(QStringLiteral("streamRTPDestination"));
    watchDestinationText(label, N_("Send this stream over RTP."));

    QLabel *RTPLabel = translatedDestination<QLabel>(N_("Address"), this);
    RTPEdit = new QLineEdit(this);
    RTPEdit->setObjectName(QStringLiteral("streamRTPAddress"));
    layout->addWidget(RTPLabel, 1, 0, 1, 1);
    layout->addWidget(RTPEdit, 1, 1, 1, 1);

    QLabel *RTPPortLabel = translatedDestination<QLabel>(N_("Base port"), this);
    RTPPort = new QSpinBox(this);
    RTPPort->setObjectName(QStringLiteral("streamRTPPort"));
    RTPPort->setMaximumSize(QSize(90, 16777215));
    RTPPort->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);
    RTPPort->setMinimum(1);
    RTPPort->setMaximum(65535);
    RTPPort->setValue(5004);
    layout->addWidget(RTPPortLabel, 2, 0, 1, 1);
    layout->addWidget(RTPPort, 2, 1, 1, 1);

    QLabel *SAPNameLabel = translatedDestination<QLabel>(N_("Stream name"), this);
    SAPName = new QLineEdit(this);
    layout->addWidget(SAPNameLabel, 3, 0, 1, 1);
    layout->addWidget(SAPName, 3, 1, 1, 1);

    connect( RTPEdit, &QLineEdit::textChanged, this, &RTPDestBox::mrlUpdated );
    connect( RTPPort, QOverload<int>::of(&QSpinBox::valueChanged), this, &RTPDestBox::mrlUpdated );
    connect( SAPName, &QLineEdit::textChanged, this, &RTPDestBox::mrlUpdated );
}

QString RTPDestBox::getMRL( const QString& )
{
    QString addr = RTPEdit->text();
    QString name = SAPName->text();

    if( addr.isEmpty() ) return qfu("");

    SoutChain m;
    m.begin( "rtp" );
    m.option( "dst", RTPEdit->text().trimmed() );
    m.option( "port", RTPPort->value() );
    /* mp4-mux ain't usable in rtp-output either */
    if( !mux.isEmpty() )
        m.option( "mux", mux );
    if( !name.isEmpty() )
    {
        m.option( "sap" );
        m.option( "name", name );
    }
    m.end();

    return m.to_string();
}


ICEDestBox::ICEDestBox( QWidget *_parent ) : VirtualDestBox( _parent )
{
    setObjectName(QStringLiteral("streamICEDestination"));
    watchDestinationText(label, N_("Send this stream to an Icecast server."));

    QLabel *ICELabel = translatedDestination<QLabel>(N_("Address"), this);
    QLabel *ICEPortLabel = translatedDestination<QLabel>(N_("Port"), this);
    layout->addWidget(ICELabel, 1, 0, 1, 1);
    layout->addWidget(ICEPortLabel, 2, 0, 1, 1);

    ICEEdit = new QLineEdit(this);
    ICEEdit->setObjectName(QStringLiteral("streamICEAddress"));

    ICEPort = new QSpinBox(this);
    ICEPort->setObjectName(QStringLiteral("streamICEPort"));
    ICEPort->setMaximumSize(QSize(90, 16777215));
    ICEPort->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);
    ICEPort->setMinimum(1);
    ICEPort->setMaximum(65535);
    ICEPort->setValue(8000);

    layout->addWidget(ICEEdit, 1, 1, 1, 1);
    layout->addWidget(ICEPort, 2, 1, 1, 1);

    QLabel *IcecastMountpointLabel = translatedDestination<QLabel>(N_("Mount Point"), this);
    QLabel *IcecastNameLabel = translatedDestination<QLabel>(N_("Login:pass"), this);
    ICEMountEdit = new QLineEdit( this );
    ICEMountEdit->setObjectName(QStringLiteral("streamICEMount"));
    ICEPassEdit = new QLineEdit( this );
    ICEPassEdit->setObjectName(QStringLiteral("streamICECredentials"));
    ICEPassEdit->setEchoMode(QLineEdit::Password);
    const auto credentialsHelp = [this] {
        ICEPassEdit->setPlaceholderText(qtr("user:password"));
        ICEPassEdit->setToolTip(qtr("Enter the user name, a colon and the password."));
    };
    credentialsHelp();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, credentialsHelp);
    layout->addWidget(IcecastMountpointLabel, 3, 0, 1, 1 );
    layout->addWidget(ICEMountEdit, 3, 1, 1, -1 );
    layout->addWidget(IcecastNameLabel, 4, 0, 1, 1 );
    layout->addWidget(ICEPassEdit, 4, 1, 1, -1 );

    connect( ICEPort, QOverload<int>::of(&QSpinBox::valueChanged), this, &ICEDestBox::mrlUpdated );
    connect( ICEEdit, &QLineEdit::textChanged, this, &ICEDestBox::mrlUpdated );
    connect( ICEMountEdit, &QLineEdit::textChanged, this, &ICEDestBox::mrlUpdated );
    connect( ICEPassEdit, &QLineEdit::textChanged, this, &ICEDestBox::mrlUpdated );
}

QString ICEDestBox::getMRL( const QString& )
{
    if( ICEEdit->text().isEmpty() ) return "";

    SoutChain m;
    m.begin( "std" );
    m.option( "access", "shout" );
    m.option( "mux", "ogg" );

    QString url = "//" + ICEPassEdit->text() + "@"
        + streamHost(ICEEdit->text())
        + ":" + QString::number( ICEPort->value(), 10 )
        + "/" + streamMount(ICEMountEdit->text());

    m.option( "dst", url );
    m.end();
    return m.to_string();
}
