#pragma once
#include "IAssetsManager.h"
#include "IAssetsPipeline.h"
#include <QStandardItemModel>
#include <QColor>

class ProjectAssetsModel : public QStandardItemModel {
public:
    enum Role { GuidRole = Qt::UserRole + 1, PathRole, LocalIDRole, FolderRole, SubAssetRole, KeyRole, PlaceholderRole };
    explicit ProjectAssetsModel(QObject * parent);
    void BuildFolders(const std::vector<IAssetMeta *> & metas);
    void BuildAssets(const std::vector<IAssetMeta *> & metas);
    void LoadSubAssets(const QModelIndex & index, IAssetsManager * assets);
    QModelIndex FindKey(const QString & key, const QModelIndex & parent = {}) const;
    QColor ErrorColor;
    QColor PendingColor;
private:
    QStandardItem * EnsureFolder(const QString & path);
    QStandardItem * SourceItem(IAssetMeta * meta);
    QMap<QString, QStandardItem *> _Folders;
};
