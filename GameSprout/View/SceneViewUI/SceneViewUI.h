#ifndef __SceneViewUI_h__
#define __SceneViewUI_h__

#include "Header.h"
#include <QElapsedTimer>

QT_BEGIN_NAMESPACE
class QMainWindow;
namespace Ui {
class SceneViewUIWindow;
}
QT_END_NAMESPACE
class ISceneManager;
class ISelectionManager;
class IAssetsManager;
class ILibraryManager;
class SceneViewport;

class SceneViewUI : public ISceneViewUI {
public:
    ~SceneViewUI() override;

    void Show() override;
    void Hide() override;
    SceneViewCameraState GetCameraState() const override;
    bool SetCameraState(const SceneViewCameraState& state) override;
    void ResetCamera() override;
    bool FocusSelection() override;
    bool IsRenderingAvailable() const override;
    std::string GetRenderError() const override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
    QWidget* _Window=nullptr;
    QObject* _Dispatch=nullptr;
    Ui::SceneViewUIWindow* _Ui=nullptr;
    SceneViewport* _Viewport=nullptr;
    IIDEDocker* _Docker=nullptr;
    ISceneManager* _Scenes=nullptr;
    ISelectionManager* _Selection=nullptr;
    IAssetsManager* _Assets=nullptr;
    ILibraryManager* _Library=nullptr;
    bool _Attached=false;
    QElapsedTimer _Timer;
};
#endif //__SceneViewUI_h__
