#ifndef __SceneViewUI_h__
#define __SceneViewUI_h__

#include "Header.h"

QT_BEGIN_NAMESPACE
class QMainWindow;
namespace Ui {
class SceneViewUIWindow;
}
QT_END_NAMESPACE

class SceneViewUI : public ISceneViewUI {
public:
    virtual ~SceneViewUI() {}

    void Show() override;
    void Hide() override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
};
#endif //__SceneViewUI_h__
