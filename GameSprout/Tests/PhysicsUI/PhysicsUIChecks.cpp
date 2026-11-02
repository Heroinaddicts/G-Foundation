#include "PhysicsUI.h"
#include "ColliderComponentEditor.h"
#include "RigidbodyComponentEditor.h"
#include "PhysicsMaterialPanel.h"
#include "GameObjectInspector.h"
#include "ComponentInfoSection.h"
#include "AddComponentPopup.h"
#include "IInspectorUI.h"
#include "IPhysicsAuthoring.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "ResourceField.h"
#include "InspectorNumberText.h"
#include "GameSproutRuntimeExport.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QTest>
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#include <limits>
#include <cmath>

static void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static void Finish(QLineEdit* field,const QString& value){Check(field,"Missing Designer field");field->setText(value);QMetaObject::invokeMethod(field,"editingFinished",Qt::DirectConnection);}
static void Pump(){QCoreApplication::processEvents();}
class SceneService final:public ISceneManager {
public:
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;GameSproutRuntime::IScene* Scene=nullptr;std::string Path;
    uint64_t Removed=0;mutable int ComponentQueries=0;std::vector<fSceneEditOperation> Reverts;
    GameSproutRuntime::IScene* GetActiveScene() const override{return Scene;}
    const std::string& GetActiveScenePath() const override{return Path;}
    bool OpenScene(const std::string&,std::string&) override{return false;}
    bool EditObject(uint64_t id,const std::string&,const fSceneEditOperation& apply,const fSceneEditOperation& revert,std::string& error) override {
        auto* object=Runtime->QueryGameObject(id);if(!object||!apply(*object,error))return false;Reverts.push_back(revert);return true;
    }
    uint64_t GetComponentFileID(uint64_t,uint64_t componentID) const override{return componentID;}
    GameSproutRuntime::IComponent* QueryComponentByFileID(uint64_t objectID,uint64_t componentID) const override {
        ++ComponentQueries;auto* object=Runtime->QueryGameObject(objectID);if(!object)return nullptr;
        for(std::size_t index=0;index<object->GetComponentCount();++index){auto* item=object->GetComponentAt(index);if(item&&item->GetComponentInstanceID()==componentID)return item;}return nullptr;
    }
    bool RemoveComponentInstance(uint64_t,uint64_t id,std::string&) override{Removed=id;return true;}
    bool Undo(std::string&) override{return false;}bool Redo(std::string&) override{return false;}
    bool CanUndo() const override{return false;}bool CanRedo() const override{return false;}
    bool SaveScene(std::string&) override{return false;}bool SaveSceneAs(const std::string&,std::string&) override{return false;}
    bool HasUnsavedChanges() const override{return false;}
    bool CreateSceneAsset(const std::string&,const std::string&,std::string&,std::string&) override{return false;}
    bool RegisterSceneListener(ISceneListener*) override{return true;}void UnregisterSceneListener(ISceneListener*) override{}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class Inspector final:public IInspectorUI {
public:
    PhysicsUI Physics;
    bool HasPendingChanges() const override{return false;}
    bool ApplyChanges() override{return true;}void RevertChanges() override{}
    bool RegisterExtension(IInspectorExtension*) override{return true;}void UnregisterExtension(IInspectorExtension*) override{}
    ComponentEditor* CreateComponentEditor(const std::type_info& type,QWidget* parent) override{return Physics.CreateComponentEditor(type,parent);}
private:
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override{return true;}bool Launch(GameSproutEditor::GameSproutViewApi*) override{return true;}void Release(GameSproutEditor::GameSproutViewApi*) override{}
};
class TestMeta final:public IAssetMeta {
public:
    TestMeta():IAssetMeta(eAssetImporterType::PhysicsMaterial,Guid("1234567890abcdef1234567890abcdef")){}
    std::string Path="Surface.physicmaterial";
    const std::string& GetPath() const override{return Path;}
    IAssetImportSettings* GetImportSettings() const override{return nullptr;}
    std::vector<IAsset*> GetSubAssets() const override{return {};}
    eAssetImportState GetImportState() const override{return eAssetImportState::Ready;}
    std::string GetImportError() const override{return {};}
    bool SerializeMeta(std::string& text) const override{text="{}";return true;}bool SetSubAssets(const std::string&) override{return true;}bool SaveMeta() override{return true;}
};
class Assets final:public IAssetsManager {
public:
    TestMeta Meta;
    IAssetMeta* QueryAsset(const Guid& guid) const override{return guid.GetGuidString()==Meta._Guid.GetGuidString()?const_cast<TestMeta*>(&Meta):nullptr;}
    bool ReimportAsset(const Guid&) override{return true;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&) const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&) override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas() const override{return {const_cast<TestMeta*>(&Meta)};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return GetAssetMetas();}
    bool RegisterAssetListener(IAssetListener*) override{return true;}void UnregisterAssetListener(IAssetListener*) override{}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class Authoring final:public IPhysicsAuthoring {
public:
    GameSproutRuntime::PhysicsMaterialData Data;std::string Source="version-1";int Saves=0;
    bool CreatePhysicsMaterialAsset(const std::string&,const std::string&,std::string&,std::string&) override{return false;}
    bool LoadPhysicsMaterialSource(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::PhysicsMaterialData& data,std::string& source,std::string&) const override{data=Data;source=Source;return true;}
    bool SavePhysicsMaterialSource(const GameSproutRuntime::ResourceReference&,const GameSproutRuntime::PhysicsMaterialData& data,const std::string& expected,std::string& error) override{
        if(expected!=Source){error="External conflict";return false;}Data=data;Source+="+";++Saves;return true;
    }
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class PreparedMaterial final:public IAsset {
public:
    Guid ID{"1234567890abcdef1234567890abcdef"};std::string Name="Surface",Path,Descriptor="{}";eAssetType Type=eAssetType::PhysicsMaterial;
    const Guid& GetGuid() const override{return ID;}uint64_t GetLocalID() const override{return 17;}
    eAssetType GetAssetType() const override{return Type;}const std::string& GetName() const override{return Name;}
    const std::string& GetArtifactPath() const override{return Path;}const std::string& GetDataDescriptorJson() const override{return Descriptor;}
    uint32_t GetDataVersion() const override{return 2;}uint64_t GetDataSize() const override{return 1;}
};
class Library final:public ILibraryManager {
public:
    PreparedMaterial Asset;std::string Root;
    ILDBSession* GetLDBSession() const override{return nullptr;}
    const std::string& GetLibraryRoot() const override{return Root;}
    std::vector<std::string> GetManifestGuids() const override{return {Asset.ID.GetGuidString()};}
    bool GetManifest(const Guid& guid,AssetImportManifest& result) const override {
        if(guid.GetGuidString()!=Asset.ID.GetGuidString())return false;result={};result.GuidString=guid.GetGuidString();
        AssetArtifact item;item.LocalID=17;item.Type=Asset.Type;item.Name=Asset.Name;item.SourceKey="main";item.Path="surface.gsasset";result.Assets={item};return true;
    }
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&) override{return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&) override{return false;}
    std::vector<std::string> FindDependents(const std::string&) const override{return {};}
    IAsset* QueryAsset(const Guid& guid,uint64_t id) override{return guid.GetGuidString()==Asset.ID.GetGuidString()&&id==17?&Asset:nullptr;}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
static void CaptureVisuals(ComponentEditContext context,GameSproutRuntime::IGameObject& object,const QString& directory) {
    Check(QDir().mkpath(directory),"Cannot create visual check directory");
    QFile source(QString::fromUtf8(PHYSICS_INSPECTOR_UI_FILE));Check(source.open(QIODevice::ReadOnly),"Cannot read actual Inspector .ui theme");
    QXmlStreamReader xml(&source);QString theme;
    while(!xml.atEnd()) {
        xml.readNext();
        if(xml.isStartElement()&&xml.name()==QStringLiteral("property")&&xml.attributes().value(QStringLiteral("name"))==QStringLiteral("styleSheet")) {
            if(xml.readNextStartElement())theme=xml.readElementText();break;
        }
    }
    Check(!theme.isEmpty(),"Inspector theme is missing");
    Assets assets;Authoring authoring;Library library;QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create visual resource fixture");
    library.Root=temporary.path().toStdString();QDir(temporary.path()).mkpath("Artifact");library.Asset.Path=(temporary.path()+"/Artifact/surface.gsasset").toStdString();
    QFile artifact(QString::fromStdString(library.Asset.Path));Check(artifact.open(QIODevice::WriteOnly)&&artifact.write("x",1)==1,"Cannot create visual prepared material");artifact.close();
    library.Asset.Name="Stone Default Surface";
    context.Assets=&assets;context.Library=&library;
    const auto capture=[&](QWidget* content,int width,const QString& name) {
        QDialog host;host.setObjectName(QStringLiteral("InspectorUIWindow"));host.setStyleSheet(theme);
        auto* layout=new QVBoxLayout(&host);layout->setContentsMargins(10,10,10,10);layout->setSpacing(8);
        auto* title=new QLabel(name,&host);title->setObjectName(QStringLiteral("assetNameLabel"));layout->addWidget(title);
        content->setParent(&host);layout->addWidget(content);host.setFixedWidth(width);host.resize(width,host.sizeHint().height());host.show();Pump();
        Check(content->minimumSizeHint().width()<=width-20,"Physics editor minimum width exceeds the Inspector");
        for(auto* child:content->findChildren<QWidget*>()) {
            if(!child->isVisible())continue;
            if(!qobject_cast<QLineEdit*>(child)&&!qobject_cast<QComboBox*>(child)&&!qobject_cast<QSpinBox*>(child)&&!qobject_cast<QLabel*>(child)&&!qobject_cast<QCheckBox*>(child)&&!qobject_cast<QToolButton*>(child))continue;
            const QRect rect(child->mapTo(&host,QPoint()),child->size());
            Check(host.rect().contains(rect),"Physics control overflows the narrow Inspector");
            if(auto* label=qobject_cast<QLabel*>(child);label&&!label->wordWrap()&&!label->text().isEmpty())
                Check(label->fontMetrics().horizontalAdvance(label->text())<=label->contentsRect().width()+2,"Physics label text is clipped");
            if(auto* check=qobject_cast<QCheckBox*>(child))Check(check->sizeHint().width()<=check->width(),"Physics checkbox text is clipped");
        }
        const QString path=directory+"/physics-"+name.toLower().replace(' ','-')+"-"+QString::number(width)+".png";
        Check(host.grab().save(path),"Cannot save Physics visual check PNG");
    };
    for(const int width:{360,420}) {
        for(auto* component:object.GetComponents<GameSproutRuntime::IColliderComponent>()) {
            QString name;
            if(dynamic_cast<GameSproutRuntime::IBoxColliderComponent*>(component))name="Box Collider";
            else if(dynamic_cast<GameSproutRuntime::ISphereColliderComponent*>(component))name="Sphere Collider";
            else name="Capsule Collider";
            auto* panel=new ColliderComponentEditor(nullptr);context.ComponentID=component->GetComponentInstanceID();panel->Bind(context);panel->Load(*component);capture(panel,width,name);
        }
        if(auto* component=object.GetComponent<GameSproutRuntime::IRigidbodyComponent>()) {
            auto* panel=new RigidbodyComponentEditor(nullptr);context.ComponentID=component->GetComponentInstanceID();panel->Bind(context);panel->Load(*component);capture(panel,width,"Rigidbody");
        }
        InspectorServices services;services.Assets=&assets;auto* panel=new PhysicsMaterialPanel(services,&authoring,nullptr);InspectorTarget target;target.Assets={{assets.Meta._Guid.GetGuidString(),0,false}};std::string error;
        Check(panel->Load(target,true,error),"Cannot load material visual fixture");capture(panel,width,"Physics Material");
    }
}
int main(int argc,char** argv){
    QApplication app(argc,argv);GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2||argc==3,"Pass Runtime directory and optional visual output directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        Check(InspectorNumbers::Format(0.6f)==QStringLiteral("0.6")&&InspectorNumbers::Format(0.05f)==QStringLiteral("0.05")&&InspectorNumbers::Format(50.f)==QStringLiteral("50"),"Inspector exposes rounding tails or unnecessary exponents");
        for(float value:{0.6f,0.05f,1.2345678f,std::numeric_limits<float>::max(),std::numeric_limits<float>::min()}) {
            bool valid=false;const double restored=InspectorNumbers::Format(value).toDouble(&valid);
            Check(valid&&std::isfinite(restored)&&std::fabs(restored)<=std::numeric_limits<float>::max()&&static_cast<float>(restored)==value,"Inspector shortest number text lost float precision");
        }
        auto* scene=runtime->CreateScene("Physics UI");auto* object=scene->CreateGameObject("Object");
        auto* first=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();auto* second=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
        auto* sphere=object->AddComponent<GameSproutRuntime::ISphereColliderComponent>();auto* capsule=object->AddComponent<GameSproutRuntime::ICapsuleColliderComponent>();auto* body=object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
        Check(first&&second&&first!=second&&sphere&&capsule&&body,"Physics component types or multiplicity missing");
        SceneService scenes;scenes.Runtime=runtime;scenes.Scene=scene;Inspector inspector;std::string reported;
        ComponentEditContext context;context.Runtime=runtime;context.Scenes=&scenes;context.SceneID=scene->GetInstanceID();context.ObjectID=object->GetInstanceID();context.ComponentID=second->GetComponentInstanceID();context.Inspector=&inspector;context.Report=[&](const std::string& error){reported=error;};
        {
            ColliderComponentEditor editor(nullptr);editor.Bind(context);editor.Load(*second);
            Finish(editor.findChild<QLineEdit*>("sizeXEdit"),"2.5");
            Check(first->GetShape().Size.X==1&&second->GetShape().Size.X==2.5f&&scenes.ComponentQueries>0,"Editing second collider changed the first one");
            std::string error;Check(scenes.Reverts.back()(*object,error)&&second->GetShape().Size.X==1,"Collider edit did not expose valid undo values");
            auto shape=second->GetShape();shape.Size.X=3;Check(second->SetShape(shape),"Cannot change second collider");editor.Reload();
            Check(editor.findChild<QLineEdit*>("sizeXEdit")->text()=="3","Reload resolved the first collider");
            editor.Bind(context);editor.Load(*capsule);Check(editor.findChild<QLineEdit*>("heightEdit")&&!editor.findChild<QLineEdit*>("heightEdit")->isHidden(),"Capsule controls absent");
        }
        {
            Assets assets;Library library;QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create isolated material artifact");
            library.Root=temporary.path().toStdString();QDir(temporary.path()).mkpath("Artifact");library.Asset.Path=(temporary.path()+"/Artifact/surface.gsasset").toStdString();
            QFile artifact(QString::fromStdString(library.Asset.Path));Check(artifact.open(QIODevice::WriteOnly)&&artifact.write("x",1)==1,"Cannot prepare resource picker fixture");artifact.close();
            context.ComponentID=second->GetComponentInstanceID();context.Assets=&assets;context.Library=&library;
            ColliderComponentEditor editor(nullptr);editor.Bind(context);editor.Load(*second);auto* field=editor.findChild<ResourceField*>("materialField");std::string error;
            Check(field&&field->AssignAssetSelection({assets.Meta._Guid.GetGuidString(),0,false},error),"Physics material picker rejected the prepared resource type");
            Check(second->GetColliderSettings().Material.LocalID==17&&first->GetColliderSettings().Material.Guid.empty(),"Material binding targeted the first collider");
            library.Asset.Type=eAssetType::Material;Check(!field->AssignAssetSelection({assets.Meta._Guid.GetGuidString(),0,false},error),"Rendering material was accepted in a physics material field");
            context.Assets=nullptr;context.Library=nullptr;
        }
        {
            RigidbodyComponentEditor editor(nullptr);context.ComponentID=body->GetComponentInstanceID();editor.Bind(context);editor.Load(*body);
            Finish(editor.findChild<QLineEdit*>("massEdit"),"4");Check(body->GetSettings().Mass==4,"Rigidbody mass edit failed");
            for(const char* name:{"positionXCheck","positionYCheck","positionZCheck","rotationXCheck","rotationYCheck","rotationZCheck"})editor.findChild<QCheckBox*>(name)->click();
            Check(body->GetSettings().Constraints!=63&&reported.find("自由度")!=std::string::npos,"Freeze-all reached an invalid Jolt body");
        }
        {
            GameObjectInspector view(nullptr);context.ComponentID=0;view.Bind(context);view.Load(*object);
            ComponentInfoSection* section=nullptr;
            for(auto* widget:view.findChildren<QWidget*>())if(auto* candidate=dynamic_cast<ComponentInfoSection*>(widget);candidate&&candidate->GetComponentInstanceID()==second->GetComponentInstanceID())section=candidate;
            Check(section,"Second collider section absent");section->findChild<QToolButton*>("removeComponentButton")->click();
            Check(scenes.Removed==second->GetComponentInstanceID(),"Remove button did not use component instance identity");
            AddComponentPopup popup(context,nullptr);auto* tree=popup.findChild<QTreeWidget*>("componentTree");bool colliderAvailable=false,bodyDisabled=false;
            QTreeWidgetItemIterator items(tree);while(*items){const auto text=(*items)->text(0);if(text=="Box Collider")colliderAvailable=!(*items)->isDisabled();if(text.startsWith("Rigidbody"))bodyDisabled=(*items)->isDisabled();++items;}
            Check(colliderAvailable&&bodyDisabled,"Add Component did not respect multiplicity");
        }
        {
            Assets assets;Authoring authoring;InspectorServices services;services.Assets=&assets;
            PhysicsMaterialPanel panel(services,&authoring,nullptr);InspectorTarget target;target.Assets={{assets.Meta._Guid.GetGuidString(),0,false}};std::string error;
            Check(panel.Load(target,true,error)&&!panel.HasPendingChanges(),"Physics material did not load");
            auto* friction=panel.findChild<QLineEdit*>("frictionEdit");friction->setText("0.25");Check(panel.HasPendingChanges()&&panel.Apply(error)&&authoring.Data.Friction==0.25f&&authoring.Saves==1,"Physics material edit/save failed");
            panel.findChild<QLineEdit*>("restitutionEdit")->setText("2");Check(!panel.Validate(error)&&!panel.Apply(error)&&authoring.Saves==1,"Invalid restitution reached the source writer");
            Check(panel.Revert(error),"Material revert failed");friction->setText("0.8");authoring.Source="external";
            Check(panel.Load(target,false,error)&&panel.HasPendingChanges()&&!panel.Apply(error)&&friction->text()=="0.8","External edit discarded or overwrote the draft");
        }
        if(argc==3)CaptureVisuals(context,*object,QString::fromLocal8Bit(argv[2]));
        delete runtime;runtime=nullptr;std::cout<<"PASS Physics .ui editing, multi-collider identity, constraints, materials and draft conflicts\n";return 0;
    }catch(const std::exception& error){delete runtime;std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
