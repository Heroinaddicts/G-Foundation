#include "SceneObjectUI.h"
#include "IInspectorUI.h"
#include "SceneObjectPanel.h"
#include "TransformComponentEditor.h"
#include "RectTransformComponentEditor.h"
#include "CanvasComponentEditor.h"
#include "ImageComponentEditor.h"
#include "MeshComponentEditor.h"
#include "MeshRendererComponentEditor.h"
#include "CameraComponentEditor.h"
void SceneObjectUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    _Inspector = editor ? editor->GetGameSproutView<IInspectorUI>("InspectorUI") : nullptr;
}
bool SceneObjectUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    _Registered = _Inspector && _Inspector->RegisterExtension(this); return _Registered;
}
void SceneObjectUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if (_Registered && _Inspector) _Inspector->UnregisterExtension(this);
    _Registered = false; _Inspector = nullptr;
}
void SceneObjectUI::Show() { if (_Inspector) _Inspector->Show(); }
bool SceneObjectUI::CanInspect(const InspectorServices&, const InspectorTarget& target) const { return target.Scene.ObjectID != 0; }
InspectorPanel* SceneObjectUI::CreatePanel(const InspectorServices& services, QWidget* parent) { return new SceneObjectPanel(services, parent); }
ComponentEditor* SceneObjectUI::CreateComponentEditor(const std::type_info& type, QWidget* parent) {
    if (type == typeid(GameSproutRuntime::ITransformComponent)) return new TransformComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::IRectTransformComponent)) return new RectTransformComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::ICanvasComponent)) return new CanvasComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::IImageComponent)) return new ImageComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::IMeshComponent)) return new MeshComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::IMeshRendererComponent)) return new MeshRendererComponentEditor(parent);
    if (type == typeid(GameSproutRuntime::ICameraComponent)) return new CameraComponentEditor(parent);
    return nullptr;
}
