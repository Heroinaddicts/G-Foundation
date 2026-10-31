#include "AvatarMaskInspector.h"
#include "IRuntimeAssets.h"
#include "ResourceField.h"
#include "ui_AvatarMaskInspector.h"
AvatarMaskInspector::AvatarMaskInspector(QWidget* parent):QWidget(parent),_Ui(new Ui::AvatarMaskInspectorForm){
    _Ui->setupUi(this);connect(_Ui->editButton,&QPushButton::clicked,this,[this]{if(Edit)Edit();});
}
AvatarMaskInspector::~AvatarMaskInspector(){delete _Ui;}
void AvatarMaskInspector::Load(IRuntimeAssets* resources,IAssetsManager* assets,ILibraryManager* library,ISelectionManager* selection,
    const GameSproutRuntime::ResourceReference& reference){
    _Ui->skeletonField->Bind(assets,library,selection,eAssetType::Skeleton);_Ui->skeletonField->setEnabled(false);
    GameSproutRuntime::AvatarMaskData data;std::string snapshot,error;
    const bool loaded=resources&&resources->LoadAvatarMaskSource(reference,data,snapshot,error);
    _Ui->editButton->setEnabled(loaded);_Ui->skeletonField->SetValue(loaded?data.Skeleton:GameSproutRuntime::ResourceReference{});
    const bool human=loaded&&data.Mode==GameSproutRuntime::AvatarMaskMode::Humanoid;_Ui->skeletonField->setVisible(!human);
    _Ui->summaryLabel->setText(loaded?tr("%1\n默认权重 %2 · 根运动权重 %3").arg(human?tr("Humanoid 人体部位遮罩 · 可跨模型"):tr("Transform 骨架路径遮罩")).arg(data.DefaultWeight,0,'g',3).arg(data.RootMotionWeight,0,'g',3):QString::fromStdString(error));
}
