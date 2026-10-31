#pragma once
#include "ISelectionManager.h"
#include "IGuidCreator.h"
#include <QDataStream>
#include <QMimeData>
#include <QIODevice>

namespace AssetDragPayload {
inline const char* MimeType() { return "application/x-gamesprout-asset-reference"; }
inline bool Valid(const ProjectAssetSelection& value) {
    return Guid(value.GuidString).IsValid() && (value.IsSubAsset ? value.LocalID != 0 : value.LocalID == 0);
}
inline QMimeData* Encode(const std::vector<ProjectAssetSelection>& values) {
    if(values.empty() || values.size()>128) return nullptr;
    QByteArray bytes;
    QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream<<quint32(0x47534152)<<quint16(1)<<quint16(values.size());
    for(const auto& value:values) {
        if(!Valid(value)) return nullptr;
        const std::string guid=Guid(value.GuidString).GetGuidString();
        stream.writeRawData(guid.data(),32);stream<<quint64(value.LocalID)<<quint8(value.IsSubAsset?1:0);
    }
    if(stream.status()!=QDataStream::Ok) return nullptr;
    auto* mime=new QMimeData();mime->setData(MimeType(),bytes);return mime;
}
inline bool Decode(const QMimeData* mime,std::vector<ProjectAssetSelection>& values) {
    values.clear();
    if(!mime || !mime->hasFormat(MimeType()))return false;
    const QByteArray bytes=mime->data(MimeType());
    if(bytes.size()>65536)return false;
    QDataStream stream(bytes);stream.setVersion(QDataStream::Qt_5_15);
    quint32 magic=0;quint16 version=0,count=0;
    stream>>magic>>version>>count;
    if(magic!=0x47534152 || version!=1 || !count || count>128 || bytes.size()!=8+count*41)return false;
    std::vector<ProjectAssetSelection> result;
    for(quint16 index=0;index<count;++index) {
        char guid[32];quint64 id=0;quint8 child=0;
        if(stream.readRawData(guid,32)!=32)return false;
        stream>>id>>child;
        ProjectAssetSelection value{std::string(guid,32),id,child==1};
        if(stream.status()!=QDataStream::Ok || child>1 || !Valid(value))return false;
        value.GuidString=Guid(value.GuidString).GetGuidString();result.push_back(std::move(value));
    }
    if(!stream.atEnd())return false;
    values=std::move(result);return true;
}
}
