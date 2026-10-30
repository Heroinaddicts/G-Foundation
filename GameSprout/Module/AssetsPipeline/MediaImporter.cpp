#include "AssetImportJob.h"
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>

namespace {
QString Tool(const QString & name) {
#if defined(_WIN32)
    const QString filename = name + ".exe";
#else
    const QString filename = name;
#endif
    const QDir application(QCoreApplication::applicationDirPath());
    for (const QString & candidate : {application.filePath("local_tools/ffmpeg/bin/" + filename),
        application.filePath("local_tools/ffmpeg/" + filename), application.filePath(filename)}) {
        if (QFileInfo(candidate).isExecutable()) return candidate;
    }
    return QStandardPaths::findExecutable(filename);
}
bool Execute(AssetImportJob & job, const QString & program, const QStringList & arguments, QByteArray & output) {
    if (program.isEmpty()) return job.Fail("Required media tool is not installed/deployed (ffmpeg/ffprobe)");
    QProcess process;
    process.setProgram(program); process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(5000)) return job.Fail("Cannot start " + program.toStdString() + ": " + process.errorString().toStdString());
    QByteArray errors;
    while (!process.waitForFinished(100)) {
        output += process.readAllStandardOutput(); errors += process.readAllStandardError();
        if (errors.size() > 65536) errors = errors.right(65536);
        if (job.Cancelled()) { process.kill(); process.waitForFinished(5000); return false; }
        if (process.state() == QProcess::NotRunning) break;
    }
    output += process.readAllStandardOutput(); errors += process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return job.Fail("Media tool failed: " + errors.right(65536).toStdString());
    return true;
}
}

bool ImportMedia(AssetImportJob & job) {
    const bool audio = job.Request.Importer == eAssetImporterType::Audio;
    const QDir stage(QString::fromStdString(job.Stage));
    const QString source = stage.filePath("input." + QFileInfo(QString::fromStdString(job.Request.SourcePath)).suffix());
    if (!AssetArtifactIO::Write(source,job.SourceBytes,job.Result.Error)) return false;
    QByteArray probe;
    if (!Execute(job,Tool("ffprobe"),{"-v","error","-show_streams","-show_format","-of","json",source},probe)) return false;
    Json::Value info;
    if (!AssetArtifactIO::Parse(probe.toStdString(),info) || !info["streams"].isArray()) return job.Fail("Invalid ffprobe result");
    Json::Value track;
    for (const auto & stream : info["streams"]) if (stream.get("codec_type","").asString() == (audio ? "audio" : "video")) { track = stream; break; }
    if (track.isNull()) return job.Fail(audio ? "No audio stream" : "No video stream");
    Json::Value metadata;
    metadata["SourceInfo"] = info;
    metadata["Settings"] = job.Settings;
    QByteArray payload;
    QString format;
    if (audio || job.Settings.get("Transcode",true).asBool()) {
        QStringList arguments{"-nostdin","-v","error","-y","-i",source};
        if (audio) {
            arguments << "-vn";
            const bool mono = job.Settings.get("ForceToMono",false).asBool();
            if (mono && job.Settings.get("Ambisonic",false).asBool()) return job.Fail("Ambisonic audio cannot be collapsed to mono");
            if (mono) arguments << "-ac" << "1";
            QStringList filters;
            if (mono && job.Settings.get("Normalize",true).asBool()) filters << "loudnorm=I=-16:TP=-1.5:LRA=11";
            if (!filters.empty()) arguments << "-af" << filters.join(",");
            const std::string rate = job.Settings.get("SampleRateMode","Preserve").asString();
            const int inputRate = QString::fromStdString(track.get("sample_rate","0").asString()).toInt();
            if (inputRate <= 0) return job.Fail("Audio stream has no valid sample rate");
            if (rate == "Override") arguments << "-ar" << QString::number(job.Settings.get("SampleRateOverride",44100).asUInt());
            else if (rate == "Optimize") arguments << "-ar" << QString::number(std::min(std::max(8000,inputRate),48000));
            else if (rate == "Preserve") arguments << "-ar" << QString::number(inputRate);
            else return job.Fail("Invalid audio sample rate mode");
            const std::string codec = job.Settings.get("CompressionFormat","Vorbis").asString();
            if (codec == "Vorbis") {
                format="ogg"; arguments << "-c:a" << "libvorbis" << "-q:a" << QString::number(job.Settings.get("Quality",0.7).asDouble()*10);
            } else if (codec == "ADPCM") { format="wav"; arguments << "-c:a" << "adpcm_ima_wav"; }
            else if (codec == "PCM") { format="wav"; arguments << "-c:a" << "pcm_s16le"; }
            else return job.Fail("Unsupported audio encoding");
        } else {
            const bool alpha = job.Settings.get("EncodeAlpha",false).asBool();
            std::string codec=job.Settings.get("Codec","Auto").asString();
            if (codec == "Auto") codec=alpha ? "VP8" : "H264";
            if (alpha && codec != "VP8") return job.Fail("Selected video codec cannot preserve alpha");
            const int width=job.Settings.get("MaxWidth",1920).asInt(), height=job.Settings.get("MaxHeight",1080).asInt();
            if (width < 2 || height < 2) return job.Fail("Video maximum dimensions must be at least 2");
            QStringList filters;
            filters << QString("scale=w='min(iw,%1)':h='min(ih,%2)':force_original_aspect_ratio=decrease:force_divisible_by=2").arg(width).arg(height);
            if (job.Settings.get("FlipHorizontal",false).asBool()) filters << "hflip";
            if (job.Settings.get("FlipVertical",false).asBool()) filters << "vflip";
            arguments << "-vf" << filters.join(",");
            if (!job.Settings.get("ImportAudio",true).asBool()) arguments << "-an";
            if (codec=="VP8") {format="webm";arguments << "-c:v" << "libvpx" << "-auto-alt-ref" << "0" << "-pix_fmt" << (alpha ? "yuva420p" : "yuv420p") << "-c:a" << "libvorbis";}
            else if (codec=="H264" || codec=="H265") {format="mp4";arguments << "-c:v" << (codec=="H264" ? "libx264" : "libx265") << "-pix_fmt" << "yuv420p" << "-c:a" << "aac" << "-movflags" << "+faststart";}
            else return job.Fail("Unsupported video codec");
        }
        const QString output=stage.filePath("encoded."+format);
        arguments << output;
        QByteArray ignored;
        if (!Execute(job,Tool("ffmpeg"),arguments,ignored)) return false;
        QFile result(output);
        if (!result.open(QIODevice::ReadOnly)) return job.Fail("Cannot read encoded media");
        payload=result.readAll();
        if (result.error()!=QFileDevice::NoError) return job.Fail("Cannot read complete encoded media");
        QByteArray encodedProbe;
        if (!Execute(job,Tool("ffprobe"),{"-v","error","-show_streams","-show_format","-of","json",output},encodedProbe)) return false;
        Json::Value encodedInfo;
        if (!AssetArtifactIO::Parse(encodedProbe.toStdString(),encodedInfo)) return job.Fail("Cannot validate encoded media");
        metadata["EncodedInfo"]=encodedInfo;
        result.close(); QFile::remove(output);
    } else {
        if (job.Settings.get("FlipHorizontal",false).asBool() || job.Settings.get("FlipVertical",false).asBool()
            || job.Settings.get("EncodeAlpha",false).asBool()) return job.Fail("Video pixel processing requires Transcode");
        format=QFileInfo(source).suffix(); payload=job.SourceBytes;
    }
    QFile::remove(source);
    metadata["Container"]=format.toStdString();
    return job.Emit(audio ? eAssetType::Audio : eAssetType::Video,"main",
        QFileInfo(QString::fromStdString(job.Request.SourcePath)).completeBaseName().toStdString(),metadata,payload);
}
