#include "AssetTestSupport.h"

int main(int argc,char **argv) {
    QGuiApplication application(argc,argv);
    Foundation::FoundationApi * real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);
    try {
        Check(real!=nullptr,"Foundation cannot load");
        Check(real->Initialize(argc,argv)&&real->Launch(),"Foundation startup failed");
        QTemporaryDir temporary;Check(temporary.isValid(),"Temp project failed");
        QDir root(temporary.path());Check(root.mkpath("Assets"),"Assets directory failed");
        TestIDE ide;TestProject project;project.Root=temporary.path().toStdString();TestConsole console;
        TestEditor editor;editor.IDE=&ide;editor.Modules["Project"]=&project;editor.Modules["Console"]=&console;
        FoundationProxy foundation;foundation.Real=real;foundation.Tasks.Real=real->TaskApi();foundation.Tasks.IDE=&ide;
        GameSprout::GameSproutApi api(&foundation,&editor);
        std::vector<std::string> names{"LocalDatabase","LibraryManager","AssetsPipeline","AssetsManager","GuidCreator"};
        for (const std::string & name:names) editor.Modules[name]=LoadModule(name);
        for (const auto & entry:editor.Modules) GameSprout::GameSproutEditorFinal::Before(entry.second,&api);
        for (const auto & entry:editor.Modules) Check(GameSprout::GameSproutEditorFinal::Initialize(entry.second,&api),"Module Initialize: "+entry.first);
        for (const auto & entry:editor.Modules) if (entry.first!="Project" && entry.first!="AssetsManager") Check(GameSprout::GameSproutEditorFinal::Launch(entry.second,&api),"Module Launch: "+entry.first);
        Check(GameSprout::GameSproutEditorFinal::Launch(&project,&api),"Project launch");
        auto * library=static_cast<ILibraryManager *>(editor.Modules["LibraryManager"]);
        auto * pipeline=static_cast<IAssetsPipeline *>(editor.Modules["AssetsPipeline"]);
        auto * manager=static_cast<IAssetsManager *>(editor.Modules["AssetsManager"]);
        auto pump=[&](const std::function<bool()> & finished,int timeout=30000) {
            const auto start=std::chrono::steady_clock::now();
            do {real->Update();QCoreApplication::processEvents();if(finished())return;QThread::msleep(2);}
            while(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(timeout));
            for (IAssetMeta * meta : manager->GetAssetMetas()) {
                AssetImportManifest output;
                const bool exists = library->GetManifest(meta->_Guid, output);
                std::cerr << "TIMEOUT asset " << meta->_Guid.GetGuidString() << " " << meta->GetPath()
                    << " manifest=" << exists << " " << output.SourcePath << "\n";
            }
            std::cerr << "TIMEOUT Loading=" << ide.Count << "\n";
            throw std::runtime_error("Timed out waiting for task completion");
        };
        auto import=[&](TestMeta & meta, bool force=false) {
            bool done=false;AssetImportResult result;
            pipeline->ImportAsset(&meta,[&](const AssetImportResult & value){Check(std::this_thread::get_id()==ide.Main,"Completion off main thread");result=value;done=true;},force);
            pump([&]{return done;});Check(ide.Count==0 && ide.Loads==ide.Unloads,"Loading not balanced");
            if(result.Success)meta.Identities=result.Manifest.SubAssetsJson;
            return result;
        };
        QImage image(7,5,QImage::Format_RGBA8888);image.fill(QColor(30,100,200,128));
        const QString png=root.filePath("Assets/Test.png");Check(image.save(png),"PNG fixture failed");
        TestMeta texture(eAssetImporterType::Texture,Guid(1,2,3,4),"Test.png");texture.Settings["MaxSize"]=4;texture.Settings["GenerateMipmaps"]=true;
        auto first=import(texture);Check(first.Success&&!first.Cached,"Texture import failed: "+first.Error);
        Check(first.Manifest.Assets.size()==1,"Texture artifact count");
        Check(first.Manifest.Directory.empty() && first.Manifest.Assets[0].Path == texture._Guid.GetGuidString()+"_"+std::to_string(first.Manifest.Assets[0].LocalID)+".gsasset", "Artifacts are not flat GUID_LocalID files");
        IAsset * asset=library->QueryAsset(texture._Guid,first.Manifest.Assets[0].LocalID);Check(asset!=nullptr,"Library lookup failed");
        QByteArray pixels;auto info=ReadArtifact(asset->GetArtifactPath(),&pixels);
        Check(info["Width"]==4 && info["Height"]==3 && info["Mips"].size()==3 && pixels.size()==60,"Texture resize/mipmap payload incorrect");
        auto cached=import(texture);Check(cached.Success&&cached.Cached,"Cache not reused");
        auto forced=import(texture,true);Check(forced.Success&&!forced.Cached&&forced.Manifest.Generation!=first.Manifest.Generation,"Force reimport did not regenerate artifacts");
        Check(forced.Manifest.Assets[0].LocalID==first.Manifest.Assets[0].LocalID,"Reimport changed LocalID");
        first=forced;
        std::string error;
        AssetImportManifest legacy=first.Manifest;
        legacy.Directory="Artifacts/"+texture._Guid.GetGuidString()+"/legacy-version";
        legacy.Generation.clear();
        const std::string flatName=legacy.Assets[0].Path;
        const QString oldFile=QString::fromStdString(library->GetLibraryRoot()+"/"+flatName);
        Check(QDir().mkpath(QString::fromStdString(library->GetLibraryRoot()+"/"+legacy.Directory)),"Cannot create legacy fixture");
        legacy.Assets[0].Path=std::to_string(legacy.Assets[0].LocalID)+".gsasset";
        Check(QFile::rename(oldFile,QString::fromStdString(library->GetLibraryRoot()+"/"+legacy.Directory+"/"+legacy.Assets[0].Path)),"Cannot move legacy fixture");
        const std::string legacyJson=AssetArtifactIO::JsonText(AssetArtifactIO::ToJson(legacy));
        Check(library->GetLDBSession()->WriteBatch({{"Manifests",texture._Guid.GetGuidString(),std::vector<uint8_t>(legacyJson.begin(),legacyJson.end()),false}}),"Cannot record legacy fixture");
        const auto migrated=import(texture);
        Check(migrated.Success&&!migrated.Cached&&migrated.Manifest.Directory.empty()&&migrated.Manifest.Assets[0].Path==flatName,"Legacy directory artifact did not migrate to flat storage");
        first=migrated;
        Check(AssetArtifactIO::Write(png,"invalid png",error),"Cannot corrupt fixture");
        auto failed=import(texture);Check(!failed.Success&&!failed.Error.empty(),"Corrupt image succeeded");
        AssetImportManifest current;Check(library->GetManifest(texture._Guid,current)&&current.Generation==first.Manifest.Generation,"Failure replaced last good output");
        Check(image.save(png),"Restore PNG failed");
        foundation.Tasks.Reject=true;auto rejected=import(texture);Check(!rejected.Success,"Rejected task succeeded");foundation.Tasks.Reject=false;
        bool cancelled=false;
        pipeline->ImportAsset(&texture,[&](const AssetImportResult & result){Check(!result.Success&&result.Superseded,"Cancelled task committed");cancelled=true;});
        pipeline->Invalidate(texture._Guid);pump([&]{return cancelled;});Check(ide.Count==0,"Cancelled task left Loading");
        ILDBSession * database=library->GetLDBSession();Check(database->CreateTable("ChecksA")&&database->CreateTable("ChecksB"),"DB tables failed");
        Check(database->WriteBatch({{"ChecksA","key",{1,2},false},{"ChecksB","key",{3},false}}),"DB batch failed");
        Check(!database->WriteBatch({{"ChecksA","key",{9},false},{"MissingTable","key",{},true}}),"Invalid DB batch succeeded");
        Check(!library->RemoveAsset(texture._Guid,error,{{"ChecksA","backup-proof",{9},false},{"MissingTable","key",{},true}}),"Invalid backup/removal transaction succeeded");
        Check(library->GetManifest(texture._Guid,current) && !database->GetTable("ChecksA")->IsExists("backup-proof"),"Failed backup/removal transaction changed active data");
        std::vector<uint8_t> bytes;Check(database->GetTable("ChecksA")->Get("key",bytes)&&bytes==std::vector<uint8_t>({1,2}),"DB batch was not atomic");
        Check(database->GetTable("ChecksA")->Delete("key")&&database->GetTable("ChecksA")->Delete("key"),"DB Delete is not idempotent");
        std::cout<<"PASS texture decode, mips, cache, failure preservation, task rejection, cancellation, UI thread/pairing and atomic database writes\n";
        // Real decoder/encoder coverage in temporary fixtures, no project files touched.
        QByteArray wav;
        QDataStream audioData(&wav,QIODevice::WriteOnly);audioData.setByteOrder(QDataStream::LittleEndian);
        const quint32 sampleCount=4410;
        audioData.writeRawData("RIFF",4);audioData<<quint32(36+sampleCount*2);audioData.writeRawData("WAVEfmt ",8);
        audioData<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(44100)<<quint32(88200)<<quint16(2)<<quint16(16);
        audioData.writeRawData("data",4);audioData<<quint32(sampleCount*2);
        for(quint32 i=0;i<sampleCount;++i) audioData<<qint16(std::sin(i*440.0*6.283185307179586/44100.0)*10000);
        Check(AssetArtifactIO::Write(root.filePath("Assets/Tone.wav"),wav,error),"Cannot write WAV fixture");
        if(!QStandardPaths::findExecutable("ffmpeg").isEmpty()) {
            TestMeta audio(eAssetImporterType::Audio,Guid(10,20,30,40),"Tone.wav");audio.Settings["CompressionFormat"]="PCM";audio.Settings["ForceToMono"]=true;
            const auto result=import(audio);Check(result.Success,"Audio preprocessing failed: "+result.Error);
            const auto value=ReadArtifact(library->QueryAsset(audio._Guid,result.Manifest.Assets[0].LocalID)->GetArtifactPath());
            Check(value["Container"]=="wav"&&value["EncodedInfo"]["streams"][0]["sample_rate"]=="44100","Encoded audio invalid");
            const QString videoFile=root.filePath("Assets/Video.mp4");
            Check(QProcess::execute(QStandardPaths::findExecutable("ffmpeg"),{"-nostdin","-v","error","-y","-f","lavfi","-i","color=c=red:s=16x16:d=0.2","-c:v","libx264","-pix_fmt","yuv420p",videoFile})==0,"Video fixture failed");
            TestMeta video(eAssetImporterType::Video,Guid(11,21,31,41),"Video.mp4");video.Settings["MaxWidth"]=8;video.Settings["MaxHeight"]=8;video.Settings["Codec"]="H264";
            const auto encoded=import(video);Check(encoded.Success,"Video preprocessing failed: "+encoded.Error);
            const auto data=ReadArtifact(library->QueryAsset(video._Guid,encoded.Manifest.Assets[0].LocalID)->GetArtifactPath());
            Check(data["EncodedInfo"]["streams"][0]["width"]==8&&data["EncodedInfo"]["streams"][0]["height"]==8,"Video size processing failed");
            std::string audioMeta;audio.SerializeMeta(audioMeta);
            Check(AssetArtifactIO::Write(root.filePath("Assets/Tone.wav.meta"),QByteArray::fromStdString(audioMeta),error),"Cannot persist audio fixture settings");
            std::cout<<"PASS WAV decode/encode and H264 video resizing\n";
        }
        QString fontPath;
        for(const QString & candidate:QStringList{"/System/Library/Fonts/Supplemental/Arial.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","C:/Windows/Fonts/arial.ttf"}) if(QFileInfo::exists(candidate)){fontPath=candidate;break;}
        if(!fontPath.isEmpty()) {
            Check(QFile::copy(fontPath,root.filePath("Assets/Font.ttf")),"Cannot copy font fixture");
            TestMeta font(eAssetImporterType::Font,Guid(12,22,32,42),"Font.ttf");font.Settings["CustomCharacters"]="GameSprout";font.Settings["FontSize"]=24;
            const auto result=import(font);Check(result.Success&&result.Manifest.Assets.size()==2,"Font atlas preprocessing failed: "+result.Error);
            for(const auto & artifact:result.Manifest.Assets) if(artifact.Type==eAssetType::Font) {
                const auto value=ReadArtifact(library->QueryAsset(font._Guid,artifact.LocalID)->GetArtifactPath());Check(value["Glyphs"].size()==10,"Font character de-duplication failed");
            }
            std::cout<<"PASS font decoding, glyph metadata and atlas generation\n";
        }
#if defined(__APPLE__)
        const QByteArray metal="#include <metal_stdlib>\nusing namespace metal;\nvertex float4 vertex_main(uint id [[vertex_id]]) { return float4(float(id),0,0,1); }\n";
        Check(AssetArtifactIO::Write(root.filePath("Assets/Test.metal"),metal,error),"Cannot write Metal fixture");
        TestMeta shader(eAssetImporterType::Shader,Guid(13,23,33,43),"Test.metal");shader.Settings["SourceLanguage"]="MSL";shader.Settings["EntryPoint"]="vertex_main";shader.Settings["Stage"]="Vertex";
        const auto compiled=import(shader);
        if (compiled.Success) {
            const auto metalInfo=ReadArtifact(library->QueryAsset(shader._Guid,compiled.Manifest.Assets[0].LocalID)->GetArtifactPath());
            Check(metalInfo["Variants"][0]["Format"]=="MetalLib","Metal artifact format failed");
            std::string shaderMeta;shader.SerializeMeta(shaderMeta);
            Check(AssetArtifactIO::Write(root.filePath("Assets/Test.metal.meta"),QByteArray::fromStdString(shaderMeta),error),"Cannot persist shader fixture settings");
            std::cout<<"PASS native Metal source compilation to metallib\n";
        } else {
            Check(compiled.Error.find("missing Metal Toolchain")!=std::string::npos,"Metal compilation failed unexpectedly: "+compiled.Error);
            Check(!library->GetManifest(shader._Guid,current),"Missing compiler published an artifact");
            Check(QFile::remove(root.filePath("Assets/Test.metal")),"Cannot remove unsupported shader fixture");
            std::cout<<"SKIP Metal success case: Xcode Metal Toolchain is missing; PASS explicit failure and no commit\n";
        }
#endif
        Check(root.mkpath("Assets/OBJ/Materials"),"Cannot create OBJ fixture directory");
        Check(image.save(root.filePath("Assets/OBJ/Color.png")),"Cannot write OBJ texture");
        TestMeta objTexture(eAssetImporterType::Texture,Guid(15,25,35,45),"OBJ/Color.png");
        const auto importedTexture=import(objTexture);Check(importedTexture.Success,"OBJ texture import failed");
        std::string objTextureMeta;objTexture.SerializeMeta(objTextureMeta);
        Check(AssetArtifactIO::Write(root.filePath("Assets/OBJ/Color.png.meta"),QByteArray::fromStdString(objTextureMeta),error),"Cannot write OBJ texture Meta");
        const QByteArray obj="mtllib Materials/Triangle.mtl\no Triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nusemtl Surface\nf 1/1 2/2 3/3\n";
        Check(AssetArtifactIO::Write(root.filePath("Assets/OBJ/Triangle.obj"),obj,error),"Cannot write OBJ fixture");
        Check(AssetArtifactIO::Write(root.filePath("Assets/OBJ/Materials/Triangle.mtl"),"newmtl Surface\nKd 0.2 0.4 0.6\nmap_Kd ../Color.png\n",error),"Cannot write MTL fixture");
        TestMeta model(eAssetImporterType::Model,Guid(14,24,34,44),"OBJ/Triangle.obj");
        const auto importedModel=import(model);Check(importedModel.Success,"OBJ/MTL preprocessing failed: "+importedModel.Error);
        bool hasMaterial=false,hasMesh=false,hasMTL=false,hasTexture=false;
        for(const auto & asset:importedModel.Manifest.Assets) {
            if(asset.Type==eAssetType::Mesh) hasMesh=true;
            if(asset.Type==eAssetType::Material) {
                const Json::Value value=ReadArtifact(library->QueryAsset(model._Guid,asset.LocalID)->GetArtifactPath());
                for(const auto & texture:value["Textures"]) if(texture["Asset"]["Guid"]==objTexture._Guid.GetGuidString()) hasMaterial=true;
            }
        }
        for(const auto & dependency:importedModel.Manifest.Dependencies) {
            hasMTL|=dependency.Path=="OBJ/Materials/Triangle.mtl";
            hasTexture|=dependency.Path=="OBJ/Color.png";
        }
        Check(hasMaterial&&hasMesh&&hasMTL&&hasTexture,"OBJ lost MTL/texture dependencies");
        std::cout<<"PASS OBJ triangulation, external MTL and texture reference/dependency tracking\n";
        // Scan through the real AssetsManager and watcher layer.
        Check(GameSprout::GameSproutEditorFinal::Launch(manager,&api),"AssetsManager launch failed");
        Check(ide.Count > 0,"AssetsManager Launch did not block the editor");
        GameSprout::GameSproutEditorFinal::Finished(manager,&api);
        pump([&]{
            GameSprout::GameSproutEditorFinal::Update(manager,&api);
            if (manager->GetAssetMetas().empty() || ide.Count != 0) return false;
            ILDBTable * table = library->GetLDBSession()->GetTable("AssetSources");
            if (!table) return false;
            for (IAssetMeta * meta : manager->GetAssetMetas()) {
                std::vector<uint8_t> data; Json::Value value;
                if (!table->Get(meta->_Guid.GetGuidString(),data) || !AssetArtifactIO::Parse(std::string(data.begin(),data.end()),value)) return false;
                if (value["Status"] != "Ready" && value["Status"] != "NotRequired") return false;
            }
            return true;
        });
        Check(console.Errors.empty(),"Unexpected AssetsManager import errors");
        std::string liveGuidString;
        for (IAssetMeta * meta:manager->GetAssetMetas()) if(meta->GetPath()=="Test.png") liveGuidString=meta->_Guid.GetGuidString();
        Check(!liveGuidString.empty(),"Missing scanned texture");
        AssetImportManifest textureOutput;
        Check(library->GetManifest(Guid(liveGuidString),textureOutput),"Scanned texture has no manifest");
        Json::Value material(Json::objectValue);
        material["Version"]=1;
        material["Texture"]["Guid"]=liveGuidString;
        material["Texture"]["LocalID"]=Json::UInt64(textureOutput.Assets[0].LocalID);
        Check(AssetArtifactIO::Write(root.filePath("Assets/Linked.mat"),QByteArray::fromStdString(AssetArtifactIO::JsonText(material)),error),"Cannot create material fixture");
        std::string materialGuid,materialDirectory;
        pump([&]{GameSprout::GameSproutEditorFinal::Update(manager,&api);for(IAssetMeta * meta:manager->GetAssetMetas()) if(meta->GetPath()=="Linked.mat") {
            AssetImportManifest output;if(library->GetManifest(meta->_Guid,output)&&ide.Count==0) {materialGuid=meta->_Guid.GetGuidString();materialDirectory=output.Generation;return true;}
        }return false;});
        Check(library->FindDependents("Test.png")==std::vector<std::string>{materialGuid},"Reverse dependency index missing");
        auto * settings=static_cast<ITextureImportSettings *>(manager->QueryAsset(Guid(liveGuidString))->GetImportSettings());
        Check(settings->SetMaxSize(2),"Cannot change texture settings");
        Check(manager->ReimportAsset(Guid(liveGuidString)),"Cannot request setting reimport");
        pump([&]{GameSprout::GameSproutEditorFinal::Update(manager,&api);AssetImportManifest output;return library->GetManifest(Guid(materialGuid),output)&&output.Generation!=materialDirectory&&ide.Count==0;});
        Check(library->GetManifest(Guid(materialGuid),current),"Material disappeared");materialDirectory=current.Generation;
        image.fill(QColor(120,10,50,255));Check(image.save(png),"Cannot modify source texture");
        pump([&]{GameSprout::GameSproutEditorFinal::Update(manager,&api);AssetImportManifest output;return library->GetManifest(Guid(materialGuid),output)&&output.Generation!=materialDirectory&&ide.Count==0;});
        Check(library->GetManifest(Guid(materialGuid),current),"Material disappeared");materialDirectory=current.Generation;
        std::cout<<"PASS settings/source changes invalidate dependents; force rebuild retains LocalID\n";
        std::cerr<<"CHECK file rename\n";
        Check(QFile::rename(png,root.filePath("Assets/Moved.png")),"Cannot rename source");
        pump([&]{GameSprout::GameSproutEditorFinal::Update(manager,&api);IAssetMeta * meta=manager->QueryAsset(Guid(liveGuidString));AssetImportManifest output;return meta&&meta->GetPath()=="Moved.png"&&library->GetManifest(meta->_Guid,output)&&output.SourcePath=="Moved.png"&&ide.Count==0;});
        pump([&]{GameSprout::GameSproutEditorFinal::Update(manager,&api);AssetImportManifest output;return library->GetManifest(Guid(materialGuid),output)&&output.Generation!=materialDirectory&&ide.Count==0;});
        Check(library->FindDependents("Moved.png")==std::vector<std::string>{materialGuid}&&library->FindDependents("Test.png").empty(),"Rename did not migrate dependency index");
        Check(QFileInfo::exists(root.filePath("Assets/Moved.png.meta"))&&!QFileInfo::exists(root.filePath("Assets/Test.png.meta")),"Rename lost Meta migration");
        std::cerr<<"CHECK nested create\n";
        Check(root.mkpath("Assets/FolderA"),"Cannot create nested folder");
        Check(image.save(root.filePath("Assets/FolderA/Nested.png")),"Nested PNG fixture");
        std::string nestedGuid;
        pump([&]{GameSprout::GameSproutEditorFinal::Update(manager,&api);for(IAssetMeta * meta:manager->GetAssetMetas())if(meta->GetPath()=="FolderA/Nested.png"){AssetImportManifest output;if(library->GetManifest(meta->_Guid,output)){nestedGuid=meta->_Guid.GetGuidString();return ide.Count==0;}}return false;});
        std::cerr<<"CHECK folder rename\n";
        Check(root.rename("Assets/FolderA","Assets/FolderB"),"Cannot rename folder");
        pump([&]{GameSprout::GameSproutEditorFinal::Update(manager,&api);IAssetMeta * meta=manager->QueryAsset(Guid(nestedGuid));AssetImportManifest output;return meta&&meta->GetPath()=="FolderB/Nested.png"&&library->GetManifest(meta->_Guid,output)&&output.SourcePath=="FolderB/Nested.png"&&ide.Count==0;});
        std::cerr<<"CHECK folder delete\n";
        Check(QDir(root.filePath("Assets/FolderB")).removeRecursively(),"Cannot remove folder");
        pump([&]{GameSprout::GameSproutEditorFinal::Update(manager,&api);return manager->QueryAsset(Guid(nestedGuid))==nullptr&&ide.Count==0;});
        Check(!library->GetManifest(Guid(nestedGuid),current),"Deleted nested asset retained active library index");
        std::cout<<"PASS file/folder rename preserves GUID/Meta; recursive removal clears library index\n";
        // Optional real animated FBX supplied on the command line.
        if(argc>1) {
            AssetImportManifest bearManifest;
            const QDir modelDirectory(QString::fromUtf8(argv[1]));
            Check(QFile::copy(modelDirectory.filePath("Bear_Animated.fbx"),root.filePath("Assets/Bear.fbx")),"Cannot copy real FBX");
            Check(QFile::copy(modelDirectory.filePath("T_Bear.png"),root.filePath("Assets/T_Bear.png")),"Cannot copy model texture");
            pump([&]{GameSprout::GameSproutEditorFinal::Update(manager,&api);for(IAssetMeta * meta:manager->GetAssetMetas())if(meta->GetPath()=="Bear.fbx") {AssetImportManifest output;if(library->GetManifest(meta->_Guid,output)) {int meshes=0,clips=0,skeletons=0;for(const auto & artifact:output.Assets){meshes+=artifact.Type==eAssetType::Mesh;clips+=artifact.Type==eAssetType::AnimationClip;skeletons+=artifact.Type==eAssetType::Skeleton;}Check(meshes==1&&clips==13&&skeletons==1,"FBX lost mesh/skeleton/animations");bearManifest=output;return ide.Count==0;}}return false;},60000);
            bool animationMoves=false;
            for(const AssetArtifact & artifact:bearManifest.Assets) {
                IAsset * imported=library->QueryAsset(Guid(bearManifest.GuidString),artifact.LocalID);
                QByteArray payload;const Json::Value value=ReadArtifact(imported->GetArtifactPath(),&payload);
                if(artifact.Type==eAssetType::Mesh) {
                    Check(value["Bones"].size()==37&&value["UVChannels"].asUInt64()>0,"Mesh lost bones/UVs");
                    const quint64 vertexCount=value["VertexCount"].asUInt64();
                    const quint64 indexOffset=value["IndexOffset"].asUInt64(),indexCount=value["IndexCount"].asUInt64();
                    Check(indexOffset==vertexCount*value["VertexStride"].asUInt64()&&quint64(payload.size())==indexOffset+indexCount*4,"Mesh layout invalid");
                    QDataStream indices(payload.mid(qsizetype(indexOffset)));indices.setByteOrder(QDataStream::LittleEndian);
                    for(quint64 i=0;i<indexCount;++i){quint32 index;indices>>index;Check(index<vertexCount,"Mesh index out of range");}
                }
                if(artifact.Type==eAssetType::AnimationClip) {
                    const quint64 stride=4+value["Tracks"].size()*40+value["BlendShapeTracks"].size()*4;
                    Check(quint64(payload.size())==stride*value["FrameCount"].asUInt64(),"Animation frame layout invalid");
                    if(value["FrameCount"].asUInt()>1&&payload.mid(4,qsizetype(stride-4))!=payload.mid(qsizetype(stride+4),qsizetype(stride-4))) animationMoves=true;
                }
            }
            Check(animationMoves,"Animation payload contains no movement");
            std::cout<<"PASS real Bear FBX: "<<bearManifest.Assets.size()<<" resources, 13 clips, 37 skin bones, indexed mesh and animated tracks\n";

        }
        Check(console.Errors.empty(),"Unexpected lifecycle import errors");
        Check(library->GetManifest(Guid(liveGuidString),current),"Missing current texture before GC");
        const QString activeGeneration=QString::fromStdString(library->GetLibraryRoot()+"/"+current.Assets[0].Path);
        const QString orphan=root.filePath("Library/"+QString::fromStdString(Guid(99,98,97,96).GetGuidString())+"_999.gsasset");
        Check(AssetArtifactIO::Write(orphan,"orphan",error),"Cannot create orphan cache fixture");
        GameSprout::GameSproutEditorFinal::Finished(library,&api);
        pump([&]{return ide.Count==0&&!QFileInfo::exists(orphan);});
        Check(QFileInfo::exists(activeGeneration),"GC removed a referenced artifact");
        std::cout<<"PASS orphan cache cleanup preserves active generation\n";
        GameSprout::GameSproutEditorFinal::Release(manager,&api);
        bool callbackAfterRelease=false;
        pipeline->ImportAsset(&texture,[&](const AssetImportResult &){callbackAfterRelease=true;},true);
        Check(ide.Count==1,"Pending task has no Loading");
        GameSprout::GameSproutEditorFinal::Release(pipeline,&api);
        Check(ide.Count==0&&ide.Loads==ide.Unloads,"Shutdown Loading imbalance");
        for(int i=0;i<100;++i){real->Update();QThread::msleep(2);}
        Check(!callbackAfterRelease,"Released task owner received a callback");
        GameSprout::GameSproutEditorFinal::Release(library,&api);
        std::cout<<"PASS release during outstanding import suppresses callback and balances Loading\n";
        real->Release();
        std::cout<<"All asset lifecycle checks passed.\n";
        return 0;
    } catch(const std::exception & error) {
        std::cerr<<"FAIL "<<error.what()<<"\n";
        if(real)real->Release();
        return 1;
    }
}
