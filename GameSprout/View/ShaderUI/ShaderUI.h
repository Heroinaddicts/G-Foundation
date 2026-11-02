#pragma once
#include "Header.h"
#include <QPointer>
class QObject;
class QMenu;
class QDialog;
class IProjectAssetsUI;
class IShaderAuthoring;
class IAssetsManager;
namespace Ui {class ShaderActionsForm;}
class ShaderUI final : public IShaderUI {
public:
    ~ShaderUI() override;
    void Show() override;
    void Hide() override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
    static void CreateShaderMenu();
    static void CreateMaterialMenu();
    void CreateAsset(bool shader);
    IProjectAssetsUI* _ProjectAssets=nullptr;
    IShaderAuthoring* _Authoring=nullptr;
    IAssetsManager* _Assets=nullptr;
    QObject* _Dispatch=nullptr;
    QMenu* _Actions=nullptr;
    Ui::ShaderActionsForm* _Ui=nullptr;
    QPointer<QDialog> _CreateDialog;
};
