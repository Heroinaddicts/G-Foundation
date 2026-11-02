#pragma once
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <cstdint>

// Hierarchy drags carry process-local identities, never object addresses. The
// receiver must validate them against its current world/session via a Module.
namespace SceneObjectDragPayload {
struct Value {
    std::uint64_t SessionID = 0;
    std::uint64_t WorldID = 0;
    std::uint64_t ObjectID = 0;
};
inline const char* MimeType() { return "application/x-gamesprout-scene-object"; }
inline QMimeData* Encode(const Value& value) {
    if (!value.SessionID || !value.WorldID || !value.ObjectID) return nullptr;
    QByteArray bytes;
    QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << quint32(0x47534f52) << quint16(1) << quint16(0)
           << quint64(value.SessionID) << quint64(value.WorldID) << quint64(value.ObjectID);
    if (stream.status()!=QDataStream::Ok) return nullptr;
    auto* mime=new QMimeData();
    mime->setData(MimeType(),bytes);
    return mime;
}
inline bool Decode(const QMimeData* mime,Value& value) {
    value={};
    if (!mime || !mime->hasFormat(MimeType())) return false;
    const QByteArray bytes=mime->data(MimeType());
    if (bytes.size()!=32) return false;
    QDataStream stream(bytes);
    stream.setVersion(QDataStream::Qt_5_15);
    quint32 magic=0;
    quint16 version=0,reserved=0;
    quint64 session=0,world=0,object=0;
    stream >> magic >> version >> reserved >> session >> world >> object;
    if (stream.status()!=QDataStream::Ok || !stream.atEnd() ||
        magic!=0x47534f52 || version!=1 || reserved || !session || !world || !object) return false;
    value={session,world,object};
    return true;
}
}
