#pragma once
#include "GameSproutEditorApi.h"

class IPrefabStageUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~IPrefabStageUI()=default;
    // Interactive routes resolve dirty documents before switching sources.
    virtual void OpenPrefab(const std::string& guid)=0;
    virtual void ReturnToMainScene()=0;
};
