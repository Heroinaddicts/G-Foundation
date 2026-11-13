#include "StaticUIFixtures.h"
#include "RigidbodyComponentEditor.h"
#include "ColliderComponentEditor.h"
#include "ScriptComponentEditor.h"
#include "AnimatorComponentEditor.h"
#include "SkinnedMeshRendererComponentEditor.h"
#include "ScriptPropertyRow.h"
#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <QTest>
#include <QLineEdit>
#include <QCheckBox>
#include <QToolButton>
#include <QPushButton>
#include <QComboBox>
#include <QDialog>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QDir>
#include <iostream>

IScriptEditor* CreateFieldsFixture(GameSproutRuntime::IGameSproutRuntime*,ISceneManager*,ISelectionManager*,IObjectEditing*,const GameSproutRuntime::ScriptTypeInfo&);
void DestroyFieldsFixture(IScriptEditor*);
struct FieldsOwner{IScriptEditor* Pointer;~FieldsOwner(){if(Pointer)DestroyFieldsFixture(Pointer);}};
class Behaviour final:public GameSproutRuntime::ScriptBehaviour {
public:
    explicit Behaviour(GameSproutRuntime::IGameSproutRuntime*) {}
    float Speed=1;GameSproutRuntime::Vector3 Direction{1,2,3};GameSproutRuntime::IGameObject* Target=nullptr;
};
class Editors final:public IInspectorExtension {
public:
    ComponentEditor* CreateComponentEditor(const std::type_info& type,QWidget* parent) override {
        if(type==typeid(GameSproutRuntime::IRigidbodyComponent))return new RigidbodyComponentEditor(parent);
        if(type==typeid(GameSproutRuntime::IBoxColliderComponent))return new ColliderComponentEditor(parent);
        if(type==typeid(GameSproutRuntime::IScriptComponent))return new ScriptComponentEditor(parent);
        if(type==typeid(GameSproutRuntime::IAnimatorComponent))return new AnimatorComponentEditor(parent);
        if(type==typeid(GameSproutRuntime::ISkinnedMeshRendererComponent))return new SkinnedMeshRendererComponentEditor(parent);
        return nullptr;
    }
};
static void Edit(QLineEdit* field,const QString& text){field->setFocus();field->selectAll();QTest::keyClicks(field,text);QMetaObject::invokeMethod(field,"editingFinished",Qt::DirectConnection);}
static ScriptPropertyRow* Field(QWidget* root,const char* id){for(auto* widget:root->findChildren<QWidget*>())if(auto* row=dynamic_cast<ScriptPropertyRow*>(widget);row&&row->Definition().ID==id)return row;throw std::runtime_error("Script field row missing");}
int main(int argc,char** argv) {
    QApplication app(argc,argv);GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc>=2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Cannot load ABI-matching Runtime");
        QTemporaryDir temporary;Check(temporary.isValid(),"No isolated project");QDir(temporary.path()).mkpath("Assets");
        GameSproutRuntime::Internal::RegisterScriptClass<Behaviour>("1234567890abcdef1234567890abcdef","Behaviour","Fixture.hpp",{},
            {GameSproutRuntime::Internal::MakeField<Behaviour>("Behaviour::Speed","Speed",&Behaviour::Speed,1.f),
             GameSproutRuntime::Internal::MakeField<Behaviour>("Behaviour::Direction","Direction",&Behaviour::Direction,GameSproutRuntime::Vector3{1,2,3}),
             GameSproutRuntime::Internal::MakeField<Behaviour>("Behaviour::Target","Target",&Behaviour::Target,static_cast<GameSproutRuntime::IGameObject*>(nullptr))});
        const auto registration=GameSproutRuntime::Internal::ScriptModuleTypes().back();std::string error;Check(runtime->RegisterScriptType(registration,error),error.c_str());
        IDE ide;Project project;project.Root=temporary.path().toStdString();Assets assets;Library library;Selection selection;PlayMode play;Host host;ObjectEditing editing;
        Editor editor(runtime);SceneManager scenes;InspectorUI inspector;SceneObjectUI sceneUI;Editors editors;
        editor.Modules={{"Project",&project},{"AssetsManager",&assets},{"LibraryManager",&library},{"SelectionManager",&selection},{"PlayMode",&play},{"RuntimeHost",&host},{"SceneManager",&scenes},{"ObjectEditing",&editing}};
        editor.Views={{"IDEWindow",&ide},{"InspectorUI",&inspector},{"SceneObjectUI",&sceneUI}};
        GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Start(scenes,api),"SceneManager failed");Check(GameSproutEditor::GameSproutEditorFinal::Start(editing,api),"Actual ObjectEditing failed");
        IScriptEditor* scripts=CreateFieldsFixture(runtime,&scenes,&selection,&editing,registration.Info);FieldsOwner fieldsOwner{scripts};editor.Modules["ScriptEditor"]=scripts;
        auto* author=scenes.GetActiveScene()->CreateGameObject("Author");auto* other=scenes.GetActiveScene()->CreateGameObject("Other");
        author->AddComponent<GameSproutRuntime::IBoxColliderComponent>();auto* body=author->AddComponent<GameSproutRuntime::IRigidbodyComponent>();auto settings=body->GetSettings();settings.GravityFactor=0;Check(body->SetSettings(settings),"Body fixture invalid");
        auto* meshRenderer=author->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
        author->AddComponent<GameSproutRuntime::ICameraComponent>();author->AddComponent<GameSproutRuntime::IAnimatorComponent>();
        other->AddComponent<GameSproutRuntime::IMeshComponent>();
        other->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
        auto* skin=other->AddComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();Check(meshRenderer&&skin,"Shadow editor fixtures unavailable");
        Check(runtime->AddScriptComponent(author,registration.Info.TypeID,{registration.Info.TypeID,1},1,error),error.c_str());
        Check(scenes.SaveSceneAs("Inspector.scene",error),error.c_str());const auto authorFile=scenes.GetObjectFileID(author->GetInstanceID());
        Check(GameSproutEditor::GameSproutEditorFinal::Start(inspector,editor),"Inspector failed");Check(GameSproutEditor::GameSproutEditorFinal::Start(sceneUI,editor),"SceneObjectUI failed");
        Check(inspector.RegisterExtension(&editors),"Component editors did not register");GameSproutEditor::GameSproutEditorFinal::Finished(inspector,editor);
        selection.SetSceneObjectSelection({scenes.GetActiveScene()->GetInstanceID(),author->GetInstanceID()});Pump(*runtime);inspector.Show();auto* root=ide.Inspector.Widget;root->resize(440,900);Pump(*runtime);
        using GameSproutRuntime::ShadowCastingMode;
        const auto activateCasting=[](QWidget* panel,int index) {
            auto* combo=Find<QComboBox>(panel,"shadowCastingModeCombo");
            combo->setCurrentIndex(index);QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,index));
        };
        activateCasting(Find<QWidget>(root,"rendererShadows"),2);Pump(*runtime);
        Check(meshRenderer->GetShadowCastingMode()==ShadowCastingMode::TwoSided,"MeshRenderer Inspector did not update cast mode");
        Check(scenes.Undo(error)&&meshRenderer->GetShadowCastingMode()==ShadowCastingMode::On,error.c_str());
        selection.SetSceneObjectSelection({scenes.GetActiveScene()->GetInstanceID(),other->GetInstanceID()});Pump(*runtime);
        auto* skinnedPanel=Find<QWidget>(root,"skinnedRendererShadows");
        Check(Find<QWidget>(root,"rendererShadows")->isHidden()&&!skinnedPanel->isHidden(),"Skinned object shows duplicate shadow controls");
        activateCasting(skinnedPanel,3);Pump(*runtime);
        Find<QCheckBox>(skinnedPanel,"receiveShadowsCheck")->click();Pump(*runtime);
        Check(skin->GetShadowCastingMode()==ShadowCastingMode::ShadowsOnly&&!skin->GetReceiveShadows(),"Skinned Inspector did not update shadow settings");
        Check(scenes.Undo(error)&&skin->GetReceiveShadows(),error.c_str());Pump(*runtime);
        Check(Find<QCheckBox>(skinnedPanel,"receiveShadowsCheck")->isChecked(),"MeshRenderer change event did not refresh the Skinned Inspector");
        selection.SetSceneObjectSelection({scenes.GetActiveScene()->GetInstanceID(),author->GetInstanceID()});Pump(*runtime);
        play.Emit(EditorPlayState::EnteringPlay);Check(Find<QLineEdit>(root,"objectNameEdit")->isReadOnly(),"EnteringPlay permits writes");
        Check(scenes.BeginPlaySession(error),error.c_str());Check(runtime->BeginExecution(error),error.c_str());play.Emit(EditorPlayState::Playing);
        auto* world=scenes.GetActiveScene();auto* object=scenes.QueryObjectByFileID(authorFile);Check(object,"Play clone missing");
        selection.SetSceneObjectSelection({world->GetInstanceID(),object->GetInstanceID()});Pump(*runtime);
        Check(!Find<QLineEdit>(root,"objectNameEdit")->isReadOnly()&&!Find<QPushButton>(root,"addComponentButton")->isEnabled(),"Property and structure permissions were not split");
        Check(!Find<QToolButton>(root,"removeButton")->isEnabled(),"Play script remove enabled");
        Check(!Find<QWidget>(root,"previewGroup")->isEnabled(),"Edit animation preview can drive Play world");
        Find<QCheckBox>(Find<QWidget>(root,"rendererShadows"),"receiveShadowsCheck")->click();Pump(*runtime);
        Check(!object->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->GetReceiveShadows(),"Play Inspector did not update receive shadows");
        Edit(Find<QLineEdit>(root,"objectNameEdit"),"Live");Pump(*runtime);Check(object->GetName()=="Live", "Play name edit failed");
        auto* position=Find<QLineEdit>(root,"transformPositionXEdit");position->setFocus();position->selectAll();QTest::keyClicks(position,"12");
        object->GetTransform()->SetLocalPosition({0,37,0});Pump(*runtime);
        Check(position->text()=="12","Runtime event overwrote focused Transform draft");QMetaObject::invokeMethod(position,"editingFinished",Qt::DirectConnection);Pump(*runtime);
        Check(object->GetTransform()->GetLocalPosition().X==12&&object->GetTransform()->GetLocalPosition().Y==37,"Transform draft overwrote another live axis");
        auto* mass=Find<QLineEdit>(root,"massEdit");mass->setFocus();mass->selectAll();QTest::keyClicks(mass,"3");
        auto* liveBody=object->GetComponent<GameSproutRuntime::IRigidbodyComponent>();auto liveSettings=liveBody->GetSettings();liveSettings.GravityFactor=.25f;Check(liveBody->SetSettings(liveSettings),"Cannot mutate live gravity");Pump(*runtime);
        Check(mass->text()=="3","Live update overwrote mass input");QMetaObject::invokeMethod(mass,"editingFinished",Qt::DirectConnection);Pump(*runtime);
        Check(liveBody->GetSettings().Mass==3&&liveBody->GetSettings().GravityFactor==.25f,"Mass commit clobbered live gravity");
        auto* native=object->GetComponent<Behaviour>();Check(native,"Native script not initialized");
        QPointer<QWidget> speedRow=Field(root,"Behaviour::Speed");auto* speed=Find<QLineEdit>(speedRow,"scalarEdit");speed->setFocus();speed->selectAll();QTest::keyClicks(speed,"7");native->Speed=8;native->Direction.Y=99;runtime->Update(0);Pump(*runtime);
        Check(speedRow&&speed->text()=="7","Native event rebuilt field row or overwrote input");QMetaObject::invokeMethod(speed,"editingFinished",Qt::DirectConnection);Pump(*runtime);Check(native->Speed==7,"Play field edit did not reach native member");
        auto* direction=Field(root,"Behaviour::Direction");Edit(Find<QLineEdit>(direction,"xEdit"),"6");Pump(*runtime);Check(native->Direction.X==6&&native->Direction.Y==99,"Script vector commit clobbered live axis");
        play.Emit(EditorPlayState::Paused);Edit(Find<QLineEdit>(root,"gravityEdit"),"0.5");Pump(*runtime);Check(liveBody->GetSettings().GravityFactor==.5f,"Paused properties remain read-only");
        auto* lock=Find<QToolButton>(root,"objectLockButton");lock->click();Pump(*runtime);auto* temporaryObject=world->CreateGameObject("Temporary");temporaryObject->AddComponent<GameSproutRuntime::ICameraComponent>();
        selection.SetSceneObjectSelection({world->GetInstanceID(),temporaryObject->GetInstanceID()});Pump(*runtime);Check(Find<QLineEdit>(root,"objectNameEdit")->text()=="Live","Locked Inspector followed new selection");
        auto* targetRow=Field(root,"Behaviour::Target");Find<QToolButton>(targetRow,"pickObjectButton")->click();Pump(*runtime);
        auto* picker=Find<QDialog>(targetRow,"SceneObjectPickerForm");auto* tree=Find<QTreeWidget>(picker,"objectTree");QTreeWidgetItem* choice=nullptr;
        for(int index=0;index<tree->topLevelItemCount();++index)if(tree->topLevelItem(index)->text(0).contains("Temporary"))choice=tree->topLevelItem(index);
        Check(choice,"Transient object missing from picker");tree->setCurrentItem(choice);Find<QPushButton>(picker,"selectButton")->click();Pump(*runtime);Check(native->Target==temporaryObject,"Runtime object reference was not restored");
        lock->click();selection.SetSceneObjectSelection({world->GetInstanceID(),temporaryObject->GetInstanceID()});Pump(*runtime);Edit(Find<QLineEdit>(root,"objectNameEdit"),"Temporary edited");Pump(*runtime);Check(temporaryObject->GetName()=="Temporary edited","Runtime-only object cannot edit properties");
        Find<QPushButton>(root,"setMainCameraButton")->click();Pump(*runtime);Check(world->GetMainCamera()==temporaryObject->GetComponent<GameSproutRuntime::ICameraComponent>(),"Runtime-only camera required author FileID");
        play.Emit(EditorPlayState::ExitingPlay);Check(Find<QLineEdit>(root,"objectNameEdit")->isReadOnly(),"ExitingPlay permits writes");Check(runtime->EndExecution(error),error.c_str());Check(scenes.EndPlaySession(error),error.c_str());play.Emit(EditorPlayState::Edit);
        auto* restored=scenes.QueryObjectByFileID(authorFile);Check(restored&&restored->GetName()=="Author"&&restored->GetTransform()->GetLocalPosition().X==0&&restored->GetComponent<GameSproutRuntime::IRigidbodyComponent>()->GetSettings().Mass==1,"Stop did not restore author snapshot");
        Check(restored->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->GetReceiveShadows(),"Play shadow change leaked into edit scene");
        selection.SetSceneObjectSelection({scenes.GetActiveScene()->GetInstanceID(),restored->GetInstanceID()});Pump(*runtime);Check(!Find<QLineEdit>(root,"objectNameEdit")->isReadOnly()&&Find<QPushButton>(root,"addComponentButton")->isEnabled(),"Stop did not restore authoring permissions");
        inspector.UnregisterExtension(&editors);GameSproutEditor::GameSproutEditorFinal::Stop(sceneUI,editor);GameSproutEditor::GameSproutEditorFinal::Stop(inspector,editor);
        DestroyFieldsFixture(scripts);fieldsOwner.Pointer=nullptr;editor.Modules.erase("ScriptEditor");GameSproutEditor::GameSproutEditorFinal::Stop(editing,api);GameSproutEditor::GameSproutEditorFinal::Stop(scenes,api);
        delete runtime;runtime=nullptr;Check(play.Listeners.empty()&&assets.Listeners.empty()&&selection.Listeners.empty(),"Released Inspector retained subscriptions");
        std::cout<<"PASS real Play/Paused Inspector edits, current-target events, native fields, input preservation, field patches, lock, transient object references and stop restoration\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';delete runtime;return 1;}
}
