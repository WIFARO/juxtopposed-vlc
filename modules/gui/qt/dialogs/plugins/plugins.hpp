/*****************************************************************************
 * plugins.hpp : Plug-ins and extensions listing
 ****************************************************************************
 * Copyright (C) 2008 the VideoLAN team
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

#ifndef QVLC_PLUGIN_DIALOG_H_
#define QVLC_PLUGIN_DIALOG_H_ 1

#include "widgets/native/qvlcframe.hpp"

#include <QStringList>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>

class QLabel;
class QCheckBox;
class QProgressBar;
class QTabWidget;
class QComboBox;
class QTreeWidget;
class QLineEdit;
//class QTextBrowser;
class QListView;
class QStyleOptionViewItem;
class QPainter;
class QKeyEvent;
class PluginTab;
class ExtensionTab;
class AddonsTab;
class ExtensionListItem;
class ExtensionCopy;
class ExtensionsManager;
class AddonsModel;

extern "C" {
    typedef struct extension_t extension_t;
};

class PluginDialog : public QVLCFrame
{
    Q_OBJECT

public:
    PluginDialog( qt_intf_t * );
    virtual ~PluginDialog();

protected:
    bool eventFilter(QObject*, QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    QLineEdit *activeSearch() const;
    QTabWidget *tabs;
    PluginTab *pluginTab;
    ExtensionTab *extensionTab;
    AddonsTab *addonsTab;
};

class PluginTab : public QVLCFrame
{
    Q_OBJECT
public:
    enum
    {
        NAME = 0,
        CAPABILITY,
        SCORE
    };

protected:
    void keyPressEvent( QKeyEvent *keyEvent ) override;

private:
    PluginTab( qt_intf_t *p_intf );
    virtual ~PluginTab();

    void FillTree();
    QTreeWidget *treePlugins;
    QLineEdit *edit;
    QLabel *searchInfo;

private slots:
    void search( const QString& );

    friend class PluginDialog;
};

class ExtensionTab : public QVLCFrame
{
    Q_OBJECT

protected:
    void keyPressEvent( QKeyEvent *keyEvent ) override;

private:
    ExtensionTab( qt_intf_t *p_intf );
    virtual ~ExtensionTab();

private slots:
    void moreInformation();
    void updateButtons();

private:
    QListView *extList;
    QLineEdit *searchInput;
    QLabel *listInfo;
    QPushButton *butMoreInfo;

    friend class PluginDialog;
};

class AddonsTab : public QVLCFrame
{
    Q_OBJECT
    friend class PluginDialog;

protected:
    void keyPressEvent( QKeyEvent *keyEvent ) override;

private slots:
    void moreInformation();
    void installChecked( int );
    void reposync();
    void updateControls();

private:
    AddonsTab( qt_intf_t *p_intf );
    virtual ~AddonsTab();
    bool eventFilter ( QObject * watched, QEvent * event ) override;

    QListView *addonsView;
    QLineEdit *searchInput;
    QComboBox *category;
    QCheckBox *installedOnly;
    QPushButton *repositoryButton, *infoButton, *manageButton;
    QLabel *listInfo;
    QProgressBar *loadingIndicator;
    bool onlineCatalogRequested = false;
    QString restoreSelectionId;
    std::unique_ptr<AddonsModel> m_model;
};

class PluginTreeItem : public QTreeWidgetItem
{
public:
    PluginTreeItem(QStringList &qs_item, int Type = QTreeWidgetItem::Type)
            : QTreeWidgetItem (qs_item, Type) {}
    virtual ~PluginTreeItem() {}

    virtual bool operator< ( const QTreeWidgetItem & other ) const;
};

class ExtensionListModel : public QAbstractListModel
{

    Q_OBJECT

public:
    /* Safe copy of the extension_t struct */
    class ExtensionCopy
    {

    public:
        ExtensionCopy( extension_t * );
        ~ExtensionCopy();
        QVariant data( int role ) const;

    private:
        QString name, title, description, shortdesc, author, version, url;
        QPixmap *icon;
    };

    ExtensionListModel( QObject *parent, ExtensionsManager *EM );
    ExtensionListModel( QObject *parent = 0 );
    virtual ~ExtensionListModel();

    enum
    {
        SummaryRole = Qt::UserRole,
        VersionRole,
        AuthorRole,
        LinkRole,
        FilenameRole,
        DescriptionRole
    };

    QVariant data( const QModelIndex& index, int role ) const override;
    QModelIndex index( int row, int column = 0,
                       const QModelIndex& = QModelIndex() ) const override;
    int rowCount( const QModelIndex& = QModelIndex() ) const override;

protected slots:
    void updateList();

private:
    ExtensionsManager *EM;
    QList<ExtensionCopy*> extensions;
};

class ExtensionItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    ExtensionItemDelegate( QObject *parent );
    virtual ~ExtensionItemDelegate();

    void paint( QPainter *painter,
                const QStyleOptionViewItem &option,
                const QModelIndex &index ) const override;
    QSize sizeHint( const QStyleOptionViewItem &option,
                    const QModelIndex &index ) const override;
    void initStyleOption( QStyleOptionViewItem *option,
                          const QModelIndex &index ) const override;

protected:
    QMargins margins;
};


class AddonItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    AddonItemDelegate( QObject *parent );
    ~AddonItemDelegate();

    void paint( QPainter *painter,
                const QStyleOptionViewItem &option,
                const QModelIndex &index ) const override;
    QSize sizeHint( const QStyleOptionViewItem &option,
                    const QModelIndex &index ) const override;
    void initStyleOption( QStyleOptionViewItem *option,
                         const QModelIndex &index ) const override;
protected:
    QMargins margins;
};

class ExtensionInfoDialog : public QVLCDialog
{
public:
    ExtensionInfoDialog(const QModelIndex &index,
                         qt_intf_t *p_intf, QWindow *parent );
};

class AddonInfoDialog : public QVLCDialog
{
public:
    AddonInfoDialog(const QModelIndex &index,
                     qt_intf_t *p_intf, QWindow *parent );
};

#endif
