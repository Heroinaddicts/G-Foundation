#include "../../View/InspectorUI/AvatarMaskDialog.h"
#include "IRuntimeAssets.h"
#include "HumanoidImportSettings.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QTableWidget>
#include <QLabel>
#include <QTimer>
#include <iostream>
#include <stdexcept>
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T* Find(QWidget& parent,const char* name){auto* value=parent.findChild<T*>(name);Check(value,"Missing .ui control");return value;}
class Provider final:public GameSproutRuntime::IRuntimeResourceProvider {
public:
    int SkeletonLoads=0;
    std::uint64_t GetRevision()const override{return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&)override{return false;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{return false;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::SkeletonData& data,std::string&)override{
        ++SkeletonLoads;data={};for(int index=0;index<4;++index){GameSproutRuntime::SkeletonNode node;
            node.Name=index==0?"Arm":index==1?"Root":index==2?"Spine":"Leg";
            node.Path=index==0?"ArmPath":index==1?"RootPath":index==2?"SpinePath":"LegPath";
            node.Parent=index==0?2:index==1?-1:1;data.Nodes.push_back(node);}return true;
    }
};
class Assets final:public IRuntimeAssets {
public:
    mutable Provider Resources;GameSproutRuntime::AvatarMaskData Data,Saved;std::string Snapshot="initial";int Saves=0;
    bool LoadModelRigSource(const GameSproutRuntime::ResourceReference&,ModelRigImportSettings&,GameSproutRuntime::SkeletonData&,std::string&,std::string&)const override{return false;}
    bool SaveModelRigSource(const GameSproutRuntime::ResourceReference&,const ModelRigImportSettings&,const std::string&,std::string&)override{return false;}
    bool SuggestHumanoidMapping(const GameSproutRuntime::SkeletonData&,std::vector<GameSproutRuntime::HumanBoneMapping>&,std::string&)const override{return false;}
    GameSproutRuntime::IRuntimeResourceProvider* GetResourceProvider()const override{return &Resources;}
    bool CreateAnimatorControllerAsset(const std::string&,const std::string&,const GameSproutRuntime::ResourceReference&,std::string&,std::string&)override{return false;}
    bool LoadAnimatorControllerSource(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimatorControllerData&,std::string&,std::string&)const override{return false;}
    bool SaveAnimatorControllerSource(const GameSproutRuntime::ResourceReference&,const GameSproutRuntime::AnimatorControllerData&,const std::string&,std::string&)override{return false;}
    bool CreateAvatarMaskSource(const std::string&,const std::string&,const GameSproutRuntime::ResourceReference&,std::string&,std::string&)override{return false;}
    bool LoadAvatarMaskSource(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AvatarMaskData& data,std::string& snapshot,std::string&)const override{data=Data;snapshot=Snapshot;return true;}
    bool SaveAvatarMaskSource(const GameSproutRuntime::ResourceReference&,const GameSproutRuntime::AvatarMaskData& data,const std::string& expected,std::string& error)override{
        if(expected!=Snapshot){error="external conflict";return false;}Data=Saved=data;++Saves;return true;
    }
protected:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
}
int main(int argc,char** argv){QApplication app(argc,argv);try{
    {
        Assets human;human.Data.Mode=GameSproutRuntime::AvatarMaskMode::Humanoid;human.Data.DefaultWeight=0;human.Data.RootMotionWeight=1;
        AvatarMaskDialog portable(&human,nullptr,nullptr,nullptr,{"00000001000000020000000300000004",3});
        auto* parts=Find<QTableWidget>(portable,"humanPartsTable");
        Check(parts->rowCount()==8&&!parts->isHidden()&&Find<QTreeWidget>(portable,"bonesTree")->isHidden()&&human.Resources.SkeletonLoads==0,"Portable Human mask unexpectedly required or displayed a model Skeleton");
        parts->item(int(GameSproutRuntime::HumanBodyPart::LeftArm),1)->setText("0.5");Find<QDoubleSpinBox>(portable,"rootMotionWeightSpin")->setValue(.2);Find<QPushButton>(portable,"saveButton")->click();
        Check(human.Saves==1&&human.Saved.Skeleton.Guid.empty()&&human.Saved.Nodes.empty()&&human.Saved.HumanParts.size()==1&&human.Saved.HumanParts[0].Part==GameSproutRuntime::HumanBodyPart::LeftArm&&human.Saved.HumanParts[0].Weight==.5f&&human.Saved.RootMotionWeight==.2f,"Portable mask did not preserve semantic weights independently of root motion");
        if(!qEnvironmentVariable("GSPROUT_CAPTURE_UI").isEmpty()){portable.show();QCoreApplication::processEvents();portable.grab().save(qEnvironmentVariable("GSPROUT_CAPTURE_UI")+"/gamesprout-humanoid-mask-ui.png");}
    }

    Assets assets;assets.Data.Skeleton={"00000001000000020000000300000004",1};assets.Data.DefaultWeight=1;assets.Data.RootMotionWeight=1;
    AvatarMaskDialog dialog(&assets,nullptr,nullptr,nullptr,{"00000001000000020000000300000004",2});
    auto* tree=Find<QTreeWidget>(dialog,"bonesTree");auto* root=tree->topLevelItem(0);Check(root&&root->childCount()==2,"Skeleton hierarchy not shown");
    root->setCheckState(0,Qt::Unchecked);Check(root->child(0)->checkState(0)==Qt::Unchecked&&root->child(0)->child(0)->checkState(0)==Qt::Unchecked,"Parent toggle failed to cascade");
    auto* spine=root->child(0);spine->setText(1,"0.5");Check(spine->child(0)->text(1)=="0.5"&&root->child(1)->text(1)=="0","Weight did not apply only to selected subtree");
    auto* rootMotion=Find<QDoubleSpinBox>(dialog,"rootMotionWeightSpin");Check(rootMotion->value()==1,"Bone toggles changed root motion weight");rootMotion->setValue(.25);
    Find<QPushButton>(dialog,"saveButton")->click();Check(assets.Saves==1&&assets.Saved.Nodes.size()==2&&assets.Saved.Nodes[0].Weight==0&&assets.Saved.Nodes[1].Weight==.5f&&assets.Saved.RootMotionWeight==.25f,"Mask sparse draft did not save through Module");
    tree=Find<QTreeWidget>(dialog,"bonesTree");root=tree->topLevelItem(0);Check(root->child(0)->child(0)->text(1)=="0.5","Mask reload lost inherited weights");
    if(!qEnvironmentVariable("GSPROUT_CAPTURE_UI").isEmpty()){dialog.show();QCoreApplication::processEvents();dialog.grab().save(qEnvironmentVariable("GSPROUT_CAPTURE_UI")+"/gamesprout-avatar-mask-ui.png");dialog.hide();}
    root->child(1)->setText(1,"2");Find<QPushButton>(dialog,"saveButton")->click();Check(assets.Saves==1,"Invalid weight saved");root->child(1)->setText(1,"0.3");
    assets.Snapshot="outside";Find<QPushButton>(dialog,"saveButton")->click();Check(assets.Saves==1&&Find<QLabel>(dialog,"statusLabel")->text().contains("conflict"),"External source overwritten");
    dialog.show();QCoreApplication::processEvents();
QTimer::singleShot(0,[]{auto* confirmation=QApplication::activeModalWidget();Check(confirmation,"Missing unsaved confirmation");Find<QPushButton>(*confirmation,"cancelButton")->click();});
    dialog.reject();Check(dialog.isVisible()&&root->child(1)->text(1)=="0.3","Cancel lost mask draft");
    QTimer::singleShot(0,[]{auto* confirmation=QApplication::activeModalWidget();Check(confirmation,"Missing discard confirmation");Find<QPushButton>(*confirmation,"discardButton")->click();});dialog.reject();
    Check(!dialog.isVisible()&&assets.Saves==1,"Discard saved unexpectedly");
    std::cout<<"AvatarMask tree/subtree/weight/inheritance/root motion/conflict/draft checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
