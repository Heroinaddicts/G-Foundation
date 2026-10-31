#ifndef __IHierarchyUI_h__
#define __IHierarchyUI_h__

#include "GameSproutEditorApi.h"
#include <cstdint>

class IHierarchyUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IHierarchyUI() {}
    // 当前运行节点身份，0 表示未选择；不持有场景对象指针。
    virtual std::uint64_t GetSelectedObjectID() const = 0;
};

#endif //__IHierarchyUI_h__
