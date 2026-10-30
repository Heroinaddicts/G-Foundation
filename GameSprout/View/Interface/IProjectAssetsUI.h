#ifndef __IProjectAssetsUI_h__
#define __IProjectAssetsUI_h__

#include "GameSproutEditorApi.h"
#include "ISelectionManager.h"

class IProjectAssetsUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IProjectAssetsUI() {}
    // 主线程；返回身份快照，不借用资源对象。
    virtual std::vector<ProjectAssetSelection> GetSelection() const = 0;
    virtual bool NavigateToDirectory(const std::string & path) = 0;
};

#endif //__IProjectAssetsUI_h__
