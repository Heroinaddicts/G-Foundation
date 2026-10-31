#ifndef __GameViewUI_h__
#define __GameViewUI_h__

#include "Header.h"

QT_BEGIN_NAMESPACE
class QWidget;
class QObject;
namespace Ui {
class GameViewUIWindow;
}
QT_END_NAMESPACE

class ISceneManager;
class IRuntimeAssets;
class GameViewport;

class GameViewUI : public IGameViewUI {
public:
    ~GameViewUI() override;

    void Show() override;
    void Hide() override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
    QWidget* _Window=nullptr;
    QObject* _Dispatch=nullptr;
    Ui::GameViewUIWindow* _Ui=nullptr;
    GameViewport* _Viewport=nullptr;
    IIDEDocker* _Docker=nullptr;
    ISceneManager* _Scenes=nullptr;
    IRuntimeAssets* _Resources=nullptr;
    bool _Attached=false;
};
#endif //__GameViewUI_h__
