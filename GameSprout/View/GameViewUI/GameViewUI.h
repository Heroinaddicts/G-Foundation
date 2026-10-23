#ifndef __GameViewUI_h__
#define __GameViewUI_h__

#include "Header.h"

QT_BEGIN_NAMESPACE
class QMainWindow;
namespace Ui {
class GameViewUIWindow;
}
QT_END_NAMESPACE

class GameViewUI : public IGameViewUI {
public:
    virtual ~GameViewUI() {}

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
#endif //__GameViewUI_h__
