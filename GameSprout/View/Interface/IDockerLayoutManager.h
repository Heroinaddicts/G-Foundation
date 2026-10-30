#ifndef __IDockerLayoutManager_h__
#define __IDockerLayoutManager_h__

#include "GameSproutEditorApi.h"

class IDockerLayoutManager : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IDockerLayoutManager() {}
    // 只排列已注册面板；面板尚未全部创建时返回 false，不修改布局。
    virtual bool ResetDefaultLayout() = 0;
};

#endif //__IDockerLayoutManager_h__
