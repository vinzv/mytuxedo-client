/*
 * Copyright (C) by Klaas Freitag <freitag@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "folderstatusmodel.h"
#include "folderman.h"
#include "utility.h"
#include <theme.h>
#include <account.h>

#include <QtCore>
#include <QtGui>
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QtWidgets>
#endif

Q_DECLARE_METATYPE(QPersistentModelIndex)

namespace OCC {

static const char propertyParentIndexC[] = "oc_parentIndex";

FolderStatusModel::FolderStatusModel(QObject *parent)
    :QAbstractItemModel(parent)
{
}

FolderStatusModel::~FolderStatusModel()
{ }


void FolderStatusModel::setAccount(const AccountPtr& account)
{
    beginResetModel();
    _dirty = false;
    _folders.clear();
    _account = account;
    endResetModel();
}


Qt::ItemFlags FolderStatusModel::flags ( const QModelIndex &index  ) const
{
    switch (classify(index)) {
        case AddButton:
            return Qt::ItemIsEnabled;
        case RootFolder:
            return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
        case SubFolder:
            return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    }
    return 0;
}

QVariant FolderStatusModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (role == Qt::EditRole)
        return QVariant();

    switch(classify(index)) {
    case AddButton:
        if (role == FolderStatusDelegate::AddButton)
            return QVariant(true);
        return QVariant();
    case SubFolder:
    {
        const auto &x = static_cast<SubFolderInfo *>(index.internalPointer())->_subs[index.row()];
        switch (role) {
        case Qt::ToolTipRole:
        case Qt::DisplayRole:
            return x._name;
        case Qt::CheckStateRole:
            return x._checked;
        case Qt::DecorationRole:
            return QFileIconProvider().icon(QFileIconProvider::Folder);
        }
    }
        return QVariant();
    case RootFolder:
        break;
    }

    auto folderList = FolderMan::instance()->map().values();
    auto f = folderList.at(index.row());
    if (!f)
        return QVariant();

    bool accountConnected = true; // FIXME

    switch (role) {
    case FolderStatusDelegate::FolderPathRole         : return  f->nativePath();
    case FolderStatusDelegate::FolderSecondPathRole   : return  f->remotePath();
    case FolderStatusDelegate::FolderAliasRole        : return  f->alias();
    case FolderStatusDelegate::FolderSyncPaused       : return  f->syncPaused();
    case FolderStatusDelegate::FolderAccountConnected : return  accountConnected;
    case Qt::ToolTipRole:
        return Theme::instance()->statusHeaderText(f->syncResult().status());
    case FolderStatusDelegate::FolderStatusIconRole:
        if ( accountConnected ) {
            auto theme = Theme::instance();
            auto status = f->syncResult().status();
            if( f->syncPaused() ) {
                return theme->folderDisabledIcon( );
            } else {
                if( status == SyncResult::SyncPrepare ) {
                    return theme->syncStateIcon(SyncResult::SyncRunning);
                } else if( status == SyncResult::Undefined ) {
                    return theme->syncStateIcon( SyncResult::SyncRunning);
                } else {
                    // kepp the previous icon for the prepare phase.
                    if( status == SyncResult::Problem) {
                        return theme->syncStateIcon( SyncResult::Success);
                    } else {
                        return theme->syncStateIcon( status );
                    }
                }
            }
        } else {
            return Theme::instance()->folderOfflineIcon();
        }
    case FolderStatusDelegate::AddProgressSpace:
        return !_progresses.value(index.row()).isNull();
    case FolderStatusDelegate::SyncProgressItemString:
        return _progresses.value(index.row())._progressString;
    case FolderStatusDelegate::WarningCount:
        return _progresses.value(index.row())._warningCount;
    case FolderStatusDelegate::SyncProgressOverallPercent:
        return _progresses.value(index.row())._overallPercent;
    case FolderStatusDelegate::SyncProgressOverallString:
        return _progresses.value(index.row())._overallSyncString;
    }
    return QVariant();
}

bool FolderStatusModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if(role == Qt::CheckStateRole) {
        auto info = infoForIndex(index);
        Qt::CheckState checked = static_cast<Qt::CheckState>(value.toInt());

        if (info && info->_checked != checked) {
            info->_checked = checked;
            if (checked == Qt::Checked) {
                // If we are checked, check that we may need to check the parent as well if
                // all the sibilings are also checked
                QModelIndex parent = index.parent();
                auto parentInfo = infoForIndex(parent);
                if (parentInfo && parentInfo->_checked != Qt::Checked) {
                    bool hasUnchecked = false;
                    foreach(const auto &sub, parentInfo->_subs) {
                        if (sub._checked != Qt::Checked) {
                            hasUnchecked = true;
                            break;
                        }
                    }
                    if (!hasUnchecked) {
                        setData(parent, Qt::Checked, Qt::CheckStateRole);
                    } else if (parentInfo->_checked == Qt::Unchecked) {
                        setData(parent, Qt::PartiallyChecked, Qt::CheckStateRole);
                    }
                }
                // also check all the children
                for (int i = 0; i < info->_subs.count(); ++i) {
                    if (info->_subs[i]._checked != Qt::Checked) {
                        setData(index.child(i, 0), Qt::Checked, Qt::CheckStateRole);
                    }
                }
            }

            if (checked == Qt::Unchecked) {
                QModelIndex parent = index.parent();
                auto parentInfo = infoForIndex(parent);
                if (parentInfo && parentInfo->_checked == Qt::Checked) {
                    setData(parent, Qt::PartiallyChecked, Qt::CheckStateRole);
                }

                // Uncheck all the children
                for (int i = 0; i < info->_subs.count(); ++i) {
                    if (info->_subs[i]._checked != Qt::Unchecked) {
                        setData(index.child(i, 0), Qt::Unchecked, Qt::CheckStateRole);
                    }
                }
            }

            if (checked == Qt::PartiallyChecked) {
                QModelIndex parent = index.parent();
                auto parentInfo = infoForIndex(parent);
                if (parentInfo && parentInfo->_checked != Qt::PartiallyChecked) {
                    setData(parent, Qt::PartiallyChecked, Qt::CheckStateRole);
                }
            }

        }
        _dirty = true;
        emit dirtyChanged();
        dataChanged(index, index, QVector<int>() << role);
        return true;
    }
    return QAbstractItemModel::setData(index, value, role);
}


int FolderStatusModel::columnCount(const QModelIndex&) const
{
    return 1;
}

int FolderStatusModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return FolderMan::instance()->map().count() + 1;
    }

    auto info = infoForIndex(parent);
    if (!info)
        return 0;
    return info->_subs.count();
}

FolderStatusModel::ItemType FolderStatusModel::classify(const QModelIndex& index) const
{
    if (index.internalPointer()) {
        return SubFolder;
    }
    //FIXME:
    auto folderList = FolderMan::instance()->map(); //.values();
    if (index.row() < folderList.count()) {
        return RootFolder;
    }
    return AddButton;
}

FolderStatusModel::SubFolderInfo* FolderStatusModel::infoForIndex(const QModelIndex& index) const
{
    if (!index.isValid())
        return 0;
    if (auto parentInfo = index.internalPointer()) {
        return &static_cast<SubFolderInfo*>(parentInfo)->_subs[index.row()];
    } else {
        auto folders = FolderMan::instance()->map(); // FIXME
        if (index.row() >= folders.count()) {
            // AddButton
            return 0;
        }
        if (_folders.size() <=  index.row()) {
            _folders.resize(index.row() + 1);
        }
        auto info = &_folders[index.row()];
        if (info->_pathIdx.isEmpty()) {
            info->_pathIdx << index.row();
            info->_name = folders.values().at(index.row())->alias();
            info->_path = "/";
            info->_folder = folders.values().at(index.row());
        }
        return info;
    }
}


QModelIndex FolderStatusModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return createIndex(row, column, nullptr);
    }
    switch(classify(parent)) {
        case AddButton: return QModelIndex();
        case RootFolder:
            if (_folders.count() <= parent.row())
                return QModelIndex(); // should not happen
            return createIndex(row, column, const_cast<SubFolderInfo *>(&_folders[parent.row()]));
        case SubFolder:
            //return QModelIndex();
            if (static_cast<SubFolderInfo*>(parent.internalPointer())->_subs.count() <= parent.row())
                return QModelIndex(); // should not happen
            if (static_cast<SubFolderInfo*>(parent.internalPointer())->_subs.at(parent.row())._subs.count() <= row)
                return QModelIndex(); // should not happen
            return createIndex(row, column, &static_cast<SubFolderInfo*>(parent.internalPointer())->_subs[parent.row()]);
    }
    return QModelIndex();
}

QModelIndex FolderStatusModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return QModelIndex();
    }
    switch(classify(child)) {
        case RootFolder:
        case AddButton:
            return QModelIndex();
        case SubFolder:
            break;
    }
    auto pathIdx = static_cast<SubFolderInfo*>(child.internalPointer())->_subs[child.row()]._pathIdx;
    int i = 1;
    Q_ASSERT(pathIdx.at(0) < _folders.count());
    if (pathIdx.count() == 2) {
        return createIndex(pathIdx.at(0), 0, nullptr);
    }

    const SubFolderInfo *info = &_folders[pathIdx.at(0)];
    while (i < pathIdx.count() - 2) {
        Q_ASSERT(pathIdx.at(i) < info->_subs.count());
        info = &info->_subs[pathIdx.at(i)];
        ++i;
    }
    return createIndex(pathIdx.at(i), 0, const_cast<SubFolderInfo *>(info));
}

bool FolderStatusModel::hasChildren(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return true;

    auto info = infoForIndex(parent);
    if (!info)
        return false;

    if (!info->_fetched)
        return true;

    if (info->_subs.isEmpty())
        return false;

    return true;
}


bool FolderStatusModel::canFetchMore(const QModelIndex& parent) const
{
    auto info = infoForIndex(parent);
    if (!info || info->_fetched || info->_fetching)
        return false;
    return true;
}


void FolderStatusModel::fetchMore(const QModelIndex& parent)
{
    auto info = infoForIndex(parent);
    if (!info || info->_fetched || info->_fetching)
        return;

    info->_fetching = true;
    LsColJob *job = new LsColJob(_account, info->_folder->remotePath() + "/" + info->_path, this);
    job->setProperties(QList<QByteArray>() << "resourcetype" << "quota-used-bytes");
    connect(job, SIGNAL(directoryListingSubfolders(QStringList)),
            SLOT(slotUpdateDirectories(QStringList)));
    job->start();
    job->setProperty(propertyParentIndexC , QVariant::fromValue<QPersistentModelIndex>(parent));
}

void FolderStatusModel::slotUpdateDirectories(const QStringList &list_)
{
    auto job = qobject_cast<LsColJob *>(sender());
    Q_ASSERT(job);
    QModelIndex idx = qvariant_cast<QPersistentModelIndex>(job->property(propertyParentIndexC));
    if (!idx.isValid()) {
        return;
    }
    auto parentInfo = infoForIndex(idx);

    auto list = list_;
    list.removeFirst(); // remove the parent item

    beginInsertRows(idx, 0, list.count());

    QUrl url = parentInfo->_folder->remoteUrl();
    QString pathToRemove = url.path();
    if (!pathToRemove.endsWith('/'))
        pathToRemove += '/';

    parentInfo->_fetched = true;
    parentInfo->_fetching = false;

    int i = 0;
    foreach (QString path, list) {
        SubFolderInfo newInfo;
        newInfo._folder = parentInfo->_folder;
        newInfo._pathIdx = parentInfo->_pathIdx;
        newInfo._pathIdx << i++;
        auto size = job ? job->_sizes.value(path) : 0;
        newInfo._size = size;
        path.remove(pathToRemove);
        newInfo._path = path;
        newInfo._name = path.split('/', QString::SkipEmptyParts).last();

        if (path.isEmpty())
            continue;

        if (parentInfo->_checked == Qt::Unchecked) {
            newInfo._checked = Qt::Unchecked;
        } else {
            auto *f = FolderMan::instance()->map().values().at(parentInfo->_pathIdx.first());
            foreach(const QString &str , f->selectiveSyncBlackList()) {
                if (str == path || str == QLatin1String("/")) {
                    newInfo._checked = Qt::Unchecked;
                    break;
                } else if (str.startsWith(path)) {
                    newInfo._checked = Qt::PartiallyChecked;
                }
            }
        }
        parentInfo->_subs.append(newInfo);
    }

    endInsertRows();
}

/*void SelectiveSyncTreeView::slotLscolFinishedWithError(QNetworkReply *r)
{
    if (r->error() == QNetworkReply::ContentNotFoundError) {
        _loading->setText(tr("No subfolders currently on the server."));
    } else {
        _loading->setText(tr("An error occured while loading the list of sub folders."));
    }
    _loading->resize(_loading->sizeHint()); // because it's not in a layout
}*/

QStringList FolderStatusModel::createBlackList(FolderStatusModel::SubFolderInfo *root,
                                               const QStringList &oldBlackList) const
{
    if (!root) return QStringList();

    switch(root->_checked) {
        case Qt::Unchecked:
            return QStringList(root->_path);
        case  Qt::Checked:
            return QStringList();
        case Qt::PartiallyChecked:
            break;
    }

    QStringList result;
    if (root->_fetched) {
        for (int i = 0; i < root->_subs.count(); ++i) {
            result += createBlackList(&root->_subs[i], oldBlackList);
        }
    } else {
        // We did not load from the server so we re-use the one from the old black list
        QString path = root->_path;
        foreach (const QString & it, oldBlackList) {
            if (it.startsWith(path))
                result += it;
        }
    }
    return result;
}

void FolderStatusModel::slotApplySelectiveSync()
{
    if (!_dirty)
        return;
    auto folderList = FolderMan::instance()->map().values(); //FIXME

    for (int i = 0; i < folderList.count(); ++i) {
        if (i >= _folders.count()) break;
        if (!_folders[i]._fetched) continue;
        auto folder = folderList.at(i);

        auto oldBlackList = folder->selectiveSyncBlackList();
        QStringList blackList = createBlackList(&_folders[i], oldBlackList);
        folder->setSelectiveSyncBlackList(blackList);

        FolderMan *folderMan = FolderMan::instance();
        auto blackListSet = blackList.toSet();
        auto oldBlackListSet = oldBlackList.toSet();
        auto changes = (oldBlackListSet - blackListSet) + (blackListSet - oldBlackListSet);
        if (!changes.isEmpty()) {
            if (folder->isBusy()) {
                folder->slotTerminateSync();
            }
            //The part that changed should not be read from the DB on next sync because there might be new folders
            // (the ones that are no longer in the blacklist)
            foreach(const auto &it, changes) {
                folder->journalDb()->avoidReadFromDbOnNextSync(it);
            }
            folderMan->slotScheduleSync(folder->alias());
        }
    }

    resetFolders();
}


void FolderStatusModel::resetFolders()
{
    setAccount(_account);
}


// ====================================================================================

FolderStatusDelegate::FolderStatusDelegate()
    :QStyledItemDelegate()
{

}

FolderStatusDelegate::~FolderStatusDelegate()
{
  // TODO Auto-generated destructor stub
}

//alocate each item size in listview.
QSize FolderStatusDelegate::sizeHint(const QStyleOptionViewItem & option ,
                                   const QModelIndex & index) const
{

    if (static_cast<const FolderStatusModel *>(index.model())->classify(index) != FolderStatusModel::RootFolder) {
        return QStyledItemDelegate::sizeHint(option, index);
    }


  Q_UNUSED(option)
  QFont aliasFont = option.font;
  QFont font = option.font;
  aliasFont.setPointSize( font.pointSize() +2 );

  QFontMetrics fm(font);
  QFontMetrics aliasFm(aliasFont);

  int aliasMargin = aliasFm.height()/2;
  int margin = fm.height()/4;

  // calc height

  int h = aliasMargin;         // margin to top
  h += aliasFm.height();       // alias
  h += margin;                 // between alias and local path
  h += fm.height();            // local path
  h += margin;                 // between local and remote path
  h += fm.height();            // remote path
  h += aliasMargin;            // bottom margin

  // add some space to show an error condition.
  if( ! qvariant_cast<QStringList>(index.data(FolderErrorMsg)).isEmpty() ) {
      QStringList errMsgs = qvariant_cast<QStringList>(index.data(FolderErrorMsg));
      h += aliasMargin*2 + errMsgs.count()*fm.height();
  }

  if( qvariant_cast<bool>(index.data(AddProgressSpace)) ) {
      int margin = fm.height()/4;
      h += (5 * margin); // All the margins
      h += 2* fm.boundingRect(tr("File")).height();
  }

  return QSize( 0, h);
}

void FolderStatusDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
  if (qvariant_cast<bool>(index.data(AddButton))) {
      painter->drawText(option.rect, "[+ Add Folder]");
      return;
  }


  QStyledItemDelegate::paint(painter,option,index);

  if (static_cast<const FolderStatusModel *>(index.model())->classify(index) != FolderStatusModel::RootFolder) {
      return;
  }
  painter->save();

  QFont aliasFont = option.font;
  QFont subFont   = option.font;
  QFont errorFont = subFont;
  QFont progressFont = subFont;

  progressFont.setPointSize( subFont.pointSize()-2);
  //font.setPixelSize(font.weight()+);
  aliasFont.setBold(true);
  aliasFont.setPointSize( subFont.pointSize()+2 );

  QFontMetrics subFm( subFont );
  QFontMetrics aliasFm( aliasFont );
  QFontMetrics progressFm( progressFont );

  int aliasMargin = aliasFm.height()/2;
  int margin = subFm.height()/4;

  QIcon statusIcon      = qvariant_cast<QIcon>(index.data(FolderStatusIconRole));
  QString aliasText     = qvariant_cast<QString>(index.data(FolderAliasRole));
  QString pathText      = qvariant_cast<QString>(index.data(FolderPathRole));
  QString remotePath    = qvariant_cast<QString>(index.data(FolderSecondPathRole));
  QStringList errorTexts= qvariant_cast<QStringList>(index.data(FolderErrorMsg));

  int overallPercent    = qvariant_cast<int>(index.data(SyncProgressOverallPercent));
  QString overallString = qvariant_cast<QString>(index.data(SyncProgressOverallString));
  QString itemString    = qvariant_cast<QString>(index.data(SyncProgressItemString));
  int warningCount      = qvariant_cast<int>(index.data(WarningCount));
  bool syncOngoing      = qvariant_cast<bool>(index.data(SyncRunning));

  // QString statusText = qvariant_cast<QString>(index.data(FolderStatus));
  bool syncEnabled = index.data(FolderAccountConnected).toBool();
  // QString syncStatus = syncEnabled? tr( "Enabled" ) : tr( "Disabled" );

  QRect iconRect = option.rect;
  QRect aliasRect = option.rect;

  iconRect.setLeft( option.rect.left() + aliasMargin );
  iconRect.setTop( iconRect.top() + aliasMargin ); // (iconRect.height()-iconsize.height())/2);

  // alias box
  aliasRect.setTop(aliasRect.top() + aliasMargin );
  aliasRect.setBottom(aliasRect.top() + aliasFm.height());
  aliasRect.setRight(aliasRect.right() - aliasMargin );

  // remote directory box
  QRect remotePathRect = aliasRect;
  remotePathRect.setTop(aliasRect.bottom() + margin );
  remotePathRect.setBottom(remotePathRect.top() + subFm.height());

  // local directory box
  QRect localPathRect = remotePathRect;
  localPathRect.setTop( remotePathRect.bottom() + margin );
  localPathRect.setBottom( localPathRect.top() + subFm.height());

  iconRect.setBottom(localPathRect.bottom());
  iconRect.setWidth(iconRect.height());

  int nextToIcon = iconRect.right()+aliasMargin;
  aliasRect.setLeft(nextToIcon);
  localPathRect.setLeft(nextToIcon);
  remotePathRect.setLeft(nextToIcon);

  int iconSize = iconRect.width();

  QPixmap pm = statusIcon.pixmap(iconSize, iconSize, syncEnabled ? QIcon::Normal : QIcon::Disabled );
  painter->drawPixmap(QPoint(iconRect.left(), iconRect.top()), pm);

  // only show the warning icon if the sync is running. Otherwise its
  // encoded in the status icon.
  if( warningCount > 0 && syncOngoing) {
      QRect warnRect;
      warnRect.setLeft(iconRect.left());
      warnRect.setTop(iconRect.bottom()-17);
      warnRect.setWidth(16);
      warnRect.setHeight(16);

      QIcon warnIcon(":/client/resources/warning");
      QPixmap pm = warnIcon.pixmap(16,16, syncEnabled ? QIcon::Normal : QIcon::Disabled );
      painter->drawPixmap(QPoint(warnRect.left(), warnRect.top()),pm );
  }

  auto palette = option.palette;

  if (qApp->style()->inherits("QWindowsVistaStyle")) {
      // Hack: Windows Vista's light blue is not contrasting enough for white

      // (code from QWindowsVistaStyle::drawControl for CE_ItemViewItem)
      palette.setColor(QPalette::All, QPalette::HighlightedText, palette.color(QPalette::Active, QPalette::Text));
      palette.setColor(QPalette::All, QPalette::Highlight, palette.base().color().darker(108));
  }


  QPalette::ColorGroup cg = option.state & QStyle::State_Enabled
                          ? QPalette::Normal : QPalette::Disabled;
  if (cg == QPalette::Normal && !(option.state & QStyle::State_Active))
      cg = QPalette::Inactive;

  if (option.state & QStyle::State_Selected) {
      painter->setPen(palette.color(cg, QPalette::HighlightedText));
  } else {
      painter->setPen(palette.color(cg, QPalette::Text));
  }
  QString elidedAlias = aliasFm.elidedText(aliasText, Qt::ElideRight, aliasRect.width());
  painter->setFont(aliasFont);
  painter->drawText(aliasRect, elidedAlias);

  painter->setFont(subFont);
  QString elidedRemotePathText;

  if (remotePath.isEmpty() || remotePath == QLatin1String("/")) {
      elidedRemotePathText = subFm.elidedText(tr("Syncing all files in your account with"),
                                              Qt::ElideRight, remotePathRect.width());
  } else {
      elidedRemotePathText = subFm.elidedText(tr("Remote path: %1").arg(remotePath),
                                              Qt::ElideMiddle, remotePathRect.width());
  }
  painter->drawText(remotePathRect, elidedRemotePathText);

  QString elidedPathText = subFm.elidedText(pathText, Qt::ElideMiddle, localPathRect.width());
  painter->drawText(localPathRect, elidedPathText);

  // paint an error overlay if there is an error string

  int h = iconRect.bottom();
  if( !errorTexts.isEmpty() ) {
      h += aliasMargin;
      QRect errorRect = localPathRect;
      errorRect.setLeft( iconRect.left());
      errorRect.setTop( h );
      errorRect.setHeight(errorTexts.count() * subFm.height()+aliasMargin);
      errorRect.setRight( option.rect.right()-aliasMargin );

      painter->setBrush( QColor(0xbb, 0x4d, 0x4d) );
      painter->setPen( QColor(0xaa, 0xaa, 0xaa));
      painter->drawRoundedRect( errorRect, 4, 4 );

      painter->setPen( Qt::white );
      painter->setFont(errorFont);
      QRect errorTextRect = errorRect;
      errorTextRect.setLeft( errorTextRect.left()+aliasMargin );
      errorTextRect.setTop( errorTextRect.top()+aliasMargin/2 );

      int x = errorTextRect.left();
      int y = errorTextRect.top()+aliasMargin/2 + subFm.height()/2;

      foreach( QString eText, errorTexts ) {
          painter->drawText(x, y, subFm.elidedText( eText, Qt::ElideLeft, errorTextRect.width()-2*aliasMargin));
          y += subFm.height();
      }

      h = errorRect.bottom();
  }
  h += aliasMargin;

  // Sync File Progress Bar: Show it if syncFile is not empty.
  if( !overallString.isEmpty() || !itemString.isEmpty()) {
      int fileNameTextHeight = subFm.boundingRect(tr("File")).height();
      int barHeight = qMax(fileNameTextHeight, aliasFm.height()+4); ;
      int overallWidth = option.rect.width()-2*aliasMargin;

      painter->save();

      // Sizes-Text
      QRect octetRect = progressFm.boundingRect(QRect(), 0, overallString );
      int progressTextWidth = octetRect.width() + 2;

      // Overall Progress Bar.
      QRect pBRect;
      pBRect.setTop( h );
      pBRect.setLeft( iconRect.left());
      pBRect.setHeight(barHeight);
      pBRect.setWidth( overallWidth - progressTextWidth - margin );

      QStyleOptionProgressBarV2 pBarOpt;

      pBarOpt.state    = option.state | QStyle::State_Horizontal;
      pBarOpt.minimum  = 0;
      pBarOpt.maximum  = 100;
      pBarOpt.progress = overallPercent;
      pBarOpt.orientation = Qt::Horizontal;
      pBarOpt.palette = palette;
      pBarOpt.rect = pBRect;

      QApplication::style()->drawControl( QStyle::CE_ProgressBar, &pBarOpt, painter );

      // Overall Progress Text
      QRect overallProgressRect;
      overallProgressRect.setTop( pBRect.top() );
      overallProgressRect.setHeight( pBRect.height() );
      overallProgressRect.setLeft( pBRect.right()+margin);
      overallProgressRect.setWidth( progressTextWidth );
      painter->setFont(progressFont);

      painter->drawText( overallProgressRect, Qt::AlignRight+Qt::AlignVCenter, overallString);
    // painter->drawRect(overallProgressRect);

      // Individual File Progress
      QRect fileRect;
      fileRect.setTop( pBRect.bottom() + margin);
      fileRect.setLeft( iconRect.left());
      fileRect.setWidth(overallWidth);
      fileRect.setHeight(fileNameTextHeight);
      QString elidedText = progressFm.elidedText(itemString, Qt::ElideLeft, fileRect.width());

      painter->drawText( fileRect, Qt::AlignLeft+Qt::AlignVCenter, elidedText);

      painter->restore();
  }

  painter->restore();
}

bool FolderStatusDelegate::editorEvent ( QEvent * event, QAbstractItemModel * model,
                                         const QStyleOptionViewItem & option, const QModelIndex & index )
{
    return QStyledItemDelegate::editorEvent(event, model, option, index);
    return false;
}

} // namespace OCC