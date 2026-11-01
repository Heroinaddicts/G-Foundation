#include "ProjectViewTestSupport.h"
#include "../../Module/Shared/MaterialDefinition.h"
#include "../../Module/RuntimeAssets/LibraryRuntimeResources.h"
#include "../../View/Shared/AssetDragPayload.h"
#include <QCheckBox>
#include <QAction>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QScrollArea>
#include <QScrollBar>

namespace {
QByteArray ReadFile(const QString& path) {
    QFile file(path);
    Check(file.open(QIODevice::ReadOnly),"Cannot read fixture: "+path.toStdString());
    return file.readAll();
}
bool DropResource(QWidget* field,const ProjectAssetSelection& value) {
    Check(field,"Missing resource drop target");
    QMimeData* mime=AssetDragPayload::Encode({value});Check(mime,"Cannot encode material resource drag");
    QDragEnterEvent enter(QPoint(5,5),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);
    QCoreApplication::sendEvent(field,&enter);
    bool accepted=false;
    if (enter.isAccepted()) {
        QDropEvent drop(QPointF(5,5),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(field,&drop);accepted=drop.isAccepted();
    }
    delete mime;return accepted;
}

// A self-contained ASCII FBX, including a Material child and one triangle.
const char* ModelSource = R"fbx(; FBX 7.4.0 project file
FBXHeaderExtension: {
    FBXHeaderVersion: 1003
    FBXVersion: 7400
}
Objects: {
    Geometry: 1000, "Geometry::Triangle", "Mesh" {
        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }
        PolygonVertexIndex: *3 { a: 0,1,-3 }
        LayerElementMaterial: 0 {
            Version: 101
            Name: ""
            MappingInformationType: "AllSame"
            ReferenceInformationType: "IndexToDirect"
            Materials: *1 { a: 0 }
        }
        Layer: 0 {
            Version: 100
            LayerElement: {
                Type: "LayerElementMaterial"
                TypedIndex: 0
            }
        }
    }
    Model: 2000, "Model::Triangle", "Mesh" {
        Version: 232
    }
    Material: 3000, "Material::Surface", "" {
        Version: 102
        ShadingModel: "phong"
        MultiLayer: 0
        Properties70: {
            P: "DiffuseColor", "Color", "", "A", 0.2,0.4,0.6
            P: "Opacity", "double", "Number", "", 1
        }
    }
}
Connections: {
    C: "OO", 1000, 2000
    C: "OO", 2000, 0
    C: "OO", 3000, 2000
}
)fbx";

const char* ShaderSource = R"hlsl(/* GameSproutMaterial {
  "Name":"Tests/Surface",
  "Properties":[
    {"Name":"Tint","DisplayName":"颜色","Type":"Color","Default":[0.3,0.4,0.5,1],"Group":"表面"},
    {"Name":"Strength","DisplayName":"强度","Type":"Range","Default":0.25,"Min":0,"Max":1},
    {"Name":"Speed","DisplayName":"速度","Type":"Float","Default":2},
    {"Name":"Count","DisplayName":"数量","Type":"Int","Default":3},
    {"Name":"Direction","DisplayName":"方向","Type":"Vector","Default":[1,0,0,0]},
    {"Name":"Enabled","DisplayName":"启用","Type":"Toggle","Default":true},
    {"Name":"Mode","DisplayName":"模式","Type":"Enum","Default":0,"Options":["普通","发光"]},
    {"Name":"SurfaceMap","DisplayName":"贴图","Type":"Texture","Default":null},
    {"Name":"Internal","DisplayName":"隐藏项","Type":"Float","Default":7,"Hidden":true}
  ]
} */
float4 main(uint index:SV_VertexID):SV_Position { return float4(index,0,0,1); }
)hlsl";
}

int main(int argc,char** argv) {
    QApplication application(argc,argv);
    Foundation::FoundationApi* real=nullptr;
    TestProject project;TestConsole console;PreviewIDE ide;PreviewEditor editor;FoundationProxy foundation;
    GameSproutEditor::GameSproutModuleApi* api=nullptr;
    IInspectorUI* inspector=nullptr;
    LibraryRuntimeResources* resources=nullptr;
    std::vector<GameSproutEditor::IGameSproutModule*> initialized;
    const auto cleanup=[&] {
        delete resources;resources=nullptr;
        if (inspector) { GameSproutEditor::GameSproutEditorFinal::Release(inspector,&editor);inspector=nullptr; }
        for (const std::string name:{"AssetsManager","AssetsPipeline","ShaderCompiler","SelectionManager","LibraryManager","LocalDatabase","GuidCreator"}) {
            const auto found=editor.Modules.find(name);
            if (found!=editor.Modules.end() && std::find(initialized.begin(),initialized.end(),found->second)!=initialized.end())
                GameSproutEditor::GameSproutEditorFinal::Release(found->second,api);
        }
        initialized.clear();delete api;api=nullptr;
        if (real) { real->Release();real=nullptr; }
    };
    try {
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create material fixture");
        QDir root(temporary.path());Check(root.mkpath("Assets"),"Cannot create Assets");
        std::string error;
        const auto write=[&](const QString& path,const QByteArray& bytes) {
            Check(AssetArtifactIO::Write(root.filePath("Assets/"+path),bytes,error),error);
        };
        write("Model.fbx",ModelSource);write("Surface.shader",ShaderSource);
        write("Editable.mat","{\"Version\":1,\"Name\":\"Editable\",\"BaseColor\":[1,0.2,0.1,1]}");
        QImage image(4,4,QImage::Format_RGBA8888);image.fill(QColor(32,200,70));
        Check(image.save(root.filePath("Assets/Color.png")),"Cannot create texture fixture");
        real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);
        Check(real && real->Initialize(argc,argv) && real->Launch(),"Foundation startup failed");
        project.Root=temporary.path().toStdString();editor.IDE=&ide;
        editor.Modules["Project"]=&project;editor.Modules["Console"]=&console;
        foundation.Real=real;foundation.Tasks.Real=real->TaskApi();foundation.Tasks.IDE=&ide.Counter;
        api=new GameSproutEditor::GameSproutModuleApi(&foundation,&editor);
        for (const std::string name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","AssetsManager","GuidCreator","SelectionManager"})
            editor.Modules[name]=LoadModule(name);
        for (const auto& item:editor.Modules) GameSproutEditor::GameSproutEditorFinal::Before(item.second,api);
        for (const auto& item:editor.Modules) {
            Check(GameSproutEditor::GameSproutEditorFinal::Initialize(item.second,api),"Initialize "+item.first);
            initialized.push_back(item.second);
        }
        inspector=static_cast<IInspectorUI*>(LoadView("InspectorUI"));
        GameSproutEditor::GameSproutEditorFinal::Before(inspector,&editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(inspector,&editor),"Inspector initialize failed");
        for (const auto& item:editor.Modules) Check(GameSproutEditor::GameSproutEditorFinal::Launch(item.second,api),"Launch "+item.first);
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(inspector,&editor),"Inspector launch failed");
        auto* assets=static_cast<IAssetsManager*>(editor.Modules["AssetsManager"]);
        auto* library=static_cast<ILibraryManager*>(editor.Modules["LibraryManager"]);
        auto* selection=static_cast<ISelectionManager*>(editor.Modules["SelectionManager"]);
        std::string stage="startup";
        QTimer::singleShot(85000,&application,[&] { std::cerr<<"Material test stalled: "<<stage<<std::endl;std::_Exit(2); });
        const auto pump=[&](const std::function<bool()>& done) {
            QElapsedTimer timer;timer.start();
            do {
                real->Update();QCoreApplication::processEvents();
                GameSproutEditor::GameSproutEditorFinal::Update(assets,api);
                GameSproutEditor::GameSproutEditorFinal::Update(selection,api);
                GameSproutEditor::GameSproutEditorFinal::Update(inspector,&editor);
                if (done()) return;
                QThread::msleep(2);
            } while (timer.elapsed()<30000);
            throw std::runtime_error("Material test timeout: "+stage);
        };
        const auto idle=[&] {
            if (ide.Counter.Count) return false;
            for (IAssetMeta* meta:assets->GetAssetMetas()) if (meta->GetImportState()==eAssetImportState::Pending) return false;
            return true;
        };
        GameSproutEditor::GameSproutEditorFinal::Finished(assets,api);
        pump([&]{ return assets->GetAssetMetas().size()==4 && idle(); });
        const auto resource=[&](const std::string& path,eAssetType type) {
            for (IAssetMeta* meta:assets->GetAssetMetas()) if (meta->GetPath()==path) {
                AssetImportManifest manifest;Check(library->GetManifest(meta->_Guid,manifest),"Manifest missing: "+path+": "+meta->GetImportError());
                for (const auto& asset:manifest.Assets) if (asset.Type==type)
                    return GameSproutRuntime::ResourceReference{meta->_Guid.GetGuidString(),asset.LocalID};
            }
            throw std::runtime_error("Resource missing: "+path);
        };
        const auto independent=resource("Editable.mat",eAssetType::Material);
        const auto embedded=resource("Model.fbx",eAssetType::Material);
        const auto shader=resource("Surface.shader",eAssetType::Shader);
        const auto texture=resource("Color.png",eAssetType::Texture);
        const auto mesh=resource("Model.fbx",eAssetType::Mesh);
        QWidget* content=ide.InspectorDock.Content;
        auto* title=content->findChild<QLabel*>("assetNameLabel");
        auto* apply=content->findChild<QPushButton*>("applyButton");
        const auto spin=[&](const char* name) {
            auto* control=content->findChild<QDoubleSpinBox*>(QString::fromUtf8(name));
            Check(control,"Material parameter control missing: "+std::string(name));return control;
        };
        const auto select=[&](const GameSproutRuntime::ResourceReference& ref,bool child) {
            selection->SetAssetSelection({{ref.Guid,child?ref.LocalID:0,child}});
            pump([&]{return title->text()==(ref.Guid==embedded.Guid?"Surface":"Editable.mat") ||
                (ref.Guid!=embedded.Guid && title->text()=="Editable");});
        };
        stage="built-in edit";select(independent,false);
        Check(spin("material.BaseColor.0")->isEnabled() && !inspector->HasPendingChanges(),"Material opened dirty or disabled");
        const QByteArray originalMeta=ReadFile(root.filePath("Assets/Editable.mat.meta"));
        spin("material.BaseColor.0")->setValue(0.75);
        spin("material.Metallic.number")->setValue(0.4);
        Check(inspector->HasPendingChanges() && apply->isEnabled() && apply->text()=="保存材质","Material draft not detected");
        Check(inspector->ApplyChanges(),"Material UI save failed");pump(idle);
        Json::Value document;std::string source;
        Check(assets->LoadMaterialSource(Guid(independent.Guid),source,error) && AssetArtifactIO::Parse(source,document),error);
        Check(document["BaseColor"][0].asDouble()==0.75 && document["Metallic"].asDouble()==0.4,"Saved values missing");
        Check(ReadFile(root.filePath("Assets/Editable.mat.meta"))==originalMeta,"Material edit wrote import settings instead of source");
        spin("material.Metallic.number")->setValue(0.8);inspector->RevertChanges();
        Check(!inspector->HasPendingChanges() && spin("material.Metallic.number")->value()==0.4,"Material revert lost saved value");

        stage="save validation";
        const std::string baseline=source;
        Json::Value invalid=document;invalid["Roughness"]=2;
        Check(!assets->SaveMaterialSource(Guid(independent.Guid),AssetArtifactIO::JsonText(invalid),baseline,error),"Invalid range saved");
        invalid=document;invalid["BaseColorTexture"]["Guid"]=mesh.Guid;invalid["BaseColorTexture"]["LocalID"]=Json::UInt64(mesh.LocalID);
        Check(!assets->SaveMaterialSource(Guid(independent.Guid),AssetArtifactIO::JsonText(invalid),baseline,error),"Mesh accepted as texture");
        Check(assets->LoadMaterialSource(Guid(independent.Guid),source,error) && source==baseline,"Rejected save changed source");
        document["BaseColorTexture"]["Guid"]=texture.Guid;document["BaseColorTexture"]["LocalID"]=Json::UInt64(texture.LocalID);
        document["BaseColorTextureST"]=MaterialDefinition::Vector(2,3,0.1,0.2);
        Check(assets->SaveMaterialSource(Guid(independent.Guid),AssetArtifactIO::JsonText(document),baseline,error),error);pump(idle);
        resources=new LibraryRuntimeResources(assets,library);GameSproutRuntime::MaterialRenderData render;
        Check(resources->LoadMaterial(independent,render,error) && render.BaseColorTexture==texture && render.BaseColorTextureST.X==2 && render.BaseColorTextureST.W>0.19f,error);
        auto* baseTextureField=content->findChild<QWidget*>("material.BaseColorTexture")->findChild<QWidget*>("textureResource");
        baseTextureField->findChild<QAction*>("clearResourceAction")->trigger();
        Check(inspector->ApplyChanges(),"Clearing material texture did not save");pump(idle);
        Check(resources->LoadMaterial(independent,render,error) && render.BaseColorTexture.Guid.empty(),"Cleared texture was rebound");
        baseTextureField=content->findChild<QWidget*>("material.BaseColorTexture")->findChild<QWidget*>("textureResource");
        Check(DropResource(baseTextureField,{texture.Guid,0,false}) && inspector->ApplyChanges(),"Reassigning material texture failed");pump(idle);

        stage="external conflict";
        spin("material.Metallic.number")->setValue(0.8);
        Check(assets->LoadMaterialSource(Guid(independent.Guid),source,error),error);
        Json::Value changed;Check(AssetArtifactIO::Parse(source,changed),"Invalid saved source");changed["Metallic"]=0.2;
        Check(assets->SaveMaterialSource(Guid(independent.Guid),AssetArtifactIO::JsonText(changed),source,error),error);pump(idle);
        Check(spin("material.Metallic.number")->value()==0.8 && !inspector->ApplyChanges(),"External edit lost draft or stale draft committed");
        inspector->RevertChanges();Check(spin("material.Metallic.number")->value()==0.2,"Revert failed to load external change");

        stage="shader-driven controls";
        content->findChild<QComboBox*>("shaderModeCombo")->setCurrentIndex(1);
        auto* shaderField=content->findChild<QWidget*>("shaderResource");
        Check(!DropResource(shaderField,{mesh.Guid,mesh.LocalID,true}),"Mesh accepted in Shader field");
        Check(DropResource(shaderField,{shader.Guid,0,false}),"Shader source drag rejected");
        Check(spin("material.Strength.number")->value()==0.25 && spin("material.Speed.number")->value()==2,"Shader defaults not shown");
        Check(!content->findChild<QWidget*>("material.Internal") && !content->findChild<QWidget*>("material.Metallic"),"Wrong Shader property list shown");
        auto* integer=content->findChild<QLineEdit*>("material.Count.integer");
        auto* mode=content->findChild<QWidget*>("material.Mode")->findChild<QComboBox*>("enumCombo");
        auto* enabled=content->findChild<QWidget*>("material.Enabled")->findChild<QCheckBox*>("toggleCheck");
        Check(integer && mode && enabled,"Typed material controls missing");
        integer->setText("12");mode->setCurrentIndex(1);enabled->setChecked(false);
        spin("material.Tint.1")->setValue(0.9);spin("material.Direction.2")->setValue(4);spin("material.Strength.number")->setValue(0.6);
        auto* textureField=content->findChild<QWidget*>("material.SurfaceMap")->findChild<QWidget*>("textureResource");
        Check(!DropResource(textureField,{mesh.Guid,mesh.LocalID,true}),"Mesh accepted in material texture field");
        Check(DropResource(textureField,{texture.Guid,0,false}),"Material texture drag rejected");
        Check(inspector->ApplyChanges(),"Custom Shader material save failed");pump(idle);
        Check(assets->LoadMaterialSource(Guid(independent.Guid),source,error) && AssetArtifactIO::Parse(source,document),error);
        Check(document["Count"].asInt()==12 && document["Mode"].asInt()==1 && !document["Enabled"].asBool()
            && document["Direction"][2].asDouble()==4 && document["Internal"].asDouble()==7,
            "Typed values did not round-trip: "+AssetArtifactIO::JsonText(document));
        QCoreApplication::processEvents();QCoreApplication::processEvents();
        for (auto* scroll:content->findChildren<QScrollArea*>()) if (scroll->isVisible())
            Check(scroll->horizontalScrollBar()->maximum()==0,"Material Inspector overflowed narrow dock horizontally");
        Check(content->grab().save("/tmp/gamesprout-material-inspector.png"),"Cannot capture material Inspector");

        stage="Shader source conflict";
        const QByteArray savedMaterial=ReadFile(root.filePath("Assets/Editable.mat"));
        QByteArray changedShader(ShaderSource);changedShader.replace("\"Max\":1","\"Max\":2");
        write("Surface.shader",changedShader);
        document["Count"]=13;
        Check(!assets->SaveMaterialSource(Guid(independent.Guid),AssetArtifactIO::JsonText(document),source,error)
            && ReadFile(root.filePath("Assets/Editable.mat"))==savedMaterial,"Unpublished Shader schema accepted a material save");
        pump([&] {
            if (!idle()) return false;
            Json::Value schema;
            return MaterialDefinition::ResolveSchema(library,document,schema,error)
                && schema["Properties"][1]["Max"].asDouble()==2;
        });

        stage="embedded extraction";select(embedded,true);
        Check(!spin("material.BaseColor.0")->isEnabled() && !apply->isVisible(),"Embedded material editable");
        const float oldRed=static_cast<float>(spin("material.BaseColor.0")->value());
        std::string dialogError;
        QTimer::singleShot(0,&application,[&] {
            QWidget* dialog=QApplication::activeModalWidget();
            if (!dialog) { dialogError="Extraction dialog did not open";return; }
            dialog->findChild<QLineEdit*>("pathEdit")->setText("Extracted.mat");
            dialog->findChild<QPushButton*>("extractButton")->click();
            if (dialog==QApplication::activeModalWidget()) {
                dialogError=dialog->findChild<QLabel*>("errorLabel")->text().toStdString();
                static_cast<QDialog*>(dialog)->reject();
            }
        });
        content->findChild<QPushButton*>("extractMaterialButton")->click();
        Check(dialogError.empty(),"Extraction failed: "+dialogError);pump(idle);
        const auto extracted=resource("Extracted.mat",eAssetType::Material);
        Check(extracted.Guid!=embedded.Guid && extracted.LocalID==embedded.LocalID,"Extraction lost stable source identity");
        Check(resources->LoadMaterial(embedded,render,error) && std::abs(render.BaseColor.R-oldRed)<0.001,error);
        Check(assets->LoadMaterialSource(Guid(extracted.Guid),source,error) && AssetArtifactIO::Parse(source,document),error);
        document["BaseColor"]=MaterialDefinition::Vector(0.1,0.2,0.95,1);
        Check(assets->SaveMaterialSource(Guid(extracted.Guid),AssetArtifactIO::JsonText(document),source,error),error);pump(idle);
        Check(resources->LoadMaterial(embedded,render,error) && render.BaseColor.B>0.94f,"Original FBX reference did not follow extracted material: "+error);
        Check(assets->ReimportAsset(Guid(embedded.Guid)),"Model reimport rejected");pump(idle);
        Check(resource("Model.fbx",eAssetType::Material)==embedded && resources->LoadMaterial(embedded,render,error) && render.BaseColor.B>0.94f,"Reimport lost mapping or LocalID");
        const QByteArray extractedBytes=ReadFile(root.filePath("Assets/Extracted.mat"));
        std::string output;
        Check(!assets->ExtractMaterial(Guid(embedded.Guid),embedded.LocalID,"Extracted.mat",output,error)
            && ReadFile(root.filePath("Assets/Extracted.mat"))==extractedBytes,"Extraction overwrote target");
        Check(!assets->ExtractMaterial(Guid(embedded.Guid),embedded.LocalID,"../Escape.mat",output,error)
            && !QFileInfo::exists(root.filePath("Escape.mat")),"Extraction escaped Assets");
        stage="manual source validation";
        Json::Value invalidSource;
        invalidSource["Version"]=1;
        invalidSource["BaseColorTexture"]["Guid"]=mesh.Guid;
        invalidSource["BaseColorTexture"]["LocalID"]=Json::UInt64(mesh.LocalID);
        write("WrongTexture.mat",QByteArray::fromStdString(AssetArtifactIO::JsonText(invalidSource)));
        invalidSource=Json::Value(Json::objectValue);invalidSource["Version"]=1;
        invalidSource["ExternalMaterial"]["Guid"]=extracted.Guid;
        invalidSource["ExternalMaterial"]["LocalID"]=Json::UInt64(extracted.LocalID);
        write("WrongAlias.mat",QByteArray::fromStdString(AssetArtifactIO::JsonText(invalidSource)));
        pump([&] {
            if (!idle()) return false;
            unsigned failed=0;
            for (IAssetMeta* meta:assets->GetAssetMetas())
                if ((meta->GetPath()=="WrongTexture.mat" || meta->GetPath()=="WrongAlias.mat")
                    && meta->GetImportState()==eAssetImportState::Failed && !meta->GetImportError().empty()) ++failed;
            return failed==2;
        });
        Check(ide.Counter.Loads==ide.Counter.Unloads && ide.Counter.Count==0,"Material imports left Loading active");
        cleanup();
        std::cout<<"PASS Shader-defined material controls, source save/revert/conflicts, FBX extraction/remap/reimport and runtime UV values\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr<<"FAIL "<<exception.what()<<"\n";cleanup();return 1;
    }
}
