#include "../../View/InspectorUI/AnimationClipOptionsEditor.h"
#include "../../View/InspectorUI/ModelAnimationSettingsEditor.h"
#include "../../View/InspectorUI/AnimationClipInspector.h"
#include "../../View/InspectorUI/ResourceField.h"
#include "../../View/InspectorUI/ModelInspector.h"
#include "../../View/InspectorUI/AnimationClipPreviewInspector.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
template<class T>T* Find(QWidget& parent,const char* name){auto* value=parent.findChild<T*>(name);Check(value,std::string("Missing Designer control: ")+name);return value;}
Json::Value Options(const char* name="Walk") {
    Json::Value value(Json::objectValue);value["Name"]=name;value["ExtractRootMotion"]=true;value["RootMotionNode"]="/Root#0/Hips#0";
    Json::Value event(Json::objectValue);event["Time"]=0.123456789;event["Name"]="Footstep";event["Payload"]="左脚";
    value["Events"]=Json::Value(Json::arrayValue);value["Events"].append(event);return value;
}
Json::Value ModelSettings() {
    Json::Value value(Json::objectValue);value["ScaleFactor"]=1;value["UseFileScale"]=true;value["NormalsMode"]="Import";
    value["NormalSmoothingAngle"]=60;value["TangentsMode"]="Calculate";value["WeldVertices"]=true;
    value["KeepCPUCopy"]=false;value["MeshCompression"]="Off";value["OptimizeMesh"]=true;value["GenerateLightmapUVs"]=false;
    value["ImportBlendShapes"]=true;value["ImportCameras"]=false;value["ImportLights"]=false;value["ImportAnimation"]=true;
    value["AnimationType"]="Generic";value["ImportMaterials"]=true;value["AnimationClips"]=Json::Value(Json::arrayValue);return value;
}
}
int main(int argc,char** argv){QApplication application(argc,argv);try {
    std::string error;Json::Value read;
    AnimationClipOptionsEditor options;int changes=0;options.Changed=[&]{++changes;};
    options.SetNodePaths({"/Root#0","/Root#0/Hips#0"});const auto original=Options();options.Load(original);
    Check(changes==0&&options.Read(read,error)&&read==original,"Loading options changed the source or lost numeric precision");
    Find<QComboBox>(options,"rootMotionNodeCombo")->setCurrentText("/Root#0");
    Check(changes==1&&options.Read(read,error)&&read["RootMotionNode"]=="/Root#0","Root source node edit not captured");
    auto* events=Find<QTableWidget>(options,"animationEventsTable");events->item(0,2)->setText("Right foot");
    Find<QPushButton>(options,"addAnimationEventButton")->click();Check(events->rowCount()==2,"Add event did not append draft row");
    events->item(1,0)->setText("0.75");events->item(1,1)->setText("Land");
    Check(options.Read(read,error)&&read["Events"].size()==2&&read["Events"][1]["Time"].asDouble()==.75,"Event edit not captured");
    events->item(1,0)->setText("0.05");Find<QPushButton>(options,"sortAnimationEventsButton")->click();
    Check(events->item(0,1)->text()=="Land"&&options.Read(read,error)&&read["Events"][0]["Time"].asDouble()==.05,"Event-time sorting lost row payloads");
    events->item(1,0)->setText("NaN");Check(!options.Read(read,error)&&!error.empty(),"Invalid event time was accepted");
    events->item(1,0)->setText("0.75");events->setCurrentCell(1,0);Find<QPushButton>(options,"removeAnimationEventButton")->click();
    Check(options.Read(read,error)&&read["Events"].size()==1,"Remove event lost remaining events");
    options.Load(Json::Value(Json::objectValue));Check(options.Read(read,error)&&read.empty(),"Absent defaults became a dirty draft");

    ModelAnimationSettingsEditor clips;clips.SetSources({"Walk","Idle"},{"/Root#0","/Root#0/Hips#0"});Json::Value empty(Json::arrayValue);clips.Load(empty);
    Check(clips.Read(read,error)&&read==empty,"Unconfigured source clips were materialized during load");
    Find<QComboBox>(clips,"sourceClipCombo")->setCurrentText("Walk");Find<QPushButton>(clips,"addClipOptionsButton")->click();
    Find<QCheckBox>(clips,"extractRootMotionCheck")->setChecked(true);Find<QComboBox>(clips,"rootMotionNodeCombo")->setCurrentText("/Root#0/Hips#0");
    Find<QPushButton>(clips,"addAnimationEventButton")->click();
    Find<QComboBox>(clips,"sourceClipCombo")->setCurrentText("Idle");Find<QPushButton>(clips,"addClipOptionsButton")->click();
    Check(clips.Read(read,error)&&read.size()==2&&read[0]["Name"]=="Walk"&&read[0]["Events"].size()==1&&read[1]["Name"]=="Idle","Switching clip lost the previous clip draft");
    Find<QListWidget>(clips,"configuredClipsList")->setCurrentRow(0);auto* table=Find<QTableWidget>(clips,"animationEventsTable");table->item(0,0)->setText("bad");
    Find<QListWidget>(clips,"configuredClipsList")->setCurrentRow(1);
    Check(Find<QListWidget>(clips,"configuredClipsList")->currentRow()==0&&!clips.Read(read,error),"Invalid draft was silently lost during clip switch");
    Find<QPushButton>(clips,"removeClipOptionsButton")->click();Check(clips.Read(read,error)&&read.size()==1&&read[0]["Name"]=="Idle","Removing invalid clip draft did not preserve other entries");

    AnimationClipInspector native(nullptr);Json::Value nativeSettings(Json::objectValue);nativeSettings["MainObjectID"]=Json::UInt64(0x123456789abcdef0ull);
    nativeSettings["ClipOptions"]=Options();nativeSettings["ClipOptions"].removeMember("Name");native.SetNodePaths({"/Root#0/Hips#0"});native.Load(nativeSettings);
    Check(native.Read(read,error)&&read==nativeSettings,"Native clip options lost MainObjectID or dirtied on load");
    auto* sourceAvatar=Find<ResourceField>(native,"sourceAvatarField");const GameSproutRuntime::ResourceReference avatar{"00000001000000020000000300000004",25};
    sourceAvatar->SetValue(avatar);sourceAvatar->Changed(avatar);Check(native.Read(read,error)&&read["SourceAvatar"]["Guid"]==avatar.Guid&&read["SourceAvatar"]["LocalID"].asUInt64()==25&&read["MainObjectID"]==nativeSettings["MainObjectID"],"Source Avatar did not remain in the import-settings draft");
    native.Load(read);Check(sourceAvatar->GetValue()==avatar,"Source Avatar draft did not reload");sourceAvatar->SetValue({});sourceAvatar->Changed({});Check(native.Read(read,error)&&!read.isMember("SourceAvatar"),"Clearing Source Avatar wrote an invalid null reference");
    Find<QPushButton>(native,"addAnimationEventButton")->click();Check(native.Read(read,error)&&read["MainObjectID"]==nativeSettings["MainObjectID"]&&read["ClipOptions"]["Events"].size()==2,"Native options edits did not preserve unrelated settings");
    ModelInspector model(nullptr);const auto settings=ModelSettings();model.SetAnimationSources({"Walk"},{"/Root#0/Hips#0"});model.Load(settings);
    Check(model.Read(read,error)&&read==settings,"Model ImportSettings became dirty simply by loading motion UI: "+error);
    Find<QComboBox>(model,"sourceClipCombo")->setCurrentText("Walk");Find<QPushButton>(model,"addClipOptionsButton")->click();
    Check(model.Read(read,error)&&read["AnimationClips"].size()==1&&read["OptimizeMesh"].asBool(),"Model options did not preserve untouched disabled fields");
    AnimationClipPreviewInspector preview;
    GameSproutRuntime::AnimationClipData prepared;prepared.Duration=2;prepared.SampleRate=30;prepared.RootMotionNodePath="/Root#0/Hips#0";prepared.RootMotion.resize(2);prepared.Events.push_back({.3f,"Hit","Payload"});
    int editRequests=0;preview.EditSource=[&]{++editRequests;};preview.Load(prepared);
    Check(Find<QLabel>(preview,"rootMotionNodeLabel")->text()=="/Root#0/Hips#0"&&Find<QTableWidget>(preview,"importedEventsTable")->rowCount()==1,"Imported clip inspection lost motion/events");
    Check(Find<QTableWidget>(preview,"importedEventsTable")->editTriggers()==QAbstractItemView::NoEditTriggers,"Imported clip unexpectedly editable");
    Find<QPushButton>(preview,"editClipSourceSettingsButton")->click();Check(editRequests==1,"Imported clip did not route editing to source settings");
    if(qEnvironmentVariableIsSet("GAMESPROUT_MOTION_UI_CAPTURE")) {
        auto displaySettings=ModelSettings();displaySettings["AnimationClips"].append(Options());model.Load(displaySettings);
        Find<QTabWidget>(model,"modelTabs")->setCurrentIndex(1);model.resize(560,820);model.show();QCoreApplication::processEvents();
        Check(model.grab().save(qEnvironmentVariable("GAMESPROUT_MOTION_UI_CAPTURE")),"Cannot capture motion import UI");
    }
    std::cout<<"PASS motion/event Designer draft: defaults, sources, precision, clip switching, validation and native/model preservation\n";return 0;
}catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}}
