#pragma once
#include "SceneObjectDragPayload.h"
namespace SceneComponentDragPayload {
struct Value {SceneObjectDragPayload::Value Object;std::uint64_t ComponentID=0;};
inline const char* MimeType(){return "application/x-gamesprout-scene-component";}
inline QMimeData* Encode(const Value& value){
    if(!value.ComponentID)return nullptr;
    auto* mime=SceneObjectDragPayload::Encode(value.Object);if(!mime)return nullptr;
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_5_15);
    stream<<quint32(0x47534352)<<quint16(1)<<quint16(0)<<quint64(value.ComponentID);
    mime->setData(MimeType(),bytes);return mime;
}
inline bool Decode(const QMimeData* mime,Value& result){
    result={};if(!mime||!mime->hasFormat(MimeType())||!SceneObjectDragPayload::Decode(mime,result.Object))return false;
    const auto bytes=mime->data(MimeType());if(bytes.size()!=16)return false;
    QDataStream stream(bytes);stream.setVersion(QDataStream::Qt_5_15);quint32 magic=0;quint16 version=0,reserved=0;quint64 id=0;
    stream>>magic>>version>>reserved>>id;
    if(stream.status()!=QDataStream::Ok||!stream.atEnd()||magic!=0x47534352||version!=1||reserved||!id)return false;
    result.ComponentID=id;return true;
}
}
