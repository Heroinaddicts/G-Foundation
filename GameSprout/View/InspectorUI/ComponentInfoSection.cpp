#include "ComponentInfoSection.h"
#include "ComponentEditor.h"
#include "ui_ComponentInfoSection.h"
#include <QHeaderView>
#include <QTreeWidget>
#include <QStringList>

namespace {
QString Number(float value) { return QString::number(value,'g',7); }
QString Vector(GameSproutRuntime::Vector2 value) { return QStringLiteral("X %1   Y %2").arg(Number(value.X),Number(value.Y)); }
QString Vector(GameSproutRuntime::Vector3 value) { return QStringLiteral("X %1   Y %2   Z %3").arg(Number(value.X),Number(value.Y),Number(value.Z)); }
QString Yes(bool value) { return value?QStringLiteral("是"):QStringLiteral("否"); }
QString TypeName(const GameSproutRuntime::IComponent& component) {
    const char* name=component.GetTypeName();
    return name?QString::fromUtf8(name):QStringLiteral("Unknown");
}
}

ComponentInfoSection::ComponentInfoSection(QWidget* parent) : QWidget(parent), _Ui(new Ui::ComponentInfoSectionForm()) {
    _Ui->setupUi(this);
    _Ui->componentProperties->header()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    _Ui->componentProperties->header()->setSectionResizeMode(1,QHeaderView::Stretch);
    QObject::connect(_Ui->componentHeaderButton,&QToolButton::toggled,this,[this](bool expanded) { SetExpanded(expanded); });
    QObject::connect(_Ui->detailsButton,&QToolButton::toggled,this,[this] { UpdateDebugVisibility(); });
    QObject::connect(_Ui->componentProperties,&QTreeWidget::itemExpanded,this,[this] { UpdateHeight(); });
    QObject::connect(_Ui->componentProperties,&QTreeWidget::itemCollapsed,this,[this] { UpdateHeight(); });
    UpdateDebugVisibility();
}
ComponentInfoSection::~ComponentInfoSection() { delete _Ui; }
bool ComponentInfoSection::IsExpanded() const { return _Ui->componentHeaderButton->isChecked(); }
void ComponentInfoSection::SetExpanded(bool expanded) {
    _Ui->componentHeaderButton->setChecked(expanded);
    _Ui->componentHeaderButton->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);
    _Ui->sectionBody->setVisible(expanded);
}
void ComponentInfoSection::SetDebugMode(bool debug) {
    _DebugMode=debug;
    UpdateDebugVisibility();
}
void ComponentInfoSection::UpdateDebugVisibility() {
    _Ui->detailsButton->setVisible(_DebugMode);
    _Ui->componentProperties->setVisible(_DebugMode && _Ui->detailsButton->isChecked());
    _Ui->unavailableEditorLabel->setVisible(!_Editor && !_DebugMode);
}
void ComponentInfoSection::Add(const QString& key,const QString& label,const QString& value,QTreeWidgetItem* parent) {
    auto* item=new QTreeWidgetItem();
    item->setData(0,Qt::UserRole,key);
    item->setText(0,label); item->setText(1,value); item->setToolTip(1,value);
    if(parent) parent->addChild(item); else _Ui->componentProperties->addTopLevelItem(item);
}
void ComponentInfoSection::Matrix(const QString& key,const QString& label,const GameSproutRuntime::Mat4& matrix) {
    auto* parent=new QTreeWidgetItem(_Ui->componentProperties);
    parent->setData(0,Qt::UserRole,key); parent->setText(0,label);parent->setText(1,QStringLiteral("4 × 4，按行显示"));
    for(std::size_t row=0;row<4;++row) {
        const QString text=QStringLiteral("%1   %2   %3   %4").arg(Number(matrix(row,0)),Number(matrix(row,1)),Number(matrix(row,2)),Number(matrix(row,3)));
        Add(key+QStringLiteral(".Row%1").arg(row),QStringLiteral("第 %1 行").arg(row+1),text,parent);
    }
}
void ComponentInfoSection::Resource(const QString& key,const QString& label,const GameSproutRuntime::ResourceReference& resource) {
    Add(key+"Guid",label+" GUID",resource.Guid.empty()?QStringLiteral("未绑定"):QString::fromStdString(resource.Guid));
    Add(key+"LocalID",label+" LocalID",resource.Guid.empty()?QStringLiteral("—"):QString::number(resource.LocalID));
}
void ComponentInfoSection::Load(GameSproutRuntime::IComponent& component) {
    const QString type=TypeName(component);
    if (_EditorType != type.toStdString()) {
        delete _Editor;
        _Editor = CreateComponentEditor(component.GetComponentType(),_Ui->editorHost);
        _EditorType = type.toStdString();
        if (_Editor) _Ui->editorLayout->addWidget(_Editor);
        _Ui->editorHost->setVisible(_Editor != nullptr);
        _Ui->detailsButton->setChecked(_Editor == nullptr);
    }
    if (_Editor) { _Editor->Bind(_Context); _Editor->Load(component); }
    QStringList expanded;
    if (_Ui->componentProperties->objectName()=="componentProperties."+type) {
        for (int index=0;index<_Ui->componentProperties->topLevelItemCount();++index) {
            auto * item=_Ui->componentProperties->topLevelItem(index);
            if (item->isExpanded()) expanded.push_back(item->data(0,Qt::UserRole).toString());
        }
    }
    setObjectName("GameObjectComponentSection."+type);
    _Ui->componentProperties->setObjectName("componentProperties."+type);
    _Ui->componentProperties->clear();
    QString caption=type;
    Add("TypeName",QStringLiteral("类型标识"),type);
    if(auto* value=dynamic_cast<GameSproutRuntime::ITransformComponent*>(&component)) {
        caption="Transform";
        Add("LocalPosition",QStringLiteral("局部位置（米）"),Vector(value->GetLocalPosition()));
        const auto rotation=value->GetLocalRotation();
        Add("LocalRotation",QStringLiteral("局部旋转（四元数 XYZW）"),QStringLiteral("%1   %2   %3   %4").arg(Number(rotation.X),Number(rotation.Y),Number(rotation.Z),Number(rotation.W)));
        Add("LocalScale",QStringLiteral("局部缩放"),Vector(value->GetLocalScale()));
        Matrix("LocalMatrix",QStringLiteral("局部矩阵"),value->GetLocalMatrix());
        Matrix("WorldMatrix",QStringLiteral("世界矩阵"),value->GetWorldMatrix());
    } else if(auto* value=dynamic_cast<GameSproutRuntime::IRectTransformComponent*>(&component)) {
        caption="Rect Transform";
        Add("AnchorMin",QStringLiteral("锚点最小值"),Vector(value->GetAnchorMin()));
        Add("AnchorMax",QStringLiteral("锚点最大值"),Vector(value->GetAnchorMax()));
        Add("Pivot",QStringLiteral("枢轴"),Vector(value->GetPivot()));
        Add("AnchoredPosition",QStringLiteral("锚定位置"),Vector(value->GetAnchoredPosition()));
        Add("SizeDelta",QStringLiteral("尺寸增量"),Vector(value->GetSizeDelta()));
    } else if(auto* value=dynamic_cast<GameSproutRuntime::IImageComponent*>(&component)) {
        caption="Image";Resource("Texture",QStringLiteral("纹理"),value->GetTexture());
        const auto color=value->GetColor();
        Add("Color",QStringLiteral("颜色（线性 RGBA）"),QStringLiteral("%1   %2   %3   %4").arg(Number(color.R),Number(color.G),Number(color.B),Number(color.A)));
        const auto uv=value->GetUVRect();
        Add("UVPosition",QStringLiteral("UV 起点"),Vector(uv.Position));Add("UVSize",QStringLiteral("UV 尺寸"),Vector(uv.Size));
    } else if(auto* value=dynamic_cast<GameSproutRuntime::ICanvasComponent*>(&component)) {
        caption="Canvas";
        Add("RenderMode",QStringLiteral("渲染模式"),value->GetRenderMode()==GameSproutRuntime::CanvasRenderMode::WorldSpace?"World Space":"Screen Space");
        Add("ReferenceResolution",QStringLiteral("参考分辨率"),Vector(value->GetReferenceResolution()));
        Add("PixelsPerUnit",QStringLiteral("布局单位 / 米"),Number(value->GetPixelsPerUnit()));
    } else if(auto* value=dynamic_cast<GameSproutRuntime::IMeshComponent*>(&component)) {
        caption="Mesh";Resource("Mesh",QStringLiteral("网格"),value->GetMesh());
        const auto bounds=value->GetLocalBounds();
        Add("BoundsCenter",QStringLiteral("局部包围盒中心"),Vector(bounds.Center));
        Add("BoundsExtents",QStringLiteral("局部包围盒半尺寸"),Vector(bounds.Extents));
    } else if(auto* value=dynamic_cast<GameSproutRuntime::IMeshRendererComponent*>(&component)) {
        caption="Mesh Renderer";
        Add("Visible",QStringLiteral("可见"),Yes(value->IsVisible()));
        Add("MaterialCount",QStringLiteral("材质槽数量"),QString::number(value->GetMaterialCount()));
        for(std::size_t slot=0;slot<value->GetMaterialCount();++slot) {
            const auto resource=value->GetMaterial(slot);
            Add(QStringLiteral("Material.%1.Guid").arg(slot),QStringLiteral("材质 %1 GUID").arg(slot),resource.Guid.empty()?QStringLiteral("未绑定"):QString::fromStdString(resource.Guid));
            Add(QStringLiteral("Material.%1.LocalID").arg(slot),QStringLiteral("材质 %1 LocalID").arg(slot),resource.Guid.empty()?QStringLiteral("—"):QString::number(resource.LocalID));
        }
    } else if(auto* value=dynamic_cast<GameSproutRuntime::ICameraComponent*>(&component)) {
        caption="Camera";
        const auto settings=value->GetSettings();
        Add("Projection",QStringLiteral("投影方式"),settings.Projection==GameSproutRuntime::CameraProjection::Orthographic?"Orthographic":"Perspective");
        Add("VerticalFovDegrees",QStringLiteral("垂直视野角（度）"),QString::number(GameSproutRuntime::RadiansToDegrees(settings.VerticalFovRadians),'f',2));
        Add("OrthographicHeight",QStringLiteral("正交垂直范围（米）"),Number(settings.OrthographicHeight));
        Add("NearPlane",QStringLiteral("近裁剪距离（米）"),Number(settings.NearPlane));
        Add("FarPlane",QStringLiteral("远裁剪距离（米）"),Number(settings.FarPlane));
        Add("CullingMask",QStringLiteral("可见层掩码"),"0x"+QString::number(settings.CullingMask,16).rightJustified(8,'0'));
        auto* object=value->GetGameObject();
        Add("MainCamera",QStringLiteral("场景主相机"),Yes(object && object->GetScene() && object->GetScene()->GetMainCamera()==value));
    } else if(auto* value=dynamic_cast<GameSproutRuntime::IAnimatorComponent*>(&component)) {
        caption="Animator";Resource("Controller",QStringLiteral("Controller"),value->GetController());
        Resource("Skeleton",QStringLiteral("骨架"),value->GetSkeleton());
        Add("NodeBindings",QStringLiteral("节点绑定"),QString::number(value->GetNodeBindings().size()));
    } else if(auto* value=dynamic_cast<GameSproutRuntime::ISkinnedMeshRendererComponent*>(&component)) {
        caption="Skinned Mesh Renderer";
        Add("AnimatorObjectID",QStringLiteral("Animator 节点 ID"),QString::number(value->GetAnimatorObjectID()));
    } else Add("Unknown",QStringLiteral("组件信息"),QStringLiteral("当前版本尚无此组件的专用属性面板。"));
    _Ui->componentHeaderButton->setText(caption);
    _Ui->componentHeaderButton->setToolTip(type);
    for (int index=0;index<_Ui->componentProperties->topLevelItemCount();++index) {
        auto * item=_Ui->componentProperties->topLevelItem(index);
        item->setExpanded(expanded.contains(item->data(0,Qt::UserRole).toString()));
    }
    UpdateHeight();
    UpdateDebugVisibility();
}
void ComponentInfoSection::UpdateHeight() {
    int count=0;
    for(int i=0;i<_Ui->componentProperties->topLevelItemCount();++i) {
        auto * item=_Ui->componentProperties->topLevelItem(i);
        count+=1+(item->isExpanded()?item->childCount():0);
    }
    const int rowHeight=_Ui->componentProperties->topLevelItemCount()?qMax(22,_Ui->componentProperties->sizeHintForRow(0)):22;
    _Ui->componentProperties->setFixedHeight(_Ui->componentProperties->header()->height()+rowHeight*count+12);
}

void ComponentInfoSection::OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event) {
    if(_Editor)_Editor->OnAnimationEvent(event);
}
