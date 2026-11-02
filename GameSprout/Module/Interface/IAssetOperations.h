#ifndef __IAssetOperations_h__
#define __IAssetOperations_h__

#include "GameSproutEditorApi.h"
#include <string>
#include <vector>

struct AssetDeleteTarget {
    std::string Guid,Path;
    bool Directory=false;
    bool operator==(const AssetDeleteTarget&) const=default;
};
struct AssetDeletePlan {std::vector<AssetDeleteTarget> Sources;};

class IAssetOperations : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IAssetOperations() {}
    // Whole source GUIDs only. Selected folders subsume selected descendants.
    virtual bool PrepareDelete(const std::vector<std::string>& guids,AssetDeletePlan&,std::string& error) const=0;
    // Revalidates the identity snapshot; references retain Guid+LocalID as Missing.
    // Earlier sources may have completed if a later source fails; error reports it.
    virtual bool Delete(const AssetDeletePlan&,std::string& error)=0;
};

#endif //__IAssetOperations_h__
