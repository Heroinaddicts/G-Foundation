#include "GameObjectInspector.h"
#include "ScriptUI.h"
#include "ScriptComponentEditor.h"
#include "SceneObjectField.h"
#include "HierarchyTreeModel.h"
#include "HierarchyTreeView.h"
#include "../../View/Shared/SceneObjectDragPayload.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QMouseEvent>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QTimer>
#include "IInspectorUI.h"
#include "IProjectAssetsUI.h"
#include "IAssetsManager.h"
#include "ICodeEditor.h"
#include "IConsole.h"
#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QTest>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <map>

namespace GameSproutEditor {
// A test host exercises the same public plugin lifecycle used by the Editor.
class GameSproutFinal {
public:
    static bool Start(IGameSproutView& view,GameSproutViewApi& editor) {
        view.BeforeInitialize(&editor);return view.Initialize(&editor)&&view.Launch(&editor);
    }
    static void Stop(IGameSproutView& view,GameSproutViewApi& editor){view.Release(&editor);}
};
}
class GuiBase:public GameSproutRuntime::ScriptBehaviour { public: GameSproutRuntime::IGameObject* Target=nullptr; };
class GuiPlayer final:public GuiBase {};
class GuiEnemy final:public GuiBase {};
static void Check(bool value,const std::string& text){if(!value)throw std::runtime_error(text);}
static void Pump(){QCoreApplication::processEvents();QTest::qWait(10);}
class SceneService;
class ScriptService final:public IScriptEditor {
public:
    std::vector<EditorScriptType> Types;
    std::vector<IScriptEditorListener*> Listeners;
    ScriptBuildState State;
    bool RejectCreation=false;std::string Created;
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    SceneService* Scenes=nullptr;
    std::uint64_t Epoch=1,Located=0;
    mutable int FieldReads=0;
    std::map<IScriptEditorListener*,ScriptFieldTarget> Observed;
    bool ObserveFields(const ScriptFieldTarget& target,IScriptEditorListener* listener)override {
        if(!Current(target)||std::find(Listeners.begin(),Listeners.end(),listener)==Listeners.end())return false;
        Observed[listener]=target;return true;
    }
    void UnobserveFields(IScriptEditorListener* listener)override {Observed.erase(listener);}
    bool ReadFields(uint64_t,uint64_t,ScriptPropertySnapshot&,std::string&)const override;
    bool SetField(const ScriptFieldTarget&,const std::string&,const GameSproutRuntime::ScriptValue&,std::string&)override;
    bool GetSceneObjectChoices(const ScriptFieldTarget&,std::vector<ScriptObjectChoice>&,std::string&)const override;
    bool ReadObjectTarget(uint64_t,ScriptObjectTarget&,std::string&)const override;
    bool ReferenceFromObject(const ScriptFieldTarget&,const ScriptObjectTarget&,GameSproutRuntime::ScriptValue&,std::string&)const override;
    bool LocateObjectReference(const ScriptFieldTarget&,const GameSproutRuntime::ScriptValue&,std::string&)override;
    void PublishFields(const ScriptFieldTarget& target) {const auto observed=Observed;for(const auto& [listener,value]:observed)if(value==target&&Observed.contains(listener))listener->OnScriptFieldsChanged(target);}
    bool Current(const ScriptFieldTarget&)const;
    std::vector<EditorScriptType> GetScriptTypes()const override{return Types;}
    ScriptBuildState GetBuildState()const override{return State;}
    bool RegisterScriptListener(IScriptEditorListener* listener)override{Listeners.push_back(listener);return true;}
    void UnregisterScriptListener(IScriptEditorListener* listener)override{std::erase(Listeners,listener);Observed.erase(listener);}
    bool CreateScript(const std::string& directory,const std::string& name,std::string& path,std::string& error)override{
        if(RejectCreation){error="脚本已存在，不能覆盖。";return false;}
        path=(directory.empty()?"":directory+"/")+name+".hpp";Created=path;return true;
    }
    bool RequestCompile(std::string&)override{State.Compiling=true;for(auto* listener:Listeners)listener->OnScriptBuildStateChanged(State);return true;}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class SceneService final:public ISceneManager {
public:
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    GameSproutRuntime::IScene* Scene=nullptr;
    std::string Path;
    std::function<void()> Changed;
    struct Edit {uint64_t ID;fSceneEditOperation Apply,Revert;};
    std::vector<Edit> History;std::size_t Position=0;
    GameSproutRuntime::IScene* GetActiveScene()const override{return Scene;}
    const std::string& GetActiveScenePath()const override{return Path;}
    bool OpenScene(const std::string&,std::string&)override{return false;}
    bool EditObject(uint64_t id,const std::string&,const fSceneEditOperation& apply,const fSceneEditOperation& revert,std::string& error)override{
        auto* object=Runtime->QueryGameObject(id);if(!object||!apply(*object,error))return false;
        History.resize(Position);History.push_back({id,apply,revert});++Position;if(Changed)Changed();return true;
    }
    bool AddBuiltinComponent(uint64_t id,const std::string& type,std::string& error)override{
        const bool result=Runtime->AddBuiltinComponent(Runtime->QueryGameObject(id),type,error)!=nullptr;if(result&&Changed)Changed();return result;
    }
    bool RemoveComponent(uint64_t id,const std::string& type,std::string& error)override{
        const bool result=Runtime->RemoveBuiltinComponent(Runtime->QueryGameObject(id),type,error);if(result&&Changed)Changed();return result;
    }
    bool AttachScriptComponent(uint64_t id,const std::string& type,const GameSproutRuntime::ResourceReference& source,std::string& error)override{
        const bool result=Runtime->AddScriptComponent(Runtime->QueryGameObject(id),type,source,0,error)!=nullptr;if(result&&Changed)Changed();return result;
    }
    bool RemoveScriptComponent(uint64_t id,uint64_t instance,std::string& error)override{
        const bool result=Runtime->RemoveScriptComponent(Runtime->QueryGameObject(id),instance,error);if(result&&Changed)Changed();return result;
    }
    bool Undo(std::string& error)override{if(!Position)return false;const auto& edit=History[--Position];const bool ok=edit.Revert(*Runtime->QueryGameObject(edit.ID),error);if(Changed)Changed();return ok;}
    bool Redo(std::string& error)override{if(Position>=History.size())return false;const auto& edit=History[Position++];const bool ok=edit.Apply(*Runtime->QueryGameObject(edit.ID),error);if(Changed)Changed();return ok;}
    bool CanUndo()const override{return Position!=0;}bool CanRedo()const override{return Position<History.size();}
    bool SaveScene(std::string&)override{return false;}
    bool SaveSceneAs(const std::string&,std::string&)override{return false;}
    bool HasUnsavedChanges()const override{return Position!=0;}
    bool CreateSceneAsset(const std::string&,const std::string&,std::string&,std::string&)override{return false;}
    bool RegisterSceneListener(ISceneListener*)override{return true;}
    void UnregisterSceneListener(ISceneListener*)override{}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
static GameSproutRuntime::IScriptComponent* FindScript(GameSproutRuntime::IGameObject* object,uint64_t instance){
    if(object)for(std::size_t i=0;i<object->GetComponentCount();++i)
        if(auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(object->GetComponentAt(i));script&&script->GetScriptInstanceID()==instance)return script;
    return nullptr;
}
bool ScriptService::Current(const ScriptFieldTarget& target)const {
    return Scenes&&Scenes->Scene&&target.SessionID==Epoch&&target.WorldID==Scenes->Scene->GetInstanceID()&&
        FindScript(Runtime->QueryGameObject(target.ObjectID),target.ScriptInstanceID);
}
bool ScriptService::ReadFields(uint64_t object,uint64_t instance,ScriptPropertySnapshot& snapshot,std::string& error)const {
    ++FieldReads;
    snapshot={};snapshot.Target={Epoch,Scenes->Scene->GetInstanceID(),object,instance};
    auto* script=FindScript(Runtime->QueryGameObject(object),instance);
    if(!script){error="Script missing";return false;}
    for(const auto& type:Types)if(type.Type.TypeID==script->GetScriptTypeID())for(const auto& field:type.Type.Fields){
        ScriptFieldSnapshot item;item.Info=field;
        const auto value=script->GetFields().find(field.ID);item.Value=value==script->GetFields().end()?field.Default:value->second;
        if(item.Value.Kind==GameSproutRuntime::ScriptValueKind::SceneObject&&item.Value.SceneObject.Assigned){
            auto* target=Runtime->QuerySceneObjectByFileID(Scenes->Scene,item.Value.SceneObject.ObjectFileID);
            item.MissingObject=target==nullptr;
            if(target){item.ObjectName=target->GetName();item.ObjectPath=Scenes->Scene->GetName()+"/"+target->GetName();}
        }
        snapshot.Fields.push_back(item);
    }
    error.clear();return true;
}
bool ScriptService::SetField(const ScriptFieldTarget& target,const std::string& field,const GameSproutRuntime::ScriptValue& value,std::string& error){
    if(!Current(target)){error="Stale script field target";return false;}
    const auto previous=FindScript(Runtime->QueryGameObject(target.ObjectID),target.ScriptInstanceID)->GetFields().at(field);
    const auto operation=[instance=target.ScriptInstanceID,field](GameSproutRuntime::ScriptValue data){
        return [instance,field,data](GameSproutRuntime::IGameObject& object,std::string& message){
            auto* script=FindScript(&object,instance);return script&&script->SetField(field,data,message);
        };
    };
    if(!Scenes->EditObject(target.ObjectID,"修改脚本属性",operation(value),operation(previous),error))return false;
    PublishFields(target);return true;
}
bool ScriptService::GetSceneObjectChoices(const ScriptFieldTarget& target,std::vector<ScriptObjectChoice>& choices,std::string& error)const {
    choices.clear();if(!Current(target)){error="Stale script field target";return false;}
    std::vector<GameSproutRuntime::IGameObject*> nodes{Scenes->Scene};
    for(std::size_t index=0;index<nodes.size();++index){
        auto* object=nodes[index];std::uint64_t fileID=0;
        if(Runtime->GetSceneObjectFileID(object,fileID))choices.push_back({fileID,object->GetName(),Scenes->Scene->GetName()+"/"+object->GetName()});
        for(std::size_t child=0;child<object->GetChildCount();++child)nodes.push_back(object->GetChild(child));
    }
    return true;
}
bool ScriptService::ReadObjectTarget(uint64_t objectID,ScriptObjectTarget& target,std::string& error)const {
    auto* object=Runtime->QueryGameObject(objectID);
    if(!object||object->GetScene()!=Scenes->Scene){error="Object belongs to another world";return false;}
    target={Epoch,Scenes->Scene->GetInstanceID(),objectID};return true;
}
bool ScriptService::ReferenceFromObject(const ScriptFieldTarget& target,const ScriptObjectTarget& source,GameSproutRuntime::ScriptValue& value,std::string& error)const {
    if(!Current(target)||source.SessionID!=Epoch||source.WorldID!=target.WorldID){error="Object belongs to another world or session";return false;}
    auto* object=Runtime->QueryGameObject(source.ObjectID);std::uint64_t fileID=0;
    if(!object||object->GetScene()!=Scenes->Scene||!Runtime->GetSceneObjectFileID(object,fileID)){error="Object reference missing";return false;}
    value={};value.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;value.SceneObject={true,fileID};return true;
}
bool ScriptService::LocateObjectReference(const ScriptFieldTarget& target,const GameSproutRuntime::ScriptValue& value,std::string& error){
    if(!Current(target)||!value.SceneObject.Assigned){error="Object reference missing";return false;}
    auto* object=Runtime->QuerySceneObjectByFileID(Scenes->Scene,value.SceneObject.ObjectFileID);
    if(!object){error="Object reference missing";return false;}Located=object->GetInstanceID();return true;
}
class ProjectView final:public IProjectAssetsUI {
public:
    fContextActionCallback Create=nullptr;std::string Notified;
    fAssetOpenHandler Open;
    const void* OpenOwner=nullptr;
    std::vector<ProjectAssetSelection> GetSelection()const override{return {};}
    bool NavigateToDirectory(const std::string&)override{return true;}
    bool RegisterAssetOpenHandler(const void* owner,const fAssetOpenHandler& handler)override{OpenOwner=owner;Open=handler;return true;}
    bool RegisterContextAction(const void*,const std::vector<std::string>& path,fContextActionCallback callback,const fContextActionEnabled&)override{
        Check(path==std::vector<std::string>{"Create","Script.hpp"},"Wrong script creation menu");Create=callback;return true;
    }
    void UnregisterExtensions(const void* owner)override{if(OpenOwner==owner){Open={};OpenOwner=nullptr;}Create=nullptr;}
    ProjectAssetActionContext GetActionContext()const override{return {"Scripts",{}};}
    void NotifyAssetCreated(const std::string& path)override{Notified=path;}
private:
    bool Initialize(GameSproutEditor::GameSproutViewApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutViewApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutViewApi*)override{}
};
class Meta final:public IAssetMeta {
public:
    Meta(const char* guid,eAssetImporterType type,const char* path):IAssetMeta(type,Guid(guid)),Path(path){}
    std::string Path;
    const std::string& GetPath()const override{return Path;}
    IAssetImportSettings* GetImportSettings()const override{return nullptr;}
    std::vector<IAsset*> GetSubAssets()const override{return {};}
    eAssetImportState GetImportState()const override{return eAssetImportState::NotRequired;}
    std::string GetImportError()const override{return {};}
    bool SerializeMeta(std::string&)const override{return false;}
    bool SetSubAssets(const std::string&)override{return false;}
    bool SaveMeta()override{return false;}
};
class Assets final:public IAssetsManager {
public:
    std::vector<IAssetMeta*> Values;
    IAssetMeta* QueryAsset(const Guid& guid)const override{for(auto* meta:Values)if(meta->_Guid.GetGuidString()==guid.GetGuidString())return meta;return nullptr;}
    bool ReimportAsset(const Guid&)override{return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&)override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&)const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&)override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&)override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas()const override{return Values;}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&)const override{return {};}
    bool RegisterAssetListener(IAssetListener*)override{return true;}
    void UnregisterAssetListener(IAssetListener*)override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class CodeEditor final:public ICodeEditor {
public:
    std::vector<std::string> Opened;
    bool Reject=false;
    bool OpenSource(const std::string& path,std::string& error)override{Opened.push_back(path);if(Reject){error="Xcode project generation failed";return false;}error.clear();return true;}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class Console final:public IConsole {
public:
    std::vector<std::string> Errors;
    void ConsoleLog(const char*,int,eConsoleLog level,const std::string& message)override{if(level==eConsoleLog::Error)Errors.push_back(message);}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class InspectorView final:public IInspectorUI {
public:
    IInspectorExtension* Extension=nullptr;
    bool HasPendingChanges()const override{return false;}bool ApplyChanges()override{return true;}void RevertChanges()override{}
    bool RegisterExtension(IInspectorExtension* extension)override{Extension=extension;return true;}
    void UnregisterExtension(IInspectorExtension*)override{Extension=nullptr;}
    ComponentEditor* CreateComponentEditor(const std::type_info& type,QWidget* parent)override{return Extension?Extension->CreateComponentEditor(type,parent):nullptr;}
private:
    bool Initialize(GameSproutEditor::GameSproutViewApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutViewApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutViewApi*)override{}
};
class Editor final:public GameSproutEditor::GameSproutViewApi {
public:
    Editor(GameSproutRuntime::IGameSproutRuntime* runtime,ScriptService& scripts,InspectorView& inspector,ProjectView& project,Assets& assets,CodeEditor& code,Console& console)
        :GameSproutViewApi(runtime),Scripts(scripts),Inspector(inspector),Project(project),AssetValues(assets),Code(code),Logs(console){}
    ScriptService& Scripts;InspectorView& Inspector;ProjectView& Project;
    Assets& AssetValues;CodeEditor& Code;Console& Logs;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name)const override{
        if(name=="ScriptEditor")return &Scripts;if(name=="AssetsManager")return &AssetValues;
        if(name=="CodeEditor")return &Code;if(name=="Console")return &Logs;return nullptr;
    }
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name)const override{return name=="InspectorUI"?static_cast<GameSproutEditor::IGameSproutView*>(&Inspector):name=="ProjectAssetsUI"?static_cast<GameSproutEditor::IGameSproutView*>(&Project):nullptr;}
};
static QLineEdit* ScriptNumber(QWidget& inspector,int instanceIndex){
    auto editors=inspector.findChildren<QWidget*>("ScriptComponentEditorForm");
    Check(editors.size()>instanceIndex,"Script component editor missing");
    for(auto* row:editors[instanceIndex]->findChildren<QWidget*>("ScriptPropertyRowForm")) {
        auto* label=row->findChild<QLabel*>("nameLabel");if(label&&label->text()=="Speed")return row->findChild<QLineEdit*>("scalarEdit");
    }throw std::runtime_error("Script Speed field missing");
}
static QWidget* ScriptField(QWidget& inspector,int instanceIndex,const QString& name){
    const auto editors=inspector.findChildren<QWidget*>("ScriptComponentEditorForm");
    Check(editors.size()>instanceIndex,"Script component editor missing");
    for(auto* row:editors[instanceIndex]->findChildren<QWidget*>("ScriptPropertyRowForm"))
        if(auto* label=row->findChild<QLabel*>("nameLabel");label&&label->text()==name)return row;
    throw std::runtime_error("Script field missing: "+name.toStdString());
}
class ReferenceGestureTree final : public HierarchyTreeView {
public:
    int Drags=0;
    std::function<void(const QMimeData*)> DropReference;
protected:
    void ExecuteDrag(Qt::DropActions) override {
        ++Drags;
        QMimeData* mime=model()->mimeData(selectionModel()->selectedRows());
        QEventLoop nested;
        QTimer::singleShot(0,&nested,[&]{if(DropReference)DropReference(mime);nested.quit();});
        nested.exec();
        delete mime;
    }
};
int main(int argc,char** argv){
    QApplication app(argc,argv);
    try{
        Check(argc>1,"Pass Runtime dylib directory");auto* runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        ScriptService scripts;SceneService scenes;scenes.Runtime=runtime;scenes.Scene=runtime->CreateScene("Scene");
        auto* object=scenes.Scene->CreateGameObject("Player");std::string error;
        scripts.Runtime=runtime;scripts.Scenes=&scenes;
        Check(runtime->BindSceneObjectIdentity(scenes.Scene,0,error),error);
        Check(runtime->BindSceneObjectIdentity(object,1,error),error);
        auto* targetObject=scenes.Scene->CreateGameObject("Target with a very long readable name");
        Check(runtime->BindSceneObjectIdentity(targetObject,2,error),error);
        GameSproutRuntime::ScriptTypeRegistration registration;
        registration.Info={"0123456789abcdef0123456789abcdef","PlayerControllerWithLongName",{}, {}};
        registration.CppType=&typeid(GuiPlayer);registration.Source={registration.Info.TypeID,1};
        registration.Create=[](GameSproutRuntime::IGameObject* object)->GameSproutRuntime::IScript*{GameSproutRuntime::Internal::ScriptConstructionScope scope(object);return new GuiPlayer();};
        registration.Destroy=[](GameSproutRuntime::IScript* script){delete script;};
        GameSproutRuntime::ScriptValue defaultValue;defaultValue.Number=1;
        GameSproutRuntime::ScriptFieldRegistration field;field.Info={"speed","Speed",defaultValue};
        field.Read=[](const auto&,auto&,auto&){return true;};field.Write=[](auto&,const auto&,auto&){return true;};registration.Fields.push_back(field);
        const auto addField=[&](const char* id,const char* name,GameSproutRuntime::ScriptValue value){auto item=field;item.Info={id,name,value};registration.Fields.push_back(item);};
        GameSproutRuntime::ScriptValue value;value.Kind=GameSproutRuntime::ScriptValueKind::Boolean;addField("active","Active",value);
        value={};value.Kind=GameSproutRuntime::ScriptValueKind::Integer;value.Integer=42;addField("count","Count",value);
        value={};value.Kind=GameSproutRuntime::ScriptValueKind::String;value.String="Player";addField("title","Title",value);
        value={};value.Kind=GameSproutRuntime::ScriptValueKind::Vector3;value.Vector={1,2,3};addField("offset","Offset",value);
        value={};value.Kind=GameSproutRuntime::ScriptValueKind::Resource;value.Resource={"fedcba9876543210fedcba9876543210",1};addField("resource","Resource",value);
        registration.Fields.push_back(GameSproutRuntime::Internal::MakeField<GuiBase>("target","Target",&GuiBase::Target,static_cast<GameSproutRuntime::IGameObject*>(nullptr)));
        Check(runtime->RegisterScriptType(registration,error),error);
        const GameSproutRuntime::ResourceReference source{"0123456789abcdef0123456789abcdef",1};
        scripts.Types.push_back({runtime->GetScriptTypes().front(),source,"Scripts/PlayerControllerWithLongName.hpp"});
        auto enemyRegistration=registration;enemyRegistration.Info.TypeID="1123456789abcdef0123456789abcdef";enemyRegistration.Info.Name="EnemyControllerWithLongName";
        enemyRegistration.CppType=&typeid(GuiEnemy);enemyRegistration.Source={enemyRegistration.Info.TypeID,1};
        enemyRegistration.Create=[](GameSproutRuntime::IGameObject* object)->GameSproutRuntime::IScript*{GameSproutRuntime::Internal::ScriptConstructionScope scope(object);return new GuiEnemy();};
        Check(runtime->RegisterScriptType(enemyRegistration,error),error);
        scripts.Types.push_back({runtime->GetScriptTypes().back(),enemyRegistration.Source,"Scripts/EnemyControllerWithLongName.hpp"});
        auto* first=runtime->AddScriptComponent(object,registration.Info.TypeID,source,0,error);Check(first,error);
        auto* second=runtime->AddScriptComponent(object,enemyRegistration.Info.TypeID,enemyRegistration.Source,0,error);Check(second,error);
        Check(!runtime->AddScriptComponent(object,registration.Info.TypeID,source,0,error),"Duplicate script class was attached");
        GameSproutRuntime::ScriptTypeInfo extra;extra.TypeID="2123456789abcdef0123456789abcdef";extra.Name="ExtraController";
        scripts.Types.push_back({extra,{extra.TypeID,1},"Scripts/ExtraController.hpp"});
        const auto firstID=first->GetScriptInstanceID(),secondID=second->GetScriptInstanceID();
        InspectorView inspector;ProjectView project;Assets assets;CodeEditor code;Console console;
        Meta sourceMeta("0123456789abcdef0123456789abcdef",eAssetImporterType::Script,"Scripts/PlayerControllerWithLongName.hpp");
        Meta textureMeta("3123456789abcdef0123456789abcdef",eAssetImporterType::Texture,"Textures/Player.png");
        Meta otherScript("4123456789abcdef0123456789abcdef",eAssetImporterType::Script,"Scripts/Player.lua");
        assets.Values={&sourceMeta,&textureMeta,&otherScript};
        Editor editor(runtime,scripts,inspector,project,assets,code,console);ScriptUI feature;
        Check(GameSproutEditor::GameSproutFinal::Start(feature,editor),"Cannot launch ScriptUI");
        Check(project.Open&&project.OpenOwner==&feature,"Script source open handler not registered");
        const ProjectAssetSelection sourceSelection{sourceMeta._Guid.GetGuidString(),0,false};
        Check(project.Open(sourceSelection)&&code.Opened==std::vector<std::string>{sourceMeta.Path},"Asset source did not request the game code project");
        sourceMeta.Path="Nested Folder/角色/Player.HPP";
        Check(project.Open(sourceSelection)&&code.Opened.back()==sourceMeta.Path,"Handler retained a stale path or rejected case-insensitive .hpp");
        const auto openCount=code.Opened.size();
        Check(!project.Open({textureMeta._Guid.GetGuidString(),0,false})&&!project.Open({otherScript._Guid.GetGuidString(),0,false})&&
            !project.Open({sourceMeta._Guid.GetGuidString(),1,true})&&!project.Open({sourceMeta._Guid.GetGuidString(),1,false})&&
            !project.Open({"missing",0,false})&&code.Opened.size()==openCount,"Script open intercepted a non-native, invalid or subasset target");
        code.Reject=true;
        Check(project.Open(sourceSelection)&&console.Errors==std::vector<std::string>{"Xcode project generation failed"},"Failed code-project open fell through to system .hpp handling or hid the error");
        code.Reject=false;sourceMeta.Path="Scripts/PlayerControllerWithLongName.hpp";
        GameObjectInspector objectUI(nullptr);ComponentEditContext context;context.Scenes=&scenes;context.Runtime=runtime;context.Scripts=&scripts;context.Inspector=&inspector;context.Assets=&assets;
        context.SceneID=scenes.Scene->GetInstanceID();context.ObjectID=object->GetInstanceID();context.Report=[](const std::string&){};
        objectUI.Bind(context);objectUI.Load(*object);objectUI.resize(360,620);objectUI.show();Pump();
        auto scriptForms=objectUI.findChildren<QWidget*>("ScriptComponentEditorForm");
        Check(!scriptForms.empty(),"Script source editor not created");
        auto* sourceEdit=scriptForms.front()->findChild<QLineEdit*>("sourceEdit");
        Check(sourceEdit&&sourceEdit->toolTip().contains(QStringLiteral("双击")),"Source field has no code-project opening hint");
        sourceMeta.Path="Moved Folder/重新定位.hpp";
        QTest::mouseDClick(sourceEdit,Qt::LeftButton);Pump();
        Check(code.Opened.back()==sourceMeta.Path,"Inspector source double-click used a stale path or bypassed the code project");
        code.Reject=true;QTest::mouseDClick(sourceEdit,Qt::LeftButton);Pump();
        auto* sourceError=scriptForms.front()->findChild<QPlainTextEdit*>("errorEdit");
        Check(sourceError&&sourceError->isVisible()&&sourceError->toPlainText().contains("Xcode project generation failed"),"Inspector code-project open failure was not displayed");
        code.Reject=false;sourceMeta.Path="Scripts/PlayerControllerWithLongName.hpp";
        scenes.Changed=[&]{objectUI.Load(*object);};
        auto* edit=ScriptNumber(objectUI,1);edit->setText("7.5");QMetaObject::invokeMethod(edit,"editingFinished",Qt::DirectConnection);Pump();
        Check(first->GetFields().at("speed").Number==1&&second->GetFields().at("speed").Number==7.5,"Editing second script changed the first instance");
        Check(scenes.Undo(error),error);Pump();Check(ScriptNumber(objectUI,1)->text()=="1","Undo did not update second script field");
        Check(scenes.Redo(error),error);Pump();Check(ScriptNumber(objectUI,1)->text()=="7.5","Redo did not restore second script field");
        ScriptField(objectUI,1,"Active")->findChild<QCheckBox*>("booleanCheck")->click();Pump();
        Check(second->GetFields().at("active").Boolean&&!first->GetFields().at("active").Boolean,"Boolean field changed wrong instance");
        auto* integer=ScriptField(objectUI,1,"Count")->findChild<QLineEdit*>("scalarEdit");integer->setText("9223372036854775806");QMetaObject::invokeMethod(integer,"editingFinished",Qt::DirectConnection);Pump();
        Check(second->GetFields().at("count").Integer==9223372036854775806LL,"Integer field lost 64-bit precision");
        auto* title=ScriptField(objectUI,1,"Title")->findChild<QLineEdit*>("scalarEdit");title->setText("你好 GameSprout");QMetaObject::invokeMethod(title,"editingFinished",Qt::DirectConnection);Pump();
        Check(second->GetFields().at("title").String=="你好 GameSprout","UTF-8 string field did not save");
        auto* offset=ScriptField(objectUI,1,"Offset")->findChild<QLineEdit*>("xEdit");offset->setText("-1.25");QMetaObject::invokeMethod(offset,"editingFinished",Qt::DirectConnection);Pump();
        const auto vector=second->GetFields().at("offset").Vector;
        Check(vector.X==-1.25f&&vector.Y==2&&vector.Z==3,"Vector field changed untouched axes");
        ScriptField(objectUI,1,"Resource")->findChild<QAction*>("clearResourceAction")->trigger();Pump();
        Check(second->GetFields().at("resource").Resource.Guid.empty()&&!first->GetFields().at("resource").Resource.Guid.empty(),"Resource clear changed wrong script instance");
        // The reference editor talks only to the Module value interface.
        const auto referenceField=[&](){
            auto* widget=ScriptField(objectUI,1,"Target")->findChild<QWidget*>("SceneObjectFieldForm");
            Check(widget,"Scene object reference control missing");return static_cast<SceneObjectField*>(widget);
        };
        auto* reference=referenceField();
        Check(reference->findChild<QLineEdit*>("objectNameEdit")->text().contains("None"),"Unassigned object reference is not shown as None");
        // Exercise the complete interaction: Qt's press/drag source must not
        // replace the inspected owner before its real reference field receives
        // the production Hierarchy model's payload in the nested drag loop.
        {
            HierarchyTreeModel model(nullptr);
            model.SetDragSource([&](std::uint64_t world,std::uint64_t id){
                return SceneObjectDragPayload::Encode({scripts.Epoch,world,id});
            });
            model.Build(scenes.Scene,QColor());
            QSortFilterProxyModel proxy;proxy.setSourceModel(&model);
            ReferenceGestureTree tree;tree.setModel(&proxy);tree.setHeaderHidden(true);
            tree.setSelectionMode(QAbstractItemView::SingleSelection);tree.setSelectionBehavior(QAbstractItemView::SelectRows);
            tree.setEditTriggers(QAbstractItemView::NoEditTriggers);tree.setDragEnabled(true);
            tree.setDragDropMode(QAbstractItemView::DragOnly);tree.setDefaultDropAction(Qt::CopyAction);
            tree.resize(420,300);tree.expandAll();tree.show();Pump();
            tree.selectionModel()->setCurrentIndex(proxy.mapFromSource(model.FindNode(object->GetInstanceID())),
                QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
            std::uint64_t inspected=object->GetInstanceID();
            int selectionChanges=0;
            const auto publish=[&]{
                if(tree.IsPointerSelecting())return;
                const auto rows=tree.selectionModel()->selectedRows();
                inspected=rows.empty()?0:rows.front().data(HierarchyTreeModel::NodeIDRole).toULongLong();
                ++selectionChanges;
                // Deliberately rebuild the real Inspector on a committed
                // selection, reproducing the disappearing Target regression.
                if(auto* selected=runtime->QueryGameObject(inspected))objectUI.Load(*selected);
            };
            tree.ClickCommitted=publish;
            QObject::connect(tree.selectionModel(),&QItemSelectionModel::currentChanged,&tree,publish);
            QObject::connect(tree.selectionModel(),&QItemSelectionModel::selectionChanged,&tree,publish);
            QPointer<SceneObjectField> originalField(reference);
            bool keptOwner=false,payloadCorrect=false,entered=false,dropped=false;
            tree.DropReference=[&](const QMimeData* mime){
                keptOwner=inspected==object->GetInstanceID()&&selectionChanges==0&&!originalField.isNull();
                SceneObjectDragPayload::Value source;
                payloadCorrect=SceneObjectDragPayload::Decode(mime,source)&&source.ObjectID==targetObject->GetInstanceID();
                if(!keptOwner||!payloadCorrect)return;
                QDragEnterEvent enter(QPoint(5,5),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(originalField,&enter);entered=enter.isAccepted();
                QDropEvent drop(QPointF(5,5),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(originalField,&drop);dropped=drop.isAccepted();
            };
            const QPoint from=tree.visualRect(proxy.mapFromSource(model.FindNode(targetObject->GetInstanceID()))).center();
            QTest::mousePress(tree.viewport(),Qt::LeftButton,Qt::NoModifier,from);Pump();
            Check(inspected==object->GetInstanceID()&&selectionChanges==0&&originalField,
                "Hierarchy mouse-down replaced the real Inspector before drag started");
            for(int distance:{2,QApplication::startDragDistance()+12,QApplication::startDragDistance()+20}){
                const QPoint position=from+QPoint(distance,0);
                QMouseEvent move(QEvent::MouseMove,position,tree.viewport()->mapToGlobal(position),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(tree.viewport(),&move);
            }
            QTest::mouseRelease(tree.viewport(),Qt::LeftButton,Qt::NoModifier,from);Pump();
            Check(tree.Drags==1&&keptOwner&&payloadCorrect&&entered&&dropped,
                "Hierarchy gesture failed to deliver its source to the owner's real reference field");
            Check(inspected==object->GetInstanceID()&&selectionChanges==0&&
                second->GetFields().at("target").SceneObject==GameSproutRuntime::ScriptObjectReference{true,2},
                "Reference gesture changed the Inspector target or saved the wrong object FileID");
            Check(scenes.Undo(error),error);Pump();
            Check(!second->GetFields().at("target").SceneObject.Assigned,"Gesture reference assignment did not support undo");
        }
        reference=referenceField();
        auto* drag=SceneObjectDragPayload::Encode({scripts.Epoch,scenes.Scene->GetInstanceID(),targetObject->GetInstanceID()});
        Check(drag,"Cannot encode scene drag");
        SceneObjectDragPayload::Value decoded;
        Check(SceneObjectDragPayload::Decode(drag,decoded)&&decoded.ObjectID==targetObject->GetInstanceID(),"Scene drag lost object identity");
        QDragEnterEvent enter(QPoint(5,5),Qt::CopyAction,drag,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(reference,&enter);Check(enter.isAccepted(),"Valid hierarchy drag was rejected");
        QDropEvent drop(QPointF(5,5),Qt::CopyAction,drag,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(reference,&drop);delete drag;Pump();
        Check(drop.isAccepted()&&second->GetFields().at("target").SceneObject==GameSproutRuntime::ScriptObjectReference{true,2},"Hierarchy drop did not store the target FileID");
        Check(!first->GetFields().at("target").SceneObject.Assigned,"Reference edit changed another script instance");
        reference=referenceField();
        auto* objectName=reference->findChild<QLineEdit*>("objectNameEdit");
        Check(objectName->toolTip().contains(QString::fromStdString(targetObject->GetName()))&&!objectName->text().isEmpty(),"Object field lost readable name/full path tooltip");
        reference->grab().save("/tmp/gamesprout-script-object-reference-240.png");
        reference->findChild<QToolButton*>("locateObjectButton")->click();Pump();
        Check(scripts.Located==targetObject->GetInstanceID(),"Locate did not use the reference service");
        const int idleReads=scripts.FieldReads;Pump();Pump();Check(scripts.FieldReads==idleReads,"Idle Inspector polled script fields");
        auto* wrongWorld=SceneObjectDragPayload::Encode({scripts.Epoch,scenes.Scene->GetInstanceID()+100,targetObject->GetInstanceID()});
        QDragEnterEvent wrongEnter(QPoint(5,5),Qt::CopyAction,wrongWorld,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(reference,&wrongEnter);delete wrongWorld;
        Check(!wrongEnter.isAccepted()&&second->GetFields().at("target").SceneObject.ObjectFileID==2,"Object drag from another world changed the reference");
        QMimeData malformed;malformed.setData(SceneObjectDragPayload::MimeType(),"not an object address or identity");
        Check(!SceneObjectDragPayload::Decode(&malformed,decoded),"Malformed scene drag was accepted");
        reference->findChild<QToolButton*>("pickObjectButton")->click();Pump();
        auto* picker=reference->findChild<QDialog*>("SceneObjectPickerForm");Check(picker&&picker->isVisible(),"Scene object picker did not open");
        auto* choices=picker->findChild<QTreeWidget*>("objectTree");QTreeWidgetItem* sceneRoot=nullptr;
        for(int index=0;index<choices->topLevelItemCount();++index){auto* choice=choices->topLevelItem(index);if(choice->data(0,Qt::UserRole).toBool()&&choice->data(0,Qt::UserRole+1).toULongLong()==0)sceneRoot=choice;}
        Check(sceneRoot,"Picker omitted scene root FileID 0");choices->setCurrentItem(sceneRoot);
        picker->findChild<QPushButton*>("selectButton")->click();Pump();
        Check(second->GetFields().at("target").SceneObject==GameSproutRuntime::ScriptObjectReference{true,0},"Picker confused scene-root reference with None");
        referenceField()->findChild<QToolButton*>("clearObjectButton")->click();Pump();
        Check(!second->GetFields().at("target").SceneObject.Assigned,"Clear did not remove object reference");
        Check(scenes.Undo(error),error);Pump();Check(second->GetFields().at("target").SceneObject==GameSproutRuntime::ScriptObjectReference{true,0},"Undo did not restore root object reference");
        Check(scenes.Redo(error),error);Pump();Check(!second->GetFields().at("target").SceneObject.Assigned,"Redo did not clear object reference");
        ScriptPropertySnapshot snapshot;Check(scripts.ReadFields(object->GetInstanceID(),secondID,snapshot,error),error);
        value={};value.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;value.SceneObject={true,999};
        scenes.Changed={};
        Check(scripts.SetField(snapshot.Target,"target",value,error),error);Pump();
        reference=referenceField();
        Check(reference->findChild<QLineEdit*>("objectNameEdit")->text().contains("Missing")&&
            !reference->findChild<QToolButton*>("locateObjectButton")->isEnabled()&&reference->findChild<QToolButton*>("clearObjectButton")->isEnabled(),"Reference event did not update Missing controls");
        const auto staleTarget=snapshot.Target;++scripts.Epoch;
        value.SceneObject={};Check(!scripts.SetField(staleTarget,"target",value,error),"Stale session field command was accepted");
        auto* oldDrag=SceneObjectDragPayload::Encode({scripts.Epoch,scenes.Scene->GetInstanceID(),targetObject->GetInstanceID()});
        QDragEnterEvent staleEnter(QPoint(5,5),Qt::CopyAction,oldDrag,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(reference,&staleEnter);delete oldDrag;Check(!staleEnter.isAccepted(),"Stale field control accepted a new drag");
        objectUI.Load(*object);Pump();
        // Both worlds can reuse the same numeric IDs after a Player restart.
        // A current field must still reject a drag from the preceding session.
        auto* previousSession=SceneObjectDragPayload::Encode({scripts.Epoch-1,scenes.Scene->GetInstanceID(),targetObject->GetInstanceID()});
        QDragEnterEvent previousEnter(QPoint(5,5),Qt::CopyAction,previousSession,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(referenceField(),&previousEnter);delete previousSession;
        Check(!previousEnter.isAccepted(),"Current field accepted an old sender session with reused world/object IDs");
        referenceField()->findChild<QToolButton*>("clearObjectButton")->click();Pump();
        Check(!second->GetFields().at("target").SceneObject.Assigned,"Refreshed session could not edit reference");
        scenes.Changed=[&]{objectUI.Load(*object);};
        objectUI.grab().save("/tmp/gamesprout-script-inspector-360.png");
        auto* add=objectUI.findChild<QPushButton*>("addComponentButton");Check(add&&add->isVisible(),"Add Component button missing");add->click();Pump();
        auto* popup=objectUI.findChild<QDialog*>("AddComponentPopupForm");Check(popup&&popup->isVisible(),"Add Component popup missing");
        auto* search=popup->findChild<QLineEdit*>("searchEdit");search->setText("Camera");Pump();popup->grab().save("/tmp/gamesprout-add-component-360.png");
        auto* tree=popup->findChild<QTreeWidget*>("componentTree");Check(tree->topLevelItemCount()==1&&tree->topLevelItem(0)->childCount()==1,"Component search returned wrong catalog");
        auto* item=tree->topLevelItem(0)->child(0);QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,tree->visualItemRect(item).center());Pump();
        Check(object->GetComponent<GameSproutRuntime::ICameraComponent>(),"Popup did not add built-in Camera");
        add->click();Pump();popup=objectUI.findChild<QDialog*>("AddComponentPopupForm");search=popup->findChild<QLineEdit*>("searchEdit");search->setText("Camera");tree=popup->findChild<QTreeWidget*>("componentTree");
        Check(tree->topLevelItem(0)->child(0)->isDisabled(),"Duplicate component is not disabled");popup->close();Pump();
        add->click();Pump();popup=objectUI.findChild<QDialog*>("AddComponentPopupForm");search=popup->findChild<QLineEdit*>("searchEdit");search->setText("PlayerController");tree=popup->findChild<QTreeWidget*>("componentTree");
        Check(tree->topLevelItem(0)->child(0)->isDisabled(),"Already attached native class was offered again");
        search->setText("ExtraController");tree=popup->findChild<QTreeWidget*>("componentTree");
        item=tree->topLevelItem(0)->child(0);const auto count=object->GetComponentCount();scripts.Types.clear();
        QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,tree->visualItemRect(item).center());Pump();
        Check(object->GetComponentCount()==count&&popup->isVisible(),"Stale popup attached removed script type");popup->close();Pump();
        auto forms=objectUI.findChildren<QWidget*>("ScriptComponentEditorForm");forms[1]->findChild<QToolButton*>("removeButton")->click();Pump();
        Check(first->GetScriptInstanceID()==firstID&&object->GetComponentCount()==3,"Removing second script removed another component");
        Check(scripts.Listeners.size()==1&&scripts.Observed.size()==1,"Removed script editor kept its event subscription");
        Check(project.Create,"Script create menu not registered");project.Create();Pump();
        auto* dialog=objectUI.findChild<QDialog*>("CreateScriptDialogForm");if(!dialog)for(auto* widget:QApplication::topLevelWidgets())if(widget->objectName()=="CreateScriptDialogForm")dialog=qobject_cast<QDialog*>(widget);
        Check(dialog&&dialog->isVisible(),"Create script dialog missing");dialog->findChild<QLineEdit*>("nameEdit")->setText("PlayerController");
        dialog->grab().save("/tmp/gamesprout-create-script.png");scripts.RejectCreation=true;
        auto* create=dialog->findChild<QPushButton*>("createButton");create->click();Pump();
        Check(dialog->isVisible()&&project.Notified.empty()&&!dialog->findChild<QLabel*>("errorLabel")->text().isEmpty(),"Failed creation closed dialog or notified asset browser");
        scripts.RejectCreation=false;create->click();Pump();Check(project.Notified=="Scripts/PlayerController.hpp","Created script not passed to resource browser");
        scenes.Changed={};objectUI.Clear();Check(scripts.Listeners.empty()&&scripts.Observed.empty(),"Cleared Inspector retained script listeners");
        GameSproutEditor::GameSproutFinal::Stop(feature,editor);Check(!project.Create&&!project.Open&&!project.OpenOwner&&!inspector.Extension,"Feature release retained menus, source handler or editor extension");
        context.ObjectID=scenes.Scene->GetInstanceID();objectUI.Bind(context);objectUI.Load(*scenes.Scene);Check(!add->isVisible(),"Scene root exposes Add Component");objectUI.Clear();
        delete runtime;std::cout<<"Script Inspector GUI checks passed: distinct-class edit/undo, script singleton rejection, add/search/duplicates/stale catalog, removal/lifecycle and create script dialog.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
