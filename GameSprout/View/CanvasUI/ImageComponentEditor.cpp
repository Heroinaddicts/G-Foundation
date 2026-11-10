#include "ImageComponentEditor.h"
#include "ResourceField.h"
#include "ui_ImageComponentEditor.h"
#include <QLineEdit>

ImageComponentEditor::ImageComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::ImageComponentEditorForm()) {
    _Ui->setupUi(this);
    _Ui->textureField->Changed = [this](const GameSproutRuntime::ResourceReference& resource) {
        if (_Loading||!_PropertiesEditable) return;
        auto* object = _Context.Resolve();
        auto* component = object ? object->GetComponent<GameSproutRuntime::IImageComponent>() : nullptr;
        if (!component) { Fail("Image 组件已失效。"); return; }
        auto next = InspectorValues::Read(*component); next.Texture = resource;
        Commit<GameSproutRuntime::IImageComponent>(next,"绑定 Image 纹理");
    };
    for (QLineEdit* edit : {_Ui->imageColorREdit,_Ui->imageColorGEdit,_Ui->imageColorBEdit,_Ui->imageColorAEdit,_Ui->imageUVPositionXEdit,_Ui->imageUVPositionYEdit,_Ui->imageUVSizeXEdit,_Ui->imageUVSizeYEdit}) QObject::connect(edit,&QLineEdit::editingFinished,this,[this,edit]{CommitValues(edit);});
}
ImageComponentEditor::~ImageComponentEditor() { _Ui->textureField->Changed = {}; delete _Ui; }
void ImageComponentEditor::ContextChanged() {
    _Ui->textureField->Bind(_Context.Assets,_Context.Library,_Context.Selection,eAssetType::Texture);
    _Ui->textureField->SetContextKey(_Context.Key("Image","Texture"));
}
void ImageComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    const auto v = InspectorValues::Read(static_cast<GameSproutRuntime::IImageComponent&>(component));
    _Ui->textureField->SetValue(v.Texture);
    ShowNumbers({_Ui->imageColorREdit,_Ui->imageColorGEdit,_Ui->imageColorBEdit,_Ui->imageColorAEdit,_Ui->imageUVPositionXEdit,_Ui->imageUVPositionYEdit,_Ui->imageUVSizeXEdit,_Ui->imageUVSizeYEdit},{v.Color.R,v.Color.G,v.Color.B,v.Color.A,v.UV.Position.X,v.UV.Position.Y,v.UV.Size.X,v.UV.Size.Y});
}
void ImageComponentEditor::CommitValues(QWidget* changed) {
    if (_Loading||!_PropertiesEditable) return;
    auto* component=dynamic_cast<GameSproutRuntime::IImageComponent*>(_Context.ResolveComponent());
    if(!component){Fail("Image 组件已失效。");return;}
    auto next=InspectorValues::Read(*component);
    const std::pair<QLineEdit*,float*> fields[]={{_Ui->imageColorREdit,&next.Color.R},{_Ui->imageColorGEdit,&next.Color.G},{_Ui->imageColorBEdit,&next.Color.B},{_Ui->imageColorAEdit,&next.Color.A},
        {_Ui->imageUVPositionXEdit,&next.UV.Position.X},{_Ui->imageUVPositionYEdit,&next.UV.Position.Y},{_Ui->imageUVSizeXEdit,&next.UV.Size.X},{_Ui->imageUVSizeYEdit,&next.UV.Size.Y}};
    for(const auto& [field,value]:fields)if(changed==field&&!ReadNumber(field,*value)){Fail("请输入有限的颜色与 UV 数值。");return;}
    Commit<GameSproutRuntime::IImageComponent>(next,"修改 Image");
}
