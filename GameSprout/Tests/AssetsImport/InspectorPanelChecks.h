#pragma once
#include "ProjectViewTestSupport.h"
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <algorithm>

// Real importers and generated forms; no fake settings implementations.
inline void CheckInspectorPanels(QDir & root, IAssetsManager * manager, ILibraryManager * library,
    IInspectorUI * inspector, PreviewIDE & ide, std::string & stage,
    const std::function<void(const std::function<bool()> &)> & pump,
    const std::function<bool()> & idle, const std::function<void(const QString &)> & click) {
    QWidget * content=ide.InspectorDock.Content;
    auto * title=content->findChild<QLabel *>("assetNameLabel");
    auto * apply=content->findChild<QPushButton *>("applyButton");
    const auto select=[&](const QString & name) { click(name);pump([&]{return title->text()==name;}); };
    const auto find=[&](const std::string & path) -> IAssetMeta * {
        for (IAssetMeta * meta:manager->GetAssetMetas()) if (meta->GetPath()==path) return meta;
        return nullptr;
    };
    const auto settings=[&](IAssetMeta * meta) {
        std::string json;Json::Value value;
        Check(meta&&meta->SerializeMeta(json)&&AssetArtifactIO::Parse(json,value),"Cannot read panel settings");
        return value["ImportSettings"];
    };
    const auto artifact=[&](IAssetMeta * meta,eAssetType type,QByteArray * payload=nullptr) {
        AssetImportManifest manifest;
        Check(library->GetManifest(meta->_Guid,manifest),"Panel import manifest missing");
        for (const auto & asset:manifest.Assets) if (asset.Type==type)
            return ReadArtifact(library->QueryAsset(meta->_Guid,asset.LocalID),payload);
        throw std::runtime_error("Panel import resource missing");
    };
    const auto persistMeta=[&](TestMeta & meta) {
        std::string json,error;Check(meta.SerializeMeta(json),"Cannot serialize panel fixture Meta");
        Check(AssetArtifactIO::Write(root.filePath("Assets/"+QString::fromStdString(meta.Path)+".meta"),QByteArray::fromStdString(json),error),error);
    };
    const auto meshExtent=[&](IAssetMeta * meta) {
        QByteArray payload;const auto mesh=artifact(meta,eAssetType::Mesh,&payload);
        double extent=0;
        for (Json::UInt i=0;i<mesh["VertexCount"].asUInt();++i) {
            QDataStream stream(payload.mid(qsizetype(i)*mesh["VertexStride"].asUInt(),12));
            stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
            float x,y,z;stream>>x>>y>>z;
            extent=std::max({extent,std::abs(double(x)),std::abs(double(y)),std::abs(double(z))});
        }
        return extent;
    };
    stage="model panel";select("Triangle.obj");
    auto * model=content->findChild<QWidget *>("ModelInspectorForm");
    Check(model&&model->isVisible()&&!inspector->HasPendingChanges(),"Model form starts dirty or hidden");
    auto * scale=model->findChild<QLineEdit *>("scaleFactorEdit");
    auto * uv=model->findChild<QCheckBox *>("generateLightmapUVsCheck");
    const double originalExtent=meshExtent(find("Triangle.obj"));
    Check(originalExtent>0,"Model fixture has no geometry");
    uv->setChecked(true);Check(!apply->isEnabled(),"Unsupported UV generation enabled Apply");
    inspector->RevertChanges();scale->setText("2");
    Check(inspector->HasPendingChanges()&&inspector->ApplyChanges(),"Model panel apply failed");pump(idle);
    Check(std::abs(meshExtent(find("Triangle.obj"))-originalExtent*2)<0.00001,"Model scale did not change mesh payload");
    Check(!inspector->HasPendingChanges(),"Model normalization left dirty settings");
    auto * normals=model->findChild<QComboBox *>("normalsModeCombo");normals->setCurrentIndex(normals->findData("Calculate"));
    auto * angle=model->findChild<QDoubleSpinBox *>("normalSmoothingAngleSpin");angle->setValue(30.123);
    Check(inspector->ApplyChanges(),"First floating point apply failed");
    angle->setValue(45.678);
    Check(inspector->ApplyChanges(),"Immediate second apply conflicted with normalized float settings");pump(idle);
    Check(!inspector->HasPendingChanges(),"Float normalization left a dirty model panel");
    Check(ide.InspectorDock.Host.grab().save("/tmp/gamesprout-inspector-model.png"),"Cannot render model panel");

    stage="text panel";
    std::string error;
    Check(AssetArtifactIO::Write(root.filePath("Assets/Notes.txt"),QByteArray("first\r\nsecond\rthird"),error),error);
    pump([&]{return find("Notes.txt")&&idle();});select("Notes.txt");
    auto * text=content->findChild<QWidget *>("TextInspectorForm");
    Check(text->isVisible()&&!inspector->HasPendingChanges(),"Text form starts dirty");
    text->findChild<QCheckBox *>("normalizeLineEndingsCheck")->setChecked(true);
    Check(inspector->ApplyChanges(),"Text panel apply failed");pump(idle);
    QByteArray normalized;artifact(find("Notes.txt"),eAssetType::Text,&normalized);
    Check(normalized=="first\nsecond\nthird","Text panel did not normalize payload");
    auto * encoding=text->findChild<QComboBox *>("encodingCombo");encoding->setCurrentIndex(encoding->findData("Binary"));
    Check(!text->findChild<QCheckBox *>("normalizeLineEndingsCheck")->isEnabled(),"Binary text options still enabled");
    inspector->RevertChanges();

    const QString ffmpeg=QStandardPaths::findExecutable("ffmpeg");
    if (!ffmpeg.isEmpty()&&!QStandardPaths::findExecutable("ffprobe").isEmpty()) {
        stage="audio panel";
        TestMeta audio(eAssetImporterType::Audio,Guid(31,32,33,34),"Tone.wav");audio.Settings["CompressionFormat"]="PCM";persistMeta(audio);
        QByteArray wav;QDataStream output(&wav,QIODevice::WriteOnly);output.setByteOrder(QDataStream::LittleEndian);
        const quint32 samples=4410;
        output.writeRawData("RIFF",4);output<<quint32(36+samples*2);output.writeRawData("WAVEfmt ",8);
        output<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(44100)<<quint32(88200)<<quint16(2)<<quint16(16);
        output.writeRawData("data",4);output<<quint32(samples*2);
        for (quint32 i=0;i<samples;++i) output<<qint16(std::sin(i*440.0*6.283185307179586/44100.0)*10000);
        Check(AssetArtifactIO::Write(root.filePath("Assets/Tone.wav"),wav,error),error);
        pump([&]{return find("Tone.wav")&&idle();});select("Tone.wav");
        auto * form=content->findChild<QWidget *>("AudioInspectorForm");
        Check(form->isVisible()&&!inspector->HasPendingChanges(),"Audio form starts dirty");
        auto * mono=form->findChild<QCheckBox *>("forceToMonoCheck");
        auto * ambisonic=form->findChild<QCheckBox *>("ambisonicCheck");
        mono->setChecked(true);ambisonic->setChecked(true);
        Check(!apply->isEnabled(),"Invalid Ambisonic/mono combination accepted");inspector->RevertChanges();
        auto * rate=form->findChild<QComboBox *>("sampleRateModeCombo");rate->setCurrentIndex(rate->findData("Override"));
        form->findChild<QLineEdit *>("sampleRateOverrideEdit")->setText("22050");
        Check(inspector->ApplyChanges(),"Audio apply failed");pump(idle);
        const auto audioData=artifact(find("Tone.wav"),eAssetType::Audio);
        Check(audioData["EncodedInfo"]["streams"][0]["sample_rate"]=="22050","Audio sample rate did not change");
        Check(!inspector->HasPendingChanges(),"Audio form remained dirty");
        Check(ide.InspectorDock.Host.grab().save("/tmp/gamesprout-inspector-audio.png"),"Cannot render audio panel");

        stage="video panel";
        TestMeta video(eAssetImporterType::Video,Guid(35,36,37,38),"Clip.mp4");video.Settings["Transcode"]=false;persistMeta(video);
        Check(QProcess::execute(ffmpeg,{"-nostdin","-v","error","-y","-f","lavfi","-i","color=c=red:s=16x16:d=0.2","-c:v","libx264","-pix_fmt","yuv420p",root.filePath("Assets/Clip.mp4")})==0,"Cannot create video fixture");
        pump([&]{return find("Clip.mp4")&&idle();});select("Clip.mp4");
        auto * formVideo=content->findChild<QWidget *>("VideoInspectorForm");
        Check(formVideo->isVisible()&&!inspector->HasPendingChanges(),"Video form starts dirty");
        formVideo->findChild<QCheckBox *>("flipHorizontalCheck")->setChecked(true);
        Check(!apply->isEnabled(),"Video flip without transcode accepted");inspector->RevertChanges();
        formVideo->findChild<QCheckBox *>("transcodeCheck")->setChecked(true);
        formVideo->findChild<QLineEdit *>("maxWidthEdit")->setText("8");
        formVideo->findChild<QLineEdit *>("maxHeightEdit")->setText("8");
        Check(inspector->ApplyChanges(),"Video apply failed");pump(idle);
        const auto videoData=artifact(find("Clip.mp4"),eAssetType::Video);
        Check(videoData["EncodedInfo"]["streams"][0]["width"]==8,"Video panel did not resize output");
    } else std::cout<<"SKIP audio/video inspector payload checks: ffmpeg/ffprobe missing\n";

    QString fontPath;
    for (const QString & candidate:QStringList{"/System/Library/Fonts/Supplemental/Arial.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","C:/Windows/Fonts/arial.ttf"})
        if (QFileInfo::exists(candidate)) {fontPath=candidate;break;}
    if (!fontPath.isEmpty()) {
        stage="font panel";
        TestMeta font(eAssetImporterType::Font,Guid(39,40,41,42),"Font.ttf");font.Settings["CustomCharacters"]="AB";font.Settings["FontSize"]=24;persistMeta(font);
        Check(QFile::copy(fontPath,root.filePath("Assets/Font.ttf")),"Cannot copy font");
        pump([&]{return find("Font.ttf")&&idle();});select("Font.ttf");
        auto * form=content->findChild<QWidget *>("FontInspectorForm");
        Check(form->isVisible()&&!inspector->HasPendingChanges(),"Font form starts dirty");
        auto * size=form->findChild<QLineEdit *>("fontSizeEdit");size->setText("513");
        Check(!apply->isEnabled(),"Oversized font accepted");inspector->RevertChanges();
        size->setText("32");form->findChild<QPlainTextEdit *>("customCharactersEdit")->setPlainText("ABCD");
        Check(inspector->ApplyChanges(),"Font apply failed");pump(idle);
        const auto fontData=artifact(find("Font.ttf"),eAssetType::Font);
        Check(fontData["PixelSize"]==32&&fontData["Glyphs"].size()==4,"Font panel did not regenerate glyph data");
    } else std::cout<<"SKIP font inspector payload checks: font fixture missing\n";

    stage="shader panel";
    TestMeta shader(eAssetImporterType::Shader,Guid(43,44,45,46),"Test.shader");
    shader.Settings["SourceLanguage"]="HLSL";shader.Settings["Stage"]="Vertex";shader.Settings["EntryPoint"]="vertex_main";persistMeta(shader);
    Check(AssetArtifactIO::Write(root.filePath("Assets/Test.shader"),"float4 vertex_main(uint id : SV_VertexID) : SV_Position {return float4(0,0,0,1);}\n",error),error);
    pump([&]{return find("Test.shader")&&idle();});select("Test.shader");
    auto * shaderForm=content->findChild<QWidget *>("ShaderInspectorForm");
    Check(shaderForm->isVisible()&&!inspector->HasPendingChanges(),"Shader form starts dirty");
    shaderForm->findChild<QLineEdit *>("entryPointEdit")->setText("bad entry");Check(!apply->isEnabled(),"Invalid shader entry accepted");
    inspector->RevertChanges();
    shaderForm->findChild<QPlainTextEdit *>("definesEdit")->setPlainText("USE_FOG=1\nQUALITY=2");
    Check(inspector->ApplyChanges(),"Shader settings apply failed");pump(idle);
    Check(settings(find("Test.shader"))["Defines"].size()==2,"Shader definitions not saved");

    stage="compute panel";
    TestMeta compute(eAssetImporterType::Shader,Guid(47,48,49,50),"Test.compute.shader");
    compute.Settings["Stage"]="Compute";compute.Settings["SourceLanguage"]="HLSL";compute.Settings["EntryPoint"]="kernel_main";persistMeta(compute);
    Check(AssetArtifactIO::Write(root.filePath("Assets/Test.compute.shader"),"[numthreads(8,1,1)] void kernel_main(uint3 id : SV_DispatchThreadID) {}\n",error),error);
    pump([&]{return find("Test.compute.shader")&&idle();});select("Test.compute.shader");
    auto * computeForm=content->findChild<QWidget *>("ShaderInspectorForm");
    Check(computeForm->isVisible()&&!inspector->HasPendingChanges(),"Compute form starts dirty");
    Check(computeForm->findChild<QComboBox *>("stageCombo")->currentData()=="Compute","Compute stage was not restored");
    std::cout<<"PASS model/audio/text/font/video/shader/compute settings panels and real importer outputs\n";
}
