#pragma once
#include "RuntimeResourceApi.h"
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QThread>
#include <QMetaObject>
#include <QTableWidget>
#include <QGroupBox>
#include "DirectClipInspectorChecks.h"

class InspectorAnimationResources final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t GetRevision()const override{return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&)override{return false;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{return false;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::SkeletonData& data,std::string&)override{
        data={};GameSproutRuntime::SkeletonNode node;node.Path="Bone#0";node.Name="Bone";node.Transform.Position={4,0,0};data.Nodes.push_back(node);return true;
    }
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference& ref,GameSproutRuntime::AnimationClipData& data,std::string&)override{
        data={};data.Duration=1;data.SampleRate=1;data.Tracks={"Bone#0"};
        GameSproutRuntime::AnimationFrame a,b;a.Time=0;b.Time=1;a.Poses.resize(1);b.Poses.resize(1);
        a.Poses[0].Position={4+float(ref.LocalID-1)*10,0,0};b.Poses[0].Position={14+float(ref.LocalID-1)*10,0,0};data.Frames={a,b};
        data.Events.push_back({.2f,"Footstep","Left"});
        if(ref.LocalID==3) {
            data.Frames[0].Poses[0].Position={4,0,0};data.Frames[1].Poses[0].Position={14,0,0};
            data.RootMotionNodePath="Bone#0";data.RootMotion.resize(2);data.RootMotion[1].Translation={10,0,0};
        }
        return true;
    }
    bool LoadAnimatorController(const GameSproutRuntime::ResourceReference& ref,GameSproutRuntime::AnimatorControllerData& data,std::string&)override{
        data={};data.Parameters={{"Speed",GameSproutRuntime::AnimatorParameterType::Float,0},{"Go",GameSproutRuntime::AnimatorParameterType::Trigger,0}};
        GameSproutRuntime::AnimatorLayer base;base.Name="Base Layer";
        for(int i=0;i<2;++i){GameSproutRuntime::AnimatorState state;state.Name=i?"Run":"Idle";state.Clip={ref.Guid,std::uint64_t(i+1)};base.States.push_back(state);}
        GameSproutRuntime::AnimatorTransition idle;idle.To=0;
        idle.Conditions={{"Speed",GameSproutRuntime::AnimatorCompare::Less,.1}};
        base.Transitions.push_back(idle);
        data.Layers.push_back(base);auto upper=base;upper.Name="Upper Body";upper.DefaultWeight=0;data.Layers.push_back(upper);return true;
    }
};

static void CheckAnimationInspectorFlow(ISceneManager* scenes,ISelectionManager* selection,QDockWidget* inspectorDock,
    const QDir& root,const std::function<void()>& tick,
    const std::function<void(const std::function<bool()>&)>& pump) {
    auto* runtime=scenes->GetRuntime();InspectorAnimationResources resources;
    struct ProviderScope {
        GameSproutRuntime::IGameSproutRuntime* Runtime;
        GameSproutRuntime::IRuntimeResourceProvider* Previous;
        ~ProviderScope(){Runtime->SetResourceProvider(Previous);}
    } scope{runtime,runtime->GetResourceProvider()};
    runtime->SetResourceProvider(&resources);
    auto* scene=scenes->GetActiveScene();auto* character=scene->CreateGameObject("Inspector Animated Character");
    auto* bone=scene->CreateGameObject("Inspector Animated Bone",character);bone->GetTransform()->SetLocalPosition({4,0,0});
    auto* geometry=scene->CreateGameObject("Inspector Animated Geometry",character);
    auto* animator=character->AddComponent<GameSproutRuntime::IAnimatorComponent>();
    Check(animator,"Animation Inspector fixture cannot create Animator");
    const std::string guid="11112222333344445555666677778888";
    animator->SetSkeleton({guid,10});animator->SetController({guid,11});animator->SetDefaultClip({guid,1});animator->SetPlayOnStart(false);
    Check(animator->SetNodeBindings({{"Bone#0",bone->GetInstanceID()}}),"Animation Inspector fixture cannot bind bone");
    geometry->AddComponent<GameSproutRuntime::IMeshComponent>();geometry->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
    auto* skin=geometry->AddComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();Check(skin,"Animation Inspector fixture cannot create skin renderer");
    tick();std::string error;
    Check(scenes->SaveSceneAs("Scenes/AnimatorInspector.scene",error),error);
    const auto select=[&](GameSproutRuntime::IGameObject* object,const char* formName){
        selection->SetSceneObjectSelection({scene->GetInstanceID(),object->GetInstanceID()});
        pump([&]{auto* id=inspectorDock->findChild<QLineEdit*>("localIDEdit");auto* form=inspectorDock->findChild<QWidget*>(formName);return id&&id->text().toULongLong()==object->GetInstanceID()&&form&&form->isVisible();});
        return inspectorDock->findChild<QWidget*>(formName);
    };
    auto* form=select(character,"AnimatorComponentEditorForm");
    CheckDirectClipInspector(scenes,character,bone,form,root,tick,pump);
    // Leaving the inspected object must tear down its temporary Clip playback.
    Check(scenes->PlayAnimationClip(character->GetInstanceID(),{guid,2},.4f,1,true,error),error);
    select(geometry,"SkinnedMeshRendererComponentEditorForm");
    Check(!scenes->GetAnimationClipPreviewState(character->GetInstanceID()).Active
        &&bone->GetTransform()->GetWorldMatrix()(0,3)==4,"Changing Inspector selection retained a direct Clip preview");
    form=select(character,"AnimatorComponentEditorForm");
    auto* previewMode=form->findChild<QComboBox*>("previewModeCombo");
    Check(previewMode,"Animation preview mode selector missing");previewMode->setCurrentIndex(1);
    auto* play=form->findChild<QPushButton*>("playButton");auto* pause=form->findChild<QPushButton*>("pauseButton");
    auto* stop=form->findChild<QPushButton*>("stopButton");auto* seek=form->findChild<QPushButton*>("seekButton");
    auto* seekSpin=form->findChild<QDoubleSpinBox*>("seekSpin");auto* speed=form->findChild<QDoubleSpinBox*>("speedSpin");
    auto* state=form->findChild<QComboBox*>("stateCombo");auto* parameters=form->findChild<QComboBox*>("parameterCombo");
    auto* parameterSpin=form->findChild<QDoubleSpinBox*>("parameterSpin");auto* setParameter=form->findChild<QPushButton*>("setParameterButton");
    Check(play&&pause&&stop&&seek&&seekSpin&&speed&&state&&parameters&&parameterSpin&&setParameter,"Designer Animator controls missing from actual Inspector module");
    Check(state->findText("Idle")>=0&&state->findText("Run")>=0&&parameters->findText("Speed")>=0,"Animator Inspector did not load state and parameter declarations");
    auto* avatarField=form->findChild<QWidget*>("avatarResource");auto* ikGroup=form->findChild<QGroupBox*>("ikGroup");
    Check(avatarField&&ikGroup&&!ikGroup->isEnabled()&&form->findChild<QTableWidget*>("goalTable")&&form->findChild<QPushButton*>("applyLookAtButton"),"Humanoid Avatar / IK generated controls missing or Generic IK was enabled");
    play->click();Check(animator->GetPlaybackState().Playing&&scenes->IsAnimationPreviewPlaying(),"Inspector Play did not start Runtime and editor preview");
    pause->click();Check(!animator->GetPlaybackState().Playing,"Inspector Pause did not pause Animator");
    seekSpin->setValue(.5);seek->click();
    Check(std::fabs(bone->GetTransform()->GetWorldMatrix()(0,3)-9)<.001f&&bone->GetTransform()->GetLocalPosition().X==4,"Inspector Seek did not evaluate pose while preserving source TRS");
    Check(!scenes->HasUnsavedChanges(),"Inspector animation preview marked source scene dirty");
    auto* previewLayer=form->findChild<QComboBox*>("previewLayerCombo");auto* layerWeight=form->findChild<QDoubleSpinBox*>("runtimeWeightSpin");
    Check(previewLayer&&layerWeight&&previewLayer->count()==2,"Inspector layer preview controls missing");
    Check(!layerWeight->isEnabled(),"Base runtime weight must be fixed");previewLayer->setCurrentIndex(1);
    layerWeight->setValue(.25);Check(animator->GetLayerWeight(1)==.25f&&!scenes->HasUnsavedChanges(),"Layer preview weight failed or dirtied scene");
    state->setCurrentIndex(state->findText("Run"));play->click();pause->click();
    Check(animator->GetPlaybackState(1).CurrentState=="Run"&&!scenes->HasUnsavedChanges(),"Layer Play targeted wrong state machine");
    // With keyboard tracking disabled the timer must preserve an uncommitted typed edit.
    layerWeight->setFocus(Qt::OtherFocusReason);QCoreApplication::processEvents();
    if(layerWeight->hasFocus()){
        auto* weightEdit=layerWeight->findChild<QLineEdit*>();Check(weightEdit,"Missing spinbox value editor");weightEdit->setText("0.375");
        QMetaObject::invokeMethod(weightEdit,"textEdited",Qt::DirectConnection,Q_ARG(QString,QString("0.375")));
        QElapsedTimer editingTimer;editingTimer.start();while(editingTimer.elapsed()<250){QCoreApplication::processEvents();QThread::msleep(2);}
        Check(weightEdit->text().contains("0.375"),"Runtime polling overwrote uncommitted layer-weight text");
        layerWeight->clearFocus();
    }
    layerWeight->setValue(0);previewLayer->setCurrentIndex(0);state->setCurrentIndex(state->findText("Idle"));
    parameters->setCurrentIndex(parameters->findText("Speed"));parameterSpin->setValue(.75);setParameter->click();
    double parameter=0;Check(animator->GetParameter("Speed",parameter)&&parameter==.75&&!scenes->HasUnsavedChanges(),"Inspector parameter edit failed or dirtied authoring scene");
    parameters->setCurrentIndex(parameters->findText("Go"));setParameter->click();
    Check(animator->GetParameter("Go",parameter)&&parameter==1,"Inspector trigger button did not dispatch the typed parameter");
    state->setCurrentIndex(state->findText("Run"));form->findChild<QPushButton*>("crossFadeButton")->click();
    Check(animator->GetPlaybackState().NextState=="Run","Inspector state switching did not call CrossFade");
    pause->click();seekSpin->setValue(.5);seek->click();
    speed->setValue(1.5);
    Check(animator->GetSpeed()==1.5f&&scenes->HasUnsavedChanges()&&scenes->CanUndo(),"Inspector speed setting bypassed authoring/history services");
    Check(scenes->Undo(error)&&animator->GetSpeed()==1,error);
    Check(scenes->Redo(error)&&animator->GetSpeed()==1.5f,error);tick();
    // The skin combo uses stable file identities in Undo operations.
    auto* skinForm=select(geometry,"SkinnedMeshRendererComponentEditorForm");
    auto* combo=skinForm->findChild<QComboBox*>("animatorCombo");Check(combo,"Designer skin Animator combo missing");
    int target=combo->findData(QVariant::fromValue<qulonglong>(character->GetInstanceID()));Check(target>0,"Skin Inspector omitted the scene Animator");
    combo->setCurrentIndex(target);Check(QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,target)),"Cannot activate skin combo");
    Check(skin->GetAnimatorObjectID()==character->GetInstanceID(),"Skin combo did not bind Animator");
    Check(scenes->Undo(error)&&skin->GetAnimatorObjectID()==0,error);
    Check(scenes->Redo(error)&&skin->GetAnimatorObjectID()==character->GetInstanceID(),error);
    Check(scenes->SaveScene(error),error);
    Json::Value saved;Check(AssetArtifactIO::Parse(ReadFile(root.filePath("Assets/Scenes/AnimatorInspector.scene")).toStdString(),saved),"Cannot parse saved Animator scene");
    bool foundAnimator=false,foundBone=false;
    for(const auto& node:saved["Objects"]){
        if(node["Name"]=="Inspector Animated Bone")for(const auto& component:node["Components"])if(component["Type"]=="GameSproutRuntime.Transform"){foundBone=true;Check(component["Position"][0].asFloat()==4,"Scene saved evaluated bone pose instead of source transform");}
        if(node["Name"]=="Inspector Animated Character")for(const auto& component:node["Components"])if(component["Type"]=="GameSproutRuntime.Animator"){
            foundAnimator=true;Check(component["Speed"].asFloat()==1.5f&&!component.isMember("Time")&&!component.isMember("CurrentState")&&!component.isMember("Parameters"),"Scene omitted authoring setting or saved live playback state");}
    }
    Check(foundAnimator&&foundBone,"Scene omitted animated object source data");
    Check(scenes->OpenScene("Scenes/AnimatorInspector.scene",error),error);scene=scenes->GetActiveScene();
    character=scene->FindChild("Inspector Animated Character");bone=character?character->FindChild("Inspector Animated Bone"):nullptr;geometry=character?character->FindChild("Inspector Animated Geometry"):nullptr;
    animator=character?character->GetComponent<GameSproutRuntime::IAnimatorComponent>():nullptr;
    skin=geometry?geometry->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>():nullptr;
    Check(animator&&bone&&skin&&animator->GetSpeed()==1.5f&&bone->GetTransform()->GetLocalPosition().X==4&&skin->GetAnimatorObjectID()==character->GetInstanceID(),"Reopened Inspector animation scene lost configuration or remapped bindings");
    Check(!animator->GetPlaybackState().Playing&&animator->GetPlaybackState().Time==0,"Reopen restored transient playback state");
    form=select(character,"AnimatorComponentEditorForm");
    form->findChild<QComboBox*>("previewModeCombo")->setCurrentIndex(1);
    form->findChild<QPushButton*>("playButton")->click();form->findChild<QPushButton*>("pauseButton")->click();
    form->findChild<QDoubleSpinBox*>("seekSpin")->setValue(.5);form->findChild<QPushButton*>("seekButton")->click();
    form->findChild<QPushButton*>("stopButton")->click();
    Check(bone->GetTransform()->GetWorldMatrix()(0,3)==4&&!animator->GetPlaybackState().Playing,"Inspector Restore did not clear the evaluated pose");
    // Root motion mode is authoring configuration; motion and notifications remain transient.
    animator->SetController({});animator->SetDefaultClip({guid,3});
    Check(scenes->SaveScene(error),error);tick();form=select(character,"AnimatorComponentEditorForm");
    form->findChild<QComboBox*>("previewModeCombo")->setCurrentIndex(1);
    auto* rootMode=form->findChild<QComboBox*>("rootMotionModeCombo");
    auto* notices=form->findChild<QTableWidget*>("animationNotificationsTable");
    Check(rootMode&&notices&&form->findChild<QLabel*>("rootMotionDeltaLabel")&&form->findChild<QLabel*>("rootMotionRotationLabel"),"Root motion/notification Designer controls missing");
    rootMode->setCurrentIndex(2);Check(QMetaObject::invokeMethod(rootMode,"activated",Qt::DirectConnection,Q_ARG(int,2)),"Cannot activate root mode");
    Check(animator->GetRootMotionMode()==GameSproutRuntime::RootMotionMode::ApplyToObject&&scenes->HasUnsavedChanges(),"Root motion mode bypassed the authoring transaction");
    Check(scenes->Undo(error)&&animator->GetRootMotionMode()==GameSproutRuntime::RootMotionMode::Disabled,error);
    Check(scenes->Redo(error)&&animator->GetRootMotionMode()==GameSproutRuntime::RootMotionMode::ApplyToObject,error);
    Check(scenes->SaveScene(error),error);tick();form=select(character,"AnimatorComponentEditorForm");
    form->findChild<QPushButton*>("playButton")->click();runtime->Update(.35f);scenes->SetAnimationPreviewPlaying(false);
    Check(character->GetTransform()->GetWorldMatrix()(0,3)>5&&character->GetTransform()->GetLocalPosition().X==0,"Applied root motion modified source TRS or failed to move the running object");
    Check(std::fabs(bone->GetTransform()->GetWorldMatrix()(0,3)-9.25f)<.01f&&bone->GetTransform()->GetLocalPosition().X==4,"Root motion was applied twice or lost during pose stripping");
    Check(!scenes->HasUnsavedChanges(),"Root motion or event preview dirtied the editing scene");
    pump([&]{return notices->rowCount()>0;});
    bool footstep=false;for(int row=0;row<notices->rowCount();++row)if(notices->item(row,1)&&notices->item(row,1)->text()=="Footstep")footstep=notices->item(row,2)->text()=="Left";
    Check(footstep,"Runtime clip notification was not forwarded to the active Inspector");
    scenes->SetAnimationPreviewPlaying(true);runtime->Update(100);scenes->SetAnimationPreviewPlaying(false);
    pump([&]{return notices->rowCount()==64;});
    form->findChild<QGroupBox*>("animationNotificationsGroup")->setChecked(true);
    form->findChild<QPushButton*>("clearAnimationNotificationsButton")->click();Check(notices->rowCount()==0,"Clear notifications did not clear the view data");
    form->findChild<QPushButton*>("stopButton")->click();Check(character->GetTransform()->GetWorldMatrix()(0,3)==0,"Root-motion Restore did not restore the authoring basis");
    scenes->ResetAnimationPreview();selection->SetSceneObjectSelection({});tick();
    std::cout<<"PASS Designer Animator/SkinnedMeshRenderer Inspector: playback, typed parameters, Undo/Redo, source-pose save and reopen\n";
}
