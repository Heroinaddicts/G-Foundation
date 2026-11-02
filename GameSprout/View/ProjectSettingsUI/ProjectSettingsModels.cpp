#include "ProjectSettingsModels.h"
#include <QString>

QVariant ProjectTagsModel::data(const QModelIndex& index,int role) const {
    if(!index.isValid()||index.row()<0||index.row()>=rowCount())return {};
    const auto& tag=_Data.Tags[static_cast<std::size_t>(index.row())];
    if(role==Qt::DisplayRole||role==Qt::EditRole)return QString::fromStdString(tag.Name);
    if(role==Qt::ToolTipRole)return tag.ID?QStringLiteral("编辑名称不会改变对象的 Tag 引用。"):QStringLiteral("内置 Tag，不能修改或删除。");
    return {};
}
QVariant ProjectTagsModel::headerData(int,Qt::Orientation orientation,int role) const {
    return role==Qt::DisplayRole&&orientation==Qt::Horizontal?QVariant(QStringLiteral("Tag 名称")):QVariant{};
}
Qt::ItemFlags ProjectTagsModel::flags(const QModelIndex& index) const {
    if(!index.isValid())return Qt::NoItemFlags;
    auto result=Qt::ItemIsEnabled|Qt::ItemIsSelectable;
    if(_Editable&&_Data.Tags[static_cast<std::size_t>(index.row())].ID)result|=Qt::ItemIsEditable;
    return result;
}
bool ProjectTagsModel::setData(const QModelIndex& index,const QVariant& value,int role) {
    if(role!=Qt::EditRole||!(flags(index)&Qt::ItemIsEditable))return false;
    auto& name=_Data.Tags[static_cast<std::size_t>(index.row())].Name;
    const auto next=value.toString().trimmed().toStdString();if(next==name)return true;
    name=next;emit dataChanged(index,index);if(Edited)Edited();return true;
}
QVariant ProjectLayersModel::data(const QModelIndex& index,int role) const {
    if(!index.isValid()||index.row()<0||index.row()>=32)return {};
    if(role==Qt::DisplayRole||role==Qt::EditRole) {
        if(index.column()==0)return index.row();
        return QString::fromStdString(_Data.LayerNames[static_cast<std::size_t>(index.row())]);
    }
    if(role==Qt::ToolTipRole)return index.row()<3?QStringLiteral("内置 Layer，编号和名称固定。"):QStringLiteral("留空表示未命名；清空或改名都不会移动编号。");
    return {};
}
QVariant ProjectLayersModel::headerData(int section,Qt::Orientation orientation,int role) const {
    if(role==Qt::DisplayRole&&orientation==Qt::Horizontal)return section?QStringLiteral("Layer 名称"):QStringLiteral("编号");return {};
}
Qt::ItemFlags ProjectLayersModel::flags(const QModelIndex& index) const {
    if(!index.isValid())return Qt::NoItemFlags;
    auto result=Qt::ItemIsEnabled|Qt::ItemIsSelectable;
    if(_Editable&&index.column()==1&&index.row()>=3)result|=Qt::ItemIsEditable;return result;
}
bool ProjectLayersModel::setData(const QModelIndex& index,const QVariant& value,int role) {
    if(role!=Qt::EditRole||!(flags(index)&Qt::ItemIsEditable))return false;
    auto& name=_Data.LayerNames[static_cast<std::size_t>(index.row())];
    const auto next=value.toString().trimmed().toStdString();if(next==name)return true;
    name=next;emit dataChanged(index,index);if(Edited)Edited();return true;
}
void ProjectCollisionModel::RefreshLayers() {
    beginResetModel();_Layers.clear();
    for(std::uint8_t layer=0;layer<32;++layer)if(!_Data.LayerNames[layer].empty())_Layers.push_back(layer);
    endResetModel();
}
QVariant ProjectCollisionModel::data(const QModelIndex& index,int role) const {
    if(!index.isValid()||index.row()<0||index.column()<index.row()||index.column()>=columnCount())return {};
    const auto a=_Layers[static_cast<std::size_t>(index.row())],b=_Layers[static_cast<std::size_t>(index.column())];
    if(role==Qt::CheckStateRole)return (_Data.CollisionMatrix[a]&(1u<<b))?Qt::Checked:Qt::Unchecked;
    if(role==Qt::ToolTipRole)return QString::fromStdString(_Data.LayerNames[a])+QStringLiteral(" ↔ ")+QString::fromStdString(_Data.LayerNames[b]);
    return {};
}
QVariant ProjectCollisionModel::headerData(int section,Qt::Orientation,int role) const {
    if(section<0||static_cast<std::size_t>(section)>=_Layers.size())return {};
    const auto layer=_Layers[static_cast<std::size_t>(section)];
    if(role==Qt::DisplayRole||role==Qt::ToolTipRole)return QString::fromStdString(_Data.LayerNames[layer]);return {};
}
Qt::ItemFlags ProjectCollisionModel::flags(const QModelIndex& index) const {
    if(!index.isValid()||index.column()<index.row())return Qt::NoItemFlags;
    return _Editable?Qt::ItemIsEnabled|Qt::ItemIsUserCheckable:Qt::ItemIsEnabled;
}
bool ProjectCollisionModel::setData(const QModelIndex& index,const QVariant& value,int role) {
    if(role!=Qt::CheckStateRole||!(flags(index)&Qt::ItemIsUserCheckable))return false;
    const auto a=_Layers[static_cast<std::size_t>(index.row())],b=_Layers[static_cast<std::size_t>(index.column())];
    const bool enabled=value.toInt()==Qt::Checked;
    if(enabled){_Data.CollisionMatrix[a]|=1u<<b;_Data.CollisionMatrix[b]|=1u<<a;}
    else{_Data.CollisionMatrix[a]&=~(1u<<b);_Data.CollisionMatrix[b]&=~(1u<<a);}
    emit dataChanged(index,index,{Qt::CheckStateRole});if(Edited)Edited();return true;
}
