#pragma once
#include "RuntimeProjectSettings.h"
#include <QAbstractTableModel>
#include <functional>

// Models expose value snapshots. All widgets and layouts are defined in the View's .ui.
class ProjectSettingsModel : public QAbstractTableModel {
public:
    explicit ProjectSettingsModel(GameSproutRuntime::RuntimeProjectSettings& data,QObject* parent=nullptr)
        : QAbstractTableModel(parent),_Data(data) {}
    void Refresh(){beginResetModel();endResetModel();}
    void SetEditable(bool editable){if(_Editable!=editable){_Editable=editable;Refresh();}}
    std::function<void()> Edited;
protected:
    GameSproutRuntime::RuntimeProjectSettings& _Data;
    bool _Editable=true;
};
class ProjectTagsModel final : public ProjectSettingsModel {
public:
    using ProjectSettingsModel::ProjectSettingsModel;
    int rowCount(const QModelIndex& parent={}) const override {return parent.isValid()?0:static_cast<int>(_Data.Tags.size());}
    int columnCount(const QModelIndex& parent={}) const override {return parent.isValid()?0:1;}
    QVariant data(const QModelIndex&,int role) const override;
    QVariant headerData(int,Qt::Orientation,int) const override;
    Qt::ItemFlags flags(const QModelIndex&) const override;
    bool setData(const QModelIndex&,const QVariant&,int role) override;
};
class ProjectLayersModel final : public ProjectSettingsModel {
public:
    using ProjectSettingsModel::ProjectSettingsModel;
    int rowCount(const QModelIndex& parent={}) const override {return parent.isValid()?0:32;}
    int columnCount(const QModelIndex& parent={}) const override {return parent.isValid()?0:2;}
    QVariant data(const QModelIndex&,int role) const override;
    QVariant headerData(int,Qt::Orientation,int) const override;
    Qt::ItemFlags flags(const QModelIndex&) const override;
    bool setData(const QModelIndex&,const QVariant&,int role) override;
};
class ProjectCollisionModel final : public ProjectSettingsModel {
public:
    using ProjectSettingsModel::ProjectSettingsModel;
    int rowCount(const QModelIndex& parent={}) const override {return parent.isValid()?0:static_cast<int>(_Layers.size());}
    int columnCount(const QModelIndex& parent={}) const override {return rowCount(parent);}
    QVariant data(const QModelIndex&,int role) const override;
    QVariant headerData(int,Qt::Orientation,int) const override;
    Qt::ItemFlags flags(const QModelIndex&) const override;
    bool setData(const QModelIndex&,const QVariant&,int role) override;
    void RefreshLayers();
private:
    std::vector<std::uint8_t> _Layers;
};
