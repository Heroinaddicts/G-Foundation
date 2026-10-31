#include "SceneViewUI.h"
#include "SceneViewport.h"
#include "SceneViewportEditing.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include "IRuntimeAssets.h"
#include "ui_SceneViewUI.h"
#include <QApplication>
#include <algorithm>
#include <cmath>
#include <limits>

SceneViewUI::~SceneViewUI(){Release(nullptr);}
void SceneViewUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    g_Self=this;g_GameSproutEditor=editor;
    if(!editor)return;
    GetGameSproutView(editor,g_IIDEWindow,IDEWindow);
    GetGameSproutModule(editor,_Scenes,SceneManager);
    GetGameSproutModule(editor,_Selection,SelectionManager);
    GetGameSproutModule(editor,_RuntimeAssets,RuntimeAssets);
}
bool SceneViewUI::Initialize(GameSproutEditor::GameSproutViewApi* editor) {
    if(_Window)return true;
    delete _Dispatch;_Dispatch=nullptr;delete _Ui;_Ui=nullptr;
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Dispatch=new QObject();_Window=new QWidget();_Ui=new Ui::SceneViewUIWindow();_Ui->setupUi(_Window);
    _Viewport=_Ui->sceneViewport;
    _Viewport->StatusChanged=[this](const std::string& message){if(_Ui&&_Window)_Ui->statusLabel->setText(QString::fromStdString(message));};
    _Viewport->FocusRequested=[this]{return FocusSelection();};
    _Viewport->Initialize(editor?editor->_GRHIApi:nullptr,_Scenes?_Scenes->GetRuntime():nullptr,_RuntimeAssets?_RuntimeAssets->GetResourceProvider():nullptr);
    _Viewport->BindEditingServices(_Scenes,_Selection);
    _Viewport->Editing().ToolChanged=[this](SceneEditingTool tool){
        if(!_Ui||!_Window)return;
        QToolButton* button=tool==SceneEditingTool::View?_Ui->viewToolButton:tool==SceneEditingTool::Move?_Ui->moveToolButton:tool==SceneEditingTool::Rotate?_Ui->rotateToolButton:_Ui->scaleToolButton;
        button->setChecked(true);
    };
    QObject::connect(_Ui->viewToolButton,&QToolButton::clicked,_Dispatch,[this]{_Viewport->Editing().SetTool(SceneEditingTool::View);});
    QObject::connect(_Ui->moveToolButton,&QToolButton::clicked,_Dispatch,[this]{_Viewport->Editing().SetTool(SceneEditingTool::Move);});
    QObject::connect(_Ui->rotateToolButton,&QToolButton::clicked,_Dispatch,[this]{_Viewport->Editing().SetTool(SceneEditingTool::Rotate);});
    QObject::connect(_Ui->scaleToolButton,&QToolButton::clicked,_Dispatch,[this]{_Viewport->Editing().SetTool(SceneEditingTool::Scale);});
    QObject::connect(_Ui->spaceCombo,qOverload<int>(&QComboBox::currentIndexChanged),_Dispatch,[this](int index){_Viewport->Editing().SetSpace(index==0?SceneGizmoSpace::World:SceneGizmoSpace::Local);});
    QObject::connect(_Ui->pivotCombo,qOverload<int>(&QComboBox::currentIndexChanged),_Dispatch,[this](int index){_Viewport->Editing().SetPivot(index==0?SceneGizmoPivot::Pivot:SceneGizmoPivot::Center);});
    const auto snap=[this]{_Viewport->Editing().SetSnap(_Ui->snapCheck->isChecked(),float(_Ui->moveSnapSpin->value()),float(_Ui->rotateSnapSpin->value()),float(_Ui->scaleSnapSpin->value()));};
    QObject::connect(_Ui->snapCheck,&QCheckBox::toggled,_Dispatch,[snap](bool){snap();});
    QObject::connect(_Ui->moveSnapSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),_Dispatch,[snap](double){snap();});
    QObject::connect(_Ui->rotateSnapSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),_Dispatch,[snap](double){snap();});
    QObject::connect(_Ui->scaleSnapSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),_Dispatch,[snap](double){snap();});
    snap();
    QObject::connect(_Ui->resetCameraButton,&QToolButton::clicked,_Dispatch,[this]{ResetCamera();});
    QObject::connect(_Ui->focusSelectionButton,&QToolButton::clicked,_Dispatch,[this]{FocusSelection();});
    QObject::connect(_Ui->retryRenderButton,&QToolButton::clicked,_Dispatch,[this]{if(_Viewport)_Viewport->RetryRendering();});
    QObject::connect(_Window,&QObject::destroyed,_Dispatch,[this]{_Window=nullptr;_Viewport=nullptr;_Docker=nullptr;_Attached=false;});
    return true;
}
bool SceneViewUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Window||!g_IIDEWindow)return false;
    _Docker=g_IIDEWindow->CreateDocker(QStringLiteral("Scene"));
    if(!_Docker||!_Docker->AttachWidget(_Window))return false;
    _Attached=true;_Docker->SetTitle(QStringLiteral("Scene"));_Timer.start();return true;
}
void SceneViewUI::LaunchFinished(GameSproutEditor::GameSproutViewApi*){}
void SceneViewUI::Show(){if(_Window)_Window->show();if(_Docker)_Docker->Activate();}
void SceneViewUI::Hide(){if(_Window)_Window->hide();}
void SceneViewUI::Update(GameSproutEditor::GameSproutViewApi*) {
    const float elapsed=_Timer.isValid()?static_cast<float>(_Timer.nsecsElapsed()/1000000000.0):0.0f;_Timer.start();
    if(_Viewport){_Viewport->BindEditingServices(_Scenes,_Selection);_Viewport->RenderScene(_Scenes?_Scenes->GetActiveScene():nullptr,_Ui->gridCheck->isChecked(),elapsed);}
}
void SceneViewUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_Viewport){_Viewport->Editing().ToolChanged={};_Viewport->FocusRequested={};_Viewport->StatusChanged={};_Viewport->Shutdown();}
    delete _Dispatch;_Dispatch=nullptr;
    if(!_Attached)delete _Window;
    _Window=nullptr;_Viewport=nullptr;_Docker=nullptr;_Attached=false;
    delete _Ui;_Ui=nullptr;_Scenes=nullptr;_Selection=nullptr;_RuntimeAssets=nullptr;_Timer.invalidate();
    if(g_Self==this)g_Self=nullptr;
}
SceneViewCameraState SceneViewUI::GetCameraState() const{return _Viewport?_Viewport->Camera().GetState():SceneViewCameraState{};}
bool SceneViewUI::SetCameraState(const SceneViewCameraState& state){if(!_Viewport)return false;_Viewport->Editing().Cancel();return _Viewport->Camera().SetState(state);}
void SceneViewUI::ResetCamera(){if(_Viewport){_Viewport->Editing().Cancel();_Viewport->Camera().Reset();}}
bool SceneViewUI::IsRenderingAvailable() const{return _Viewport&&_Viewport->IsRenderingAvailable();}
std::string SceneViewUI::GetRenderError() const{return _Viewport?_Viewport->GetRenderError():"Scene viewport is not initialized";}
bool SceneViewUI::FocusSelection() {
    if(!_Viewport||!_Scenes||!_Selection)return false;
    _Viewport->Editing().Cancel();
    auto* runtime=_Scenes->GetRuntime();auto* scene=_Scenes->GetActiveScene();const auto selection=_Selection->GetSceneObjectSelection();
    if(!runtime||!scene||selection.SceneID!=scene->GetInstanceID())return false;
    auto* object=runtime->QueryGameObject(selection.ObjectID);if(!object||object->GetScene()!=scene)return false;
    const float large=std::numeric_limits<float>::max();
    GameSproutRuntime::Vector3 minimum{large,large,large},maximum{-large,-large,-large};
    bool found=false,foundSizedBounds=false;
    std::vector<GameSproutRuntime::IGameObject*> pending{object};
    while(!pending.empty()) {
        auto* node=pending.back();pending.pop_back();
        // Renderer supplies bounds from the current skin pose; the editor does no skinning.
        GameSproutRuntime::Bounds posedBounds;
        if (_Viewport->Renderer() && node->GetComponent<GameSproutRuntime::IMeshComponent>() &&
            _Viewport->Renderer()->GetWorldBounds(scene,node->GetInstanceID(),posedBounds)) {
            const auto low=posedBounds.Center-posedBounds.Extents, high=posedBounds.Center+posedBounds.Extents;
            if (!foundSizedBounds) {minimum=low;maximum=high;}
            else {minimum={std::min(minimum.X,low.X),std::min(minimum.Y,low.Y),std::min(minimum.Z,low.Z)};
                maximum={std::max(maximum.X,high.X),std::max(maximum.Y,high.Y),std::max(maximum.Z,high.Z)};}
            found=true;foundSizedBounds=true;
            for(std::size_t index=0;index<node->GetChildCount();++index)pending.push_back(node->GetChild(index));
            continue;
        }
        const auto matrix=node->GetTransform()->GetWorldMatrix();
        // 没有几何体的节点只提供 pivot；存在非零 Mesh bounds 时忽略这些 pivot。
        GameSproutRuntime::Bounds bounds;
        if(auto* mesh=node->GetComponent<GameSproutRuntime::IMeshComponent>()) {
            const auto loaded=mesh->GetLocalBounds();
            if(std::isfinite(loaded.Center.X)&&std::isfinite(loaded.Center.Y)&&std::isfinite(loaded.Center.Z)&&
               std::isfinite(loaded.Extents.X)&&std::isfinite(loaded.Extents.Y)&&std::isfinite(loaded.Extents.Z)&&
               loaded.Extents.X>=0&&loaded.Extents.Y>=0&&loaded.Extents.Z>=0) bounds=loaded;
        }
        GameSproutRuntime::Vector3 nodeMinimum{large,large,large},nodeMaximum{-large,-large,-large};
        bool nodeFound=false;
        for(int corner=0;corner<8;++corner) {
            const auto local=bounds.Center+GameSproutRuntime::Vector3{corner&1?bounds.Extents.X:-bounds.Extents.X,corner&2?bounds.Extents.Y:-bounds.Extents.Y,corner&4?bounds.Extents.Z:-bounds.Extents.Z};
            const auto point=matrix*GameSproutRuntime::Vector4(local.X,local.Y,local.Z,1);
            if(!std::isfinite(point.X)||!std::isfinite(point.Y)||!std::isfinite(point.Z))continue;
            nodeMinimum={std::min(nodeMinimum.X,point.X),std::min(nodeMinimum.Y,point.Y),std::min(nodeMinimum.Z,point.Z)};
            nodeMaximum={std::max(nodeMaximum.X,point.X),std::max(nodeMaximum.Y,point.Y),std::max(nodeMaximum.Z,point.Z)};
            nodeFound=true;
        }
        if(nodeFound) {
            const bool sized=nodeMinimum.X<nodeMaximum.X||nodeMinimum.Y<nodeMaximum.Y||nodeMinimum.Z<nodeMaximum.Z;
            if(sized&&!foundSizedBounds) {minimum=nodeMinimum;maximum=nodeMaximum;found=true;}
            else if(sized||!foundSizedBounds) {
                minimum={std::min(minimum.X,nodeMinimum.X),std::min(minimum.Y,nodeMinimum.Y),std::min(minimum.Z,nodeMinimum.Z)};
                maximum={std::max(maximum.X,nodeMaximum.X),std::max(maximum.Y,nodeMaximum.Y),std::max(maximum.Z,nodeMaximum.Z)};
                found=true;
            }
            foundSizedBounds=foundSizedBounds||sized;
        }
        for(std::size_t index=0;index<node->GetChildCount();++index)pending.push_back(node->GetChild(index));
    }
    if(!found)return false;
    const float aspect = _Viewport->height() > 0 ? static_cast<float>(_Viewport->width()) / _Viewport->height() : 1.0f;
    return _Viewport->Camera().Focus(minimum*0.5f+maximum*0.5f,(maximum*0.5f-minimum*0.5f).Length(),aspect);
}
