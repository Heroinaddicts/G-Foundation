#include "../../View/InspectorUI/ModelRigDialog.h"
#include "../../View/InspectorUI/AnimatorIKEditor.h"
#include <QGroupBox>
#include "IRuntimeAssets.h"
#include "HumanoidValidation.h"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <stdexcept>
#include <iostream>
namespace{
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T* Find(QWidget& parent,const char* name){auto* value=parent.findChild<T*>(name);Check(value,"Missing generated Rig .ui control");return value;}
class Assets final:public IRuntimeAssets{
public:
    ModelRigImportSettings Data,Saved;std::string Snapshot="initial";int Saves=0;
    GameSproutRuntime::IRuntimeResourceProvider* GetResourceProvider()const override{return nullptr;}
    bool LoadModelRigSource(const GameSproutRuntime::ResourceReference&,ModelRigImportSettings& data,GameSproutRuntime::SkeletonData& skeleton,std::string& snapshot,std::string&)const override{
        data=Data;snapshot=Snapshot;skeleton={};GameSproutRuntime::SkeletonNode hips;hips.Path="/Hips";hips.Name="Hips";skeleton.Nodes.push_back(hips);
        auto head=hips;head.Path="/Head";head.Name="Head";head.Parent=0;head.Transform.Position.Y=1;skeleton.Nodes.push_back(head);return true;
    }
    bool SaveModelRigSource(const GameSproutRuntime::ResourceReference&,const ModelRigImportSettings& data,const std::string& snapshot,std::string& error)override{
        if(snapshot!=Snapshot){error="external conflict";return false;}Data=Saved=data;++Saves;return true;
    }
    bool SuggestHumanoidMapping(const GameSproutRuntime::SkeletonData&,std::vector<GameSproutRuntime::HumanBoneMapping>& output,std::string&)const override{
        output={{GameSproutRuntime::HumanBone::Hips,"/Hips",{}},{GameSproutRuntime::HumanBone::Head,"/Head",{}}};return true;
    }
    bool CreateAnimatorControllerAsset(const std::string&,const std::string&,const GameSproutRuntime::ResourceReference&,std::string&,std::string&)override{return false;}
    bool LoadAnimatorControllerSource(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimatorControllerData&,std::string&,std::string&)const override{return false;}
    bool SaveAnimatorControllerSource(const GameSproutRuntime::ResourceReference&,const GameSproutRuntime::AnimatorControllerData&,const std::string&,std::string&)override{return false;}
    bool CreateAvatarMaskSource(const std::string&,const std::string&,const GameSproutRuntime::ResourceReference&,std::string&,std::string&)override{return false;}
    bool LoadAvatarMaskSource(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AvatarMaskData&,std::string&,std::string&)const override{return false;}
    bool SaveAvatarMaskSource(const GameSproutRuntime::ResourceReference&,const GameSproutRuntime::AvatarMaskData&,const std::string&,std::string&)override{return false;}
protected:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
}
int main(int argc,char** argv){QApplication app(argc,argv);try{
    {
        AnimatorIKEditor ik;ik.ResetContext();auto* goals=Find<QTableWidget>(ik,"goalTable");auto* look=Find<QTableWidget>(ik,"lookAtTable");
        Check(goals->rowCount()==3&&goals->columnCount()==4&&look->rowCount()==1&&look->columnCount()==3,"Designer IK tables have invalid dimensions");
        for(int row=0;row<3;++row)for(int column=0;column<4;++column)Check(goals->item(row,column)!=nullptr,"Designer goal table discarded an initial item");
        for(int column=0;column<3;++column)Check(look->item(0,column)!=nullptr&&look->item(0,column)->text()=="0","Designer LookAt table discarded its initial target row");
        for(int goal=0;goal<4;++goal)Find<QComboBox>(ik,"goalCombo")->setCurrentIndex(goal);
        Check(goals->item(1,3)->text()=="1","Unbound IK preview did not initialize identity rotation");
        ik.SetAvailable(false);Check(!Find<QGroupBox>(ik,"ikGroup")->isEnabled(),"Generic IK preview was enabled");
        ik.SetAvailable(true);Find<QGroupBox>(ik,"ikGroup")->setChecked(true);Find<QPushButton>(ik,"clearIKButton")->click();
    }
    Assets assets;ModelRigDialog dialog(&assets,{"00000001000000020000000300000004",1});
    auto* mapping=Find<QTableWidget>(dialog,"mappingTable");auto* bones=Find<QComboBox>(dialog,"boneCombo");
    Check(mapping->rowCount()==55&&mapping->item(0,1)->text().contains("必需"),"Full required/optional human roles are missing");
    Find<QComboBox>(dialog,"rigTypeCombo")->setCurrentIndex(int(eModelAnimationType::Humanoid));Find<QDoubleSpinBox>(dialog,"humanScaleSpin")->setValue(1.7);
    bones->setCurrentIndex(bones->findData("/Head")); // A deliberate manual mapping; suggestions must retain it.
    Find<QPushButton>(dialog,"suggestButton")->click();
    Check(mapping->item(0,2)->text()=="Head"&&mapping->item(int(GameSproutRuntime::HumanBone::Head),2)->text()=="Head","Automatic suggestion overwrote a manual role");
    Check(Find<QComboBox>(dialog,"rigTypeCombo")->currentIndex()==int(eModelAnimationType::Humanoid)&&Find<QDoubleSpinBox>(dialog,"humanScaleSpin")->value()==1.7,"Suggestion lost unsaved rig fields");
    Find<QTableWidget>(dialog,"referenceTable")->item(1,1)->setText("0.25");Find<QPushButton>(dialog,"saveButton")->click();
    Check(assets.Saves==1&&assets.Saved.AnimationType==eModelAnimationType::Humanoid&&assets.Saved.HumanBones.size()==2&&assets.Saved.ReferencePose.size()==1&&assets.Saved.ReferencePose[0].Path=="/Head"&&assets.Saved.ReferencePose[0].Pose.Position.X==.25f,"Rig source draft did not roundtrip through Module API");
    Find<QDoubleSpinBox>(dialog,"humanScaleSpin")->setValue(2);assets.Snapshot="outside";Find<QPushButton>(dialog,"saveButton")->click();
    Check(assets.Saves==1&&Find<QLabel>(dialog,"statusLabel")->text().contains("conflict"),"Rig editor overwrote an external source change");
    if(!qEnvironmentVariable("GSPROUT_CAPTURE_UI").isEmpty()){dialog.show();QCoreApplication::processEvents();dialog.grab().save(qEnvironmentVariable("GSPROUT_CAPTURE_UI")+"/gamesprout-humanoid-rig-ui.png");}
    dialog.show();QTimer::singleShot(0,[]{auto* box=QApplication::activeModalWidget();Check(box,"No unsaved Rig confirmation");Find<QPushButton>(*box,"cancelButton")->click();});dialog.reject();
    Check(dialog.isVisible()&&Find<QDoubleSpinBox>(dialog,"humanScaleSpin")->value()==2,"Cancel closing lost the Rig draft");
    QTimer::singleShot(0,[]{Find<QPushButton>(*QApplication::activeModalWidget(),"discardButton")->click();});dialog.reject();
    std::cout<<"Humanoid Rig .ui mappings/reference/settings/suggestions/conflict/draft checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
