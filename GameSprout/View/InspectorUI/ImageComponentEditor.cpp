#include "ImageComponentEditor.h"
#include "ResourceField.h"
#include "ui_ImageComponentEditor.h"
#include <QLineEdit>

ImageComponentEditor::ImageComponentEditor(QWidget* parent) : ComponentEditor(parent), _Ui(new Ui::ImageComponentEditorForm()) {
    _Ui->setupUi(this);
    _Texture = new ResourceField(_Ui->textureResourceHost);
    _Ui->textureResourceHostLayout->addWidget(_Texture);
    _Texture->Changed = [this](const GameSproutRuntime::ResourceReference& resource) {
        if (_Loading) return;
        auto* object = _Context.Resolve();
        auto* component = object ? object->GetComponent<GameSproutRuntime::IImageComponent>() : nullptr;
        if (!component) { Fail("Image 组件已失效。"); return; }
        auto next = InspectorValues::Read(*component); next.Texture = resource;
        Commit<GameSproutRuntime::IImageComponent>(next,"绑定 Image 纹理");
    };
    for (QLineEdit* edit : {_Ui->imageColorREdit,_Ui->imageColorGEdit,_Ui->imageColorBEdit,_Ui->imageColorAEdit,_Ui->imageUVPositionXEdit,_Ui->imageUVPositionYEdit,_Ui->imageUVSizeXEdit,_Ui->imageUVSizeYEdit}) QObject::connect(edit,&QLineEdit::editingFinished,this,[this]{CommitValues();});
}
ImageComponentEditor::~ImageComponentEditor() { _Texture->Changed = {}; delete _Ui; }
void ImageComponentEditor::ContextChanged() {
    _Texture->Bind(_Context.Assets,_Context.Library,_Context.Selection,eAssetType::Texture);
    _Texture->SetContextKey(_Context.Key("Image","Texture"));
}
void ImageComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    const auto v = InspectorValues::Read(static_cast<GameSproutRuntime::IImageComponent&>(component));
    _Texture->SetValue(v.Texture);
    ShowNumbers({_Ui->imageColorREdit,_Ui->imageColorGEdit,_Ui->imageColorBEdit,_Ui->imageColorAEdit,_Ui->imageUVPositionXEdit,_Ui->imageUVPositionYEdit,_Ui->imageUVSizeXEdit,_Ui->imageUVSizeYEdit},{v.Color.R,v.Color.G,v.Color.B,v.Color.A,v.UV.Position.X,v.UV.Position.Y,v.UV.Size.X,v.UV.Size.Y});
}
void ImageComponentEditor::CommitValues() {
    if (_Loading) return;
    std::vector<float> a;
    if (!ReadNumbers({_Ui->imageColorREdit,_Ui->imageColorGEdit,_Ui->imageColorBEdit,_Ui->imageColorAEdit,_Ui->imageUVPositionXEdit,_Ui->imageUVPositionYEdit,_Ui->imageUVSizeXEdit,_Ui->imageUVSizeYEdit},a)) { Fail("请输入有限的颜色与 UV 数值。"); return; }
    auto* object = _Context.Resolve();
    auto* component = object ? object->GetComponent<GameSproutRuntime::IImageComponent>() : nullptr;
    if (!component) { Fail("Image 组件已失效。"); return; }
    auto next = InspectorValues::Read(*component);
    next.Color = {a[0],a[1],a[2],a[3]}; next.UV = {{a[4],a[5]},{a[6],a[7]}};
    Commit<GameSproutRuntime::IImageComponent>(next,"修改 Image");
}
