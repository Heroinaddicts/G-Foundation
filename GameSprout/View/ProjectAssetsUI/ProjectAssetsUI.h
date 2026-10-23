#ifndef __ProjectAssetsUI_h__
#define __ProjectAssetsUI_h__

#include "Header.h"

QT_BEGIN_NAMESPACE
class QMainWindow;
namespace Ui {
class ProjectAssetsUIWindow;
}
QT_END_NAMESPACE

class ProjectAssetsUI : public IProjectAssetsUI {
public:
    virtual ~ProjectAssetsUI() {}

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
#endif //__ProjectAssetsUI_h__
