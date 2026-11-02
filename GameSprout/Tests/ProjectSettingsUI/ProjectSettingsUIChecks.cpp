#include "ProjectSettingsUI.h"
#include "ProjectSettingsModels.h"
#include "GameObjectInspector.h"
#include "LayerMaskCombo.h"
#include "CameraComponentEditor.h"
#include <QAbstractItemView>
#include <QTest>
#include "IIDEWindow.h"
#include "ISceneManager.h"
#include "SceneManager.h"
#include "IProject.h"
#include <QTemporaryDir>
#include <QDir>
#include "GameSproutRuntimeExport.h"
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTableView>
#include <QLabel>
#include <QTimer>
#include <QTabWidget>
#include <iostream>
#include <map>
#include <stdexcept>

namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static bool Start(IGameSproutView& view,GameSproutViewApi& api){view.BeforeInitialize(&api);return view.Initialize(&api)&&view.Launch(&api);}
    static void Tick(IGameSproutView& view,GameSproutViewApi& api){view.Update(&api);}
    static void Stop(IGameSproutView& view,GameSproutViewApi& api){view.Release(&api);}
    static bool StartModule(IGameSproutModule& module,GameSproutModuleApi& api){module.BeforeInitialize(&api);return module.Initialize(&api)&&module.Launch(&api);}
    static void StopModule(IGameSproutModule& module,GameSproutModuleApi& api){module.Release(&api);}
};
}
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
class Menu final:public IIDEMenu {
public:
    std::map<QString,Menu*> Children;fMenuClickCallback Callback=nullptr;bool Enabled=true;
    ~Menu() override{for(auto& entry:Children)delete entry.second;}
    IIDEMenu* CreateChildMenu(const QString& name) override{auto& item=Children[name];if(!item)item=new Menu;return item;}
    void SetClickCallback(fMenuClickCallback callback) override{Callback=callback;}
    void SetEnabled(bool value) override{Enabled=value;}
};
class MenuBar final:public IIDEMenuBar {
public:
    Menu Root;IIDEMenu* CreateChildMenu(const QString& name) override{return Root.CreateChildMenu(name);}
};
class IDE final:public IIDEWindow {
public:
    mutable MenuBar Bar;IIDEMenuBar* GetMenuBar() const override{return &Bar;}
    IIDEDocker* CreateDocker(const QString&) override{return nullptr;}IIDEDocker* QueryDocker(const QString&) override{return nullptr;}
    bool ResizeDockers(const QList<IIDEDocker*>&,const QList<int>&,Qt::Orientation) override{return false;}
    void Loading(const std::string&,const std::string&) override{}void Unloading() override{}void SetLoadingInfo(const std::string&,const std::string&) override{}
private:
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override{return true;}bool Launch(GameSproutEditor::GameSproutViewApi*) override{return true;}void Release(GameSproutEditor::GameSproutViewApi*) override{}
};
class Settings final:public IProjectSettings {
public:
    GameSproutRuntime::RuntimeProjectSettings Data;GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    mutable int Reads=0;int Saves=0;std::uint64_t Revision=1;std::vector<IProjectSettingsListener*> Listeners;
    GameSproutRuntime::RuntimeProjectSettings GetSettings() const override{++Reads;return Data;}
    std::uint64_t GetRevision() const override{return Revision;}
    bool SaveSettings(const GameSproutRuntime::RuntimeProjectSettings& data,std::uint64_t expected,std::string& error) override{
        if(expected!=Revision){error="Stale revision";return false;}
        if(!GameSproutRuntime::ValidateProjectSettings(data,error)||!Runtime->SetProjectSettings(data,error))return false;
        Data=data;++Revision;++Saves;Emit();return true;
    }
    void Emit(){const auto listeners=Listeners;for(auto* listener:listeners)listener->OnProjectSettingsChanged(Data,Revision);}
    bool RegisterSettingsListener(IProjectSettingsListener* listener) override{Listeners.push_back(listener);return true;}
    void UnregisterSettingsListener(IProjectSettingsListener* listener) override{std::erase(Listeners,listener);}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class Project final:public IProject {
public:
    std::string Root;
    const std::string& GetProjectRoot() const override{return Root;}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class Editor final:public GameSproutEditor::GameSproutViewApi {
public:
    explicit Editor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    Settings* Source=nullptr;IDE* Window=nullptr;Project* ProjectSource=nullptr;
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name) const override{return name=="IDEWindow"?Window:nullptr;}
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override{if(name=="Project")return ProjectSource;return name=="ProjectSettings"?Source:nullptr;}
};
static QDialog* SettingsWindow(){for(QWidget* widget:QApplication::topLevelWidgets())if(widget->objectName()=="ProjectSettingsForm")return qobject_cast<QDialog*>(widget);return nullptr;}
int main(int argc,char** argv){
    QApplication app(argc,argv);GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc>=2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        IDE ide;Settings source;source.Runtime=runtime;Editor editor(runtime);editor.Source=&source;editor.Window=&ide;ProjectSettingsUI view;
        Check(GameSproutEditor::GameSproutEditorFinal::Start(view,editor),"Project Settings view did not initialize");
        auto* menu=ide.Bar.Root.Children.at("Edit")->Children.at(QStringLiteral("项目设置…"));Check(menu->Callback,"Project Settings menu missing");menu->Callback();
        auto* dialog=SettingsWindow();Check(dialog&&dialog->isVisible(),"Project Settings dialog missing");
        auto* tags=dialog->findChild<QTableView*>("tagsTable");auto* layers=dialog->findChild<QTableView*>("layersTable");auto* matrix=dialog->findChild<QTableView*>("collisionTable");
        auto* apply=dialog->findChild<QPushButton*>("applyButton");auto* input=dialog->findChild<QLineEdit*>("addTagEdit");
        Check(tags&&layers&&matrix&&input&&apply,"Designer controls missing");
        Check(layers->model()->rowCount()==32&&!(layers->model()->flags(layers->model()->index(0,1))&Qt::ItemIsEditable),"Layer slots or built-in protection wrong");
        input->setText("Enemy");dialog->findChild<QPushButton*>("addTagButton")->click();Check(apply->isEnabled(),"Tag add did not produce a draft");apply->click();
        Check(source.Data.Tags.size()==2&&source.Data.Tags[1].ID==1&&source.Data.NextTagID==2,"Tag stable identity not assigned");
        Check(tags->model()->setData(tags->model()->index(1,0),"NPC"),"Tag rename blocked");apply->click();Check(source.Data.Tags[1].ID==1&&source.Data.Tags[1].Name=="NPC","Tag rename replaced identity");
        Check(layers->model()->setData(layers->model()->index(3,1),"Characters"),"User Layer not editable");apply->click();Check(source.Data.LayerNames[3]=="Characters"&&matrix->model()->rowCount()==4,"Layer name or matrix event refresh failed");
        Check(matrix->model()->setData(matrix->model()->index(0,3),Qt::Unchecked,Qt::CheckStateRole),"Matrix edit rejected");apply->click();
        Check(!(source.Data.CollisionMatrix[0]&(1u<<3))&&!(source.Data.CollisionMatrix[3]&1u),"Collision matrix was not symmetric");
        dialog->findChild<QPushButton*>("disableAllButton")->click();apply->click();for(auto row:source.Data.CollisionMatrix)Check(row==0,"Disable All missed unnamed Layer slots");
        dialog->findChild<QPushButton*>("enableAllButton")->click();apply->click();for(auto row:source.Data.CollisionMatrix)Check(row==0xffffffffu,"Enable All missed unnamed Layer slots");
        view.OnPlayModeStateChanged({EditorPlayState::Playing,{}});Check(!dialog->findChild<QPushButton*>("addTagButton")->isEnabled()&&!(tags->model()->flags(tags->model()->index(1,0))&Qt::ItemIsEditable),"Playing still permits project writes");view.OnPlayModeStateChanged({});
        Check(layers->model()->setData(layers->model()->index(4,1),"Draft"),"Cannot prepare conflicting draft");source.Data.LayerNames[5]="External";++source.Revision;source.Emit();
        Check(layers->model()->data(layers->model()->index(4,1)).toString()=="Draft","External settings discarded the local draft");apply->click();Check(dialog->findChild<QLabel*>("errorLabel")->text()=="Stale revision","Stale draft did not reach conflict check");dialog->findChild<QPushButton*>("revertButton")->click();Check(layers->model()->data(layers->model()->index(5,1)).toString()=="External","Revert did not read latest snapshot");
        const auto reads=source.Reads;for(int i=0;i<100;++i)GameSproutEditor::GameSproutEditorFinal::Tick(view,editor);QCoreApplication::processEvents();Check(reads==source.Reads,"Idle View polled settings");
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create isolated UI scene project");QDir(temporary.path()).mkpath("Assets");
        Project project;project.Root=temporary.path().toStdString();editor.ProjectSource=&project;
        SceneManager scenes;GameSproutEditor::GameSproutModuleApi moduleApi(nullptr,&editor);
        Check(GameSproutEditor::GameSproutEditorFinal::StartModule(scenes,moduleApi),"Cannot initialize production SceneManager");
        auto* scene=scenes.GetActiveScene();auto* object=scene->CreateGameObject("Object");
        ComponentEditContext context;context.Runtime=runtime;context.Scenes=&scenes;context.SceneID=scene->GetInstanceID();context.ObjectID=object->GetInstanceID();
        GameObjectInspector inspector(nullptr);inspector.Bind(context);inspector.SetProjectSettings(source.Data);inspector.Load(*object);
        auto* tag=inspector.findChild<QComboBox*>("tagCombo");auto* layer=inspector.findChild<QComboBox*>("layerCombo");Check(tag&&layer,"Inspector classification controls missing");
        const auto choose=[](QComboBox* combo,int index){combo->setCurrentIndex(index);QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,index));};
        choose(tag,tag->findData(QVariant::fromValue<qulonglong>(1)));Check(object->GetTagID()==1&&object->GetTag()=="NPC","Inspector Tag edit failed");std::string error;Check(scenes.Undo(error)&&object->GetTagID()==0,"Tag edit does not support Undo");
        choose(layer,layer->findData(3));Check(object->GetLayer()==3,"Inspector Layer edit failed");Check(scenes.Undo(error)&&object->GetLayer()==0,"Layer edit does not support Undo");
        auto* child=scene->CreateGameObject("Child",object);Check(child,"Cannot create Layer prompt fixture");
        const auto answer=[&](const char* name){QTimer::singleShot(0,[&,name]{auto* prompt=inspector.findChild<QDialog*>("LayerChildrenDialogForm");Check(prompt,"Layer child choice did not use Designer dialog");auto* button=prompt->findChild<QPushButton*>(name);Check(button,"Layer child choice missing");button->click();});};
        answer("selfButton");choose(layer,layer->findData(3));Check(object->GetLayer()==3&&child->GetLayer()==0,"Self-only Layer change inherited into children");
        answer("cancelButton");choose(layer,layer->findData(0));Check(object->GetLayer()==3&&child->GetLayer()==0,"Cancelling Layer prompt changed nodes");
        Check(child->SetLayer(2),"Cannot prepare descendant Layer");
        answer("childrenButton");choose(layer,layer->findData(0));Check(object->GetLayer()==0&&child->GetLayer()==0,"Apply-to-children did not update the full subtree");
        Check(scenes.Undo(error)&&object->GetLayer()==3&&child->GetLayer()==2,"Subtree Layer edit was not one Undo transaction");
        Check(scenes.Redo(error)&&object->GetLayer()==0&&child->GetLayer()==0,"Subtree Layer redo failed");
        inspector.SetReadOnly(true);Check(!tag->isEnabled()&&!layer->isEnabled(),"Playing Inspector permits classification edits");
        inspector.resize(360,500);inspector.show();QCoreApplication::processEvents();Check(tag->width()>40&&layer->width()>40,"Narrow Inspector clips classification fields");
        if(argc==3){
            inspector.SetReadOnly(false);const QString path=QString::fromLocal8Bit(argv[2]);
            auto* tabs=dialog->findChild<QTabWidget*>("tabs");
            for(int index=0;index<3;++index){tabs->setCurrentIndex(index);QCoreApplication::processEvents();Check(dialog->grab().save(path+"/project-settings-"+QString::number(index)+".png"),"Cannot save Settings visual");}
            Check(inspector.grab().save(path+"/classification-inspector.png"),"Cannot save Inspector visual");
        }
        {
            LayerMaskCombo mask;mask.SetLayers(source.Data.LayerNames);mask.SetMask(1u<<31);mask.resize(220,28);mask.show();
            std::uint32_t changed=0;mask.MaskChanged=[&](std::uint32_t value){changed=value;};mask.showPopup();QCoreApplication::processEvents();
            mask.view()->setCurrentIndex(mask.model()->index(2,0));QTest::keyClick(mask.view(),Qt::Key_Space);
            Check(mask.GetMask()==((1u<<31)|1u)&&changed==mask.GetMask(),"Toggling named Layer destroyed unnamed mask bits");
            mask.hidePopup();
            auto* camera=object->AddComponent<GameSproutRuntime::ICameraComponent>();CameraComponentEditor cameraEditor(nullptr);cameraEditor.Bind(context);cameraEditor.Load(*camera);
            auto* cameraMask=cameraEditor.findChild<QWidget*>("cameraMaskCombo");Check(cameraMask,"Camera lacks named Layer picker");
        }
        GameSproutEditor::GameSproutEditorFinal::Stop(view,editor);Check(source.Listeners.empty()&&!menu->Callback&&!menu->Enabled,"Released View retained subscription or menu callback");
        GameSproutEditor::GameSproutEditorFinal::StopModule(scenes,moduleApi);
        delete runtime;std::cout<<"PASS project settings models, stable Tag rename, fixed Layers, matrix symmetry, draft conflicts, event-only refresh and Inspector Undo\n";return 0;
    }catch(const std::exception& error){delete runtime;std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
