#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

class ScriptBuildProcess final {
public:
    struct Result {
        QByteArray Output;
        QByteArray ErrorOutput;
        QString Error;
        bool Cancelled = false;
    };
    // Every invocation owns a separate process tree. Cancellation completes only
    // after its descendants have exited, before the caller releases Build's lock.
    static bool Run(const QString& program,const QStringList& arguments,const QString& directory,
                    bool mergeChannels,qsizetype outputLimit,Result& result,
                    const std::function<bool()>& cancelled);
};
