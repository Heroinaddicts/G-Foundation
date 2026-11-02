#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <set>
#include <string>
#include <vector>

namespace ScriptBuildStorage {
struct SourceSnapshot {
    QString Relative;
    QByteArray Bytes;
    bool Compile = false;
    bool HeaderEntry = false;
};
bool WriteIfChanged(const QString& path,const QByteArray& bytes,std::string& error);
bool Synchronize(const QString& root,const std::vector<SourceSnapshot>& sources,std::string& error);
QString FileHash(const QString& path,std::string& error);
QJsonObject ReadRecord(const QString& path);
bool WriteRecord(const QString& path,const QJsonObject& record,std::string& error);
QJsonObject SnapshotHashes(const QString& root,std::string& error);
bool SnapshotMatches(const QString& root,const QJsonObject& hashes);
bool ResetCMakeState(const QString& build,std::string& error);
}
