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
    void BeforeInitialize(GameSprout::GameSproutEditor * const editor) override;
    bool Initialize(GameSprout::GameSproutEditor * const editor) override;
    bool Launch(GameSprout::GameSproutEditor * const editor) override;
    void LaunchFinished(GameSprout::GameSproutEditor * const editor) override;
    void Update(GameSprout::GameSproutEditor * const editor) override;
    void Release(GameSprout::GameSproutEditor * const editor) override;
};
#endif //__SceneViewUI_h__
