#include "../../View/InspectorUI/AnimatorControllerDialog.h"
#include "IRuntimeAssets.h"
#include "HumanoidImportSettings.h"
#include "../../Module/Shared/AnimationDefinition.h"
#include <QApplication>
#include <QPushButton>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include "../../View/InspectorUI/ResourceField.h"
#include <QLabel>
#include <QTimer>
#include <stdexcept>
#include <iostream>

namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
class Assets final:public IRuntimeAssets {
public:
    GameSproutRuntime::AnimatorControllerData Data,Saved;std::string Snapshot="initial";int Saves=0;
    bool LoadModelRigSource(const GameSproutRuntime::ResourceReference&,ModelRigImportSettings&,GameSproutRuntime::SkeletonData&,std::string&,std::string&)const override{return false;}
    bool SaveModelRigSource(const GameSproutRuntime::ResourceReference&,const ModelRigImportSettings&,const std::string&,std::string&)override{return false;}
    bool SuggestHumanoidMapping(const GameSproutRuntime::SkeletonData&,std::vector<GameSproutRuntime::HumanBoneMapping>&,std::string&)const override{return false;}
    GameSproutRuntime::IRuntimeResourceProvider* GetResourceProvider()const override{return nullptr;}
    bool CreateAvatarMaskSource(const std::string&,const std::string&,const GameSproutRuntime::ResourceReference&,std::string&,std::string&)override{return false;}
    bool LoadAvatarMaskSource(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AvatarMaskData&,std::string&,std::string&)const override{return false;}
    bool SaveAvatarMaskSource(const GameSproutRuntime::ResourceReference&,const GameSproutRuntime::AvatarMaskData&,const std::string&,std::string&)override{return false;}
    bool CreateAnimatorControllerAsset(const std::string&,const std::string&,const GameSproutRuntime::ResourceReference&,std::string&,std::string&)override{return false;}
    bool LoadAnimatorControllerSource(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimatorControllerData& data,std::string& json,std::string&)const override{data=Data;json=Snapshot;return true;}
    bool SaveAnimatorControllerSource(const GameSproutRuntime::ResourceReference&,const GameSproutRuntime::AnimatorControllerData& data,const std::string& expected,std::string& error)override{
        if(expected!=Snapshot){error="external conflict";return false;}
        if(!AnimationDefinition::ReadController(AnimationDefinition::ControllerJson(data,false),false,Saved,error))return false;
        Data=Saved;++Saves;return true;
    }
protected:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
template<class T>T* Find(QWidget& dialog,const char* name){auto* value=dialog.findChild<T*>(name);Check(value,"Missing generated .ui control");return value;}

void CheckSynchronizedLayer(){
    Assets assets;const GameSproutRuntime::ResourceReference clip{"00000001000000020000000300000004",5};
    GameSproutRuntime::AnimatorState state;state.Name="Idle";state.Clip=clip;
    GameSproutRuntime::AnimatorLayer base;base.Name="Base";base.States={state};assets.Data.Layers={base};
    base.Name="Source";assets.Data.Layers.push_back(base);base.Name="Follower";assets.Data.Layers.push_back(base);
    AnimatorControllerDialog dialog(&assets,nullptr,nullptr,nullptr,{clip.Guid,6});
    auto* layers=Find<QComboBox>(dialog,"layerCombo");layers->setCurrentIndex(2);auto* source=Find<QComboBox>(dialog,"syncSourceCombo");
    source->setCurrentIndex(source->findData(1));
    Check(!Find<QPushButton>(dialog,"addStateButton")->isEnabled(),"Synchronized topology remained editable");
    source->setCurrentIndex(source->findData(-1));Check(Find<QTableWidget>(dialog,"statesTable")->rowCount()==1,"Turning sync off lost independent graph draft");
    source->setCurrentIndex(source->findData(1));Find<QCheckBox>(dialog,"syncTimingCheck")->setChecked(true);Find<QCheckBox>(dialog,"ikPassCheck")->setChecked(true);
    auto* sync=dialog.findChild<QWidget*>("syncEditor");Check(sync,"Missing .ui sync override editor");
    Find<QCheckBox>(*sync,"syncOverrideCheck")->setChecked(true);auto* overrideClip=Find<ResourceField>(*sync,"syncClipField");overrideClip->SetValue(clip);overrideClip->Changed(clip);
    Find<QPushButton>(dialog,"saveButton")->click();
    Check(assets.Saves==1&&assets.Saved.Layers[2].SyncedLayerIndex==1&&assets.Saved.Layers[2].IKPass&&assets.Saved.Layers[2].SyncedLayerAffectsTiming&&assets.Saved.Layers[2].States.empty()&&assets.Saved.Layers[2].MotionOverrides.size()==1,"Sync source/IK/timing/override did not save through Module");
    Find<QPushButton>(dialog,"moveLayerUpButton")->click();Find<QPushButton>(dialog,"saveButton")->click();
    Check(assets.Saves==2&&assets.Saved.Layers[1].Name=="Follower"&&assets.Saved.Layers[1].SyncedLayerIndex==2,"Layer reorder changed synchronization identity");
    layers->setCurrentIndex(2);Find<QTableWidget>(dialog,"statesTable")->item(0,0)->setText("Standing");Find<QPushButton>(dialog,"saveButton")->click();
    Check(assets.Saves==3&&assets.Saved.Layers[1].MotionOverrides[0].State=="Standing","Source state rename broke synchronized override");
    Find<QPushButton>(dialog,"removeLayerButton")->click();Check(layers->count()==3,"Source layer was deleted while a synchronized layer still referenced it");
}

void CheckRenameIsolation(){
    Assets assets;assets.Data.Parameters={{"A",GameSproutRuntime::AnimatorParameterType::Float,0},{"B",GameSproutRuntime::AnimatorParameterType::Float,0}};
    GameSproutRuntime::AnimatorLayer base;base.Name="Base Layer";
    for(int index=0;index<2;++index){GameSproutRuntime::AnimatorState state;state.Name=index?"Walk":"Idle";state.Clip={"00000001000000020000000300000004",5};
        state.BlendParameter=index?"B":"A";state.Motions={{state.Clip,0,1},{state.Clip,1,1}};base.States.push_back(state);}
    GameSproutRuntime::AnimatorTransition transition;transition.From=0;transition.To=1;
    transition.Conditions={{"A",GameSproutRuntime::AnimatorCompare::Greater,0},{"B",GameSproutRuntime::AnimatorCompare::Greater,0}};base.Transitions.push_back(transition);
    assets.Data.Layers.push_back(base);base.Name="Upper";assets.Data.Layers.push_back(base);
    AnimatorControllerDialog dialog(&assets,nullptr,nullptr,nullptr,{"00000001000000020000000300000004",6});
    auto* parameters=Find<QTableWidget>(dialog,"parametersTable");auto* states=Find<QTableWidget>(dialog,"statesTable");
    auto* conditions=Find<QTableWidget>(dialog,"conditionsTable");auto* transitions=Find<QTableWidget>(dialog,"transitionsTable");
    parameters->item(0,0)->setText("B");Check(parameters->item(0,0)->text()=="A"&&conditions->item(0,0)->text()=="A"&&conditions->item(1,0)->text()=="B","Duplicate parameter renamed references before rejection");
    parameters->item(0,0)->setText(" ");Check(parameters->item(0,0)->text()=="A","Empty parameter name was accepted");parameters->item(0,0)->setText("C");
    states->item(0,0)->setText("Walk");Check(states->item(0,0)->text()=="Idle"&&transitions->item(0,0)->text()=="Idle"&&transitions->item(0,1)->text()=="Walk","Duplicate state renamed transition targets");
    states->item(0,0)->setText("");Check(states->item(0,0)->text()=="Idle","Empty state name was accepted");
    states->item(0,0)->setText("AnyState");Check(states->item(0,0)->text()=="Idle","Reserved state name was accepted");states->item(0,0)->setText("Standing");
    Find<QPushButton>(dialog,"saveButton")->click();Check(assets.Saves==1,"Valid name corrections could not save");
    for(const auto& layer:assets.Saved.Layers)Check(layer.States[0].BlendParameter=="C"&&layer.States[1].BlendParameter=="B"&&layer.Transitions[0].Conditions[0].Parameter=="C"&&layer.Transitions[0].Conditions[1].Parameter=="B","Rejecting and correcting a parameter name corrupted other-layer references");
    Check(assets.Saved.Layers[0].States[0].Name=="Standing"&&assets.Saved.Layers[0].States[1].Name=="Walk"&&assets.Saved.Layers[0].Transitions[0].From==0&&assets.Saved.Layers[0].Transitions[0].To==1,"State correction corrupted destination identity");
    parameters->item(0,0)->setText("Parameter 1");parameters->item(1,0)->setText("Parameter 3");
    Find<QPushButton>(dialog,"addParameterButton")->click();Check(parameters->rowCount()==3&&parameters->item(2,0)->text()=="Parameter 2","New parameter name collided after deletion or custom rename");
    states->item(0,0)->setText("State 1");states->item(1,0)->setText("State 3");
    Find<QPushButton>(dialog,"addStateButton")->click();Check(states->rowCount()==3&&states->item(2,0)->text()=="State 2","New state name collided after deletion or custom rename");
}
}
int main(int argc,char** argv){QApplication application(argc,argv);try{
    CheckRenameIsolation();CheckSynchronizedLayer();
    Assets assets;GameSproutRuntime::AnimatorLayer base;base.Name="Base Layer";assets.Data.Layers.push_back(base);GameSproutRuntime::AnimatorState idle;idle.Name="Idle";idle.Clip={"00000001000000020000000300000004",5};assets.Data.Layers[0].States.push_back(idle);
    auto walk=idle;walk.Name="Walk";assets.Data.Layers[0].States.push_back(walk);assets.Data.Parameters.push_back({"Speed",GameSproutRuntime::AnimatorParameterType::Float,0});
    GameSproutRuntime::AnimatorTransition transition;transition.From=0;transition.To=1;transition.Conditions.push_back({"Speed",GameSproutRuntime::AnimatorCompare::Greater,0.1});assets.Data.Layers[0].Transitions.push_back(transition);
    AnimatorControllerDialog dialog(&assets,nullptr,nullptr,nullptr,{"00000001000000020000000300000004",6});
    auto* states=Find<QTableWidget>(dialog,"statesTable");auto* transitions=Find<QTableWidget>(dialog,"transitionsTable");auto* parameters=Find<QTableWidget>(dialog,"parametersTable");
    Check(states->rowCount()==2&&parameters->rowCount()==1&&transitions->rowCount()==1,"Controller rows not loaded");
    states->item(0,0)->setText("Standing");Check(transitions->item(0,0)->text()=="Standing","Renaming state broke transition relation");
    parameters->item(0,2)->setText("0.5");transitions->item(0,2)->setText("0.35");
    Find<QPushButton>(dialog,"saveButton")->click();Check(assets.Saves==1&&assets.Saved.Layers[0].States[0].Name=="Standing"&&assets.Saved.Parameters[0].DefaultValue==0.5&&std::fabs(assets.Saved.Layers[0].Transitions[0].Duration-0.35f)<0.0001f,"FSM edits not saved through Module service");
    states->setCurrentCell(1,0);Find<QLineEdit>(dialog,"blendParameterEdit")->setText("Speed");
    Find<QPushButton>(dialog,"addMotionButton")->click();Find<QPushButton>(dialog,"addMotionButton")->click();
    Find<QPushButton>(dialog,"saveButton")->click();Check(assets.Saves==2&&assets.Saved.Layers[0].States[1].Motions.size()==2,"1D blend edits not preserved");
    parameters->item(0,0)->setText("Velocity");
    Find<QPushButton>(dialog,"saveButton")->click();Check(assets.Saves==3&&assets.Saved.Layers[0].States[1].BlendParameter=="Velocity"&&assets.Saved.Layers[0].Transitions[0].Conditions[0].Parameter=="Velocity","Parameter rename broke binding references");
    auto* layers=Find<QComboBox>(dialog,"layerCombo");auto* mode=Find<QComboBox>(dialog,"layerModeCombo");
    Check(layers->count()==1&&!mode->isEnabled()&&!Find<QPushButton>(dialog,"removeLayerButton")->isEnabled(),"Base layer controls were not fixed");
    Find<QPushButton>(dialog,"addLayerButton")->click();Check(layers->count()==2&&layers->currentIndex()==1,"Layer creation failed");
    Find<QLineEdit>(dialog,"layerNameEdit")->setText("Upper Body");Find<QDoubleSpinBox>(dialog,"layerWeightSpin")->setValue(.6);
    states->item(0,0)->setText("Aim");auto* clip=Find<ResourceField>(dialog,"clipField");clip->SetValue(idle.Clip);clip->Changed(idle.Clip);
    mode->setCurrentIndex(1);Find<ResourceField>(dialog,"referenceClipField")->SetValue(idle.Clip);Find<QDoubleSpinBox>(dialog,"referenceTimeSpin")->setValue(.5);
    layers->setCurrentIndex(0);Check(states->rowCount()==2&&states->item(0,0)->text()=="Standing","Switching layers lost base state draft");
    parameters->item(0,0)->setText("MoveSpeed");layers->setCurrentIndex(1);
    Check(states->item(0,0)->text()=="Aim"&&Find<QLineEdit>(dialog,"layerNameEdit")->text()=="Upper Body"&&mode->currentIndex()==1,"Switching layers lost upper draft");
    Find<QPushButton>(dialog,"saveButton")->click();Check(assets.Saves==4&&assets.Saved.Layers.size()==2&&assets.Saved.Layers[1].Name=="Upper Body"&&std::fabs(assets.Saved.Layers[1].DefaultWeight-.6f)<.001f&&assets.Saved.Layers[1].ReferenceClip==idle.Clip,"Layer settings not saved through Module");
    Check(assets.Saved.Layers[0].States[1].BlendParameter=="MoveSpeed"&&assets.Saved.Layers[0].Transitions[0].Conditions[0].Parameter=="MoveSpeed","Parameter rename did not update other layers");
    if(!qEnvironmentVariable("GSPROUT_CAPTURE_UI").isEmpty()){dialog.show();QCoreApplication::processEvents();dialog.grab().save(qEnvironmentVariable("GSPROUT_CAPTURE_UI")+"/gamesprout-layer-controller-ui.png");dialog.hide();}
    Find<QPushButton>(dialog,"addLayerButton")->click();clip->SetValue(idle.Clip);clip->Changed(idle.Clip);Find<QLineEdit>(dialog,"layerNameEdit")->setText("Hands");
    Find<QPushButton>(dialog,"moveLayerUpButton")->click();Check(layers->currentIndex()==1&&Find<QLineEdit>(dialog,"layerNameEdit")->text()=="Hands","Layer reorder lost draft or crossed base");
    Find<QPushButton>(dialog,"removeLayerButton")->click();Check(layers->count()==2,"Layer removal failed");
    Find<QPushButton>(dialog,"saveButton")->click();Check(assets.Saves==5&&assets.Saved.Layers[1].Name=="Upper Body","Removing layer lost surviving layer data");
    layers->setCurrentIndex(0);
    assets.Snapshot="outside";Find<QPushButton>(dialog,"saveButton")->click();Check(assets.Saves==5&&Find<QLabel>(dialog,"statusLabel")->text().contains("conflict"),"External source conflict overwritten");
    states->item(0,2)->setText("1.25");dialog.show();QCoreApplication::processEvents();

    QTimer::singleShot(0,[]{auto* confirmation=QApplication::activeModalWidget();Check(confirmation,"Missing unsaved confirmation .ui");Find<QPushButton>(*confirmation,"cancelButton")->click();});
    dialog.reject();Check(dialog.isVisible()&&states->item(0,2)->text()=="1.25","Cancel close lost unsaved draft");
    QTimer::singleShot(0,[]{auto* confirmation=QApplication::activeModalWidget();Check(confirmation,"Missing discard confirmation");Find<QPushButton>(*confirmation,"discardButton")->click();});
    dialog.reject();Check(!dialog.isVisible()&&assets.Saves==5,"Discard close unexpectedly saved source");
    std::cout<<"Controller .ui editing/default/transition/blend/conflict/unsaved-close checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
