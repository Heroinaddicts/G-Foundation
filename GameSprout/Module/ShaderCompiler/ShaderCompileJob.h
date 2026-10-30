#pragma once
#include "ShaderCompiler.h"
#include <JsonCpp/json.hpp>
#include <QDir>
#include <QDateTime>
#include <QStringList>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QCryptographicHash>

struct ShaderCompileJob {
    const ShaderCompileRequest & Request;
    const ShaderCompilerTools & Tools;
    ShaderCompileResult Result;
    QString Work;
    QString Input; // Flattened HLSL, produced from the final input snapshot.
    QString Stage;
    bool Fail(const std::string & error) {Result.Error=error;Result.Success=false;return false;}
    bool Cancelled() {return Request.IsCancellationRequested && Request.IsCancellationRequested() && FailCancelled();}
    bool FailCancelled() {Fail("Shader compilation cancelled");return true;}
    bool Run(const QString & program, const QStringList & arguments);
    bool Read(const QString & path, QByteArray & bytes);
    bool Write(const QString & path, const QByteArray & bytes);
};
std::string ShaderJsonText(const Json::Value & value);
bool ParseShaderJson(const QByteArray & bytes, Json::Value & value);
QStringList ShaderDefines(const ShaderCompileRequest & request);
bool PrepareShaderSource(ShaderCompileJob & job);
bool ValidateShaderReflection(ShaderCompileJob & job,const Json::Value & reflection);
bool CompileShaderTargets(ShaderCompileJob & job, Json::Value & descriptor);
