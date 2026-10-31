#ifndef __ILibraryManager_h__
#define __ILibraryManager_h__

#include "GameSproutEditorApi.h"
#include "ILocalDatabase.h"
#include "IAssetsPipeline.h"

class ILibraryManager : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~ILibraryManager() {}
    virtual ILDBSession * GetLDBSession() const = 0;
    virtual const std::string & GetLibraryRoot() const = 0;
    virtual std::vector<std::string> GetManifestGuids() const = 0;
    virtual bool GetManifest(const Guid & guid, AssetImportManifest & manifest) const = 0;
    // Stage lives below Library/Staging. Final paths are Library/Artifact/<Guid>_<LocalID>.gsasset.
    // Publication journals restore old files after an uncommitted replacement or crash.
    virtual bool CommitImport(const std::string & stage, AssetImportManifest & manifest, std::string & error) = 0;
    // 主线程同步执行；additionalWrites 与产物/依赖索引删除在同一事务提交。
    // AssetsManager 用它原子写备份、删除资源活动记录。
    virtual bool RemoveAsset(const Guid & guid, std::string & error, const std::vector<LDBWriteOperation> & additionalWrites = {}) = 0;
    virtual std::vector<std::string> FindDependents(const std::string & sourcePath) const = 0;
    // Borrowed until the next commit/removal for this GUID or Library release.
    virtual IAsset * QueryAsset(const Guid & guid, uint64_t localID) = 0;

    // 主线程同步调用。快速校验缓存独立于 Manifest，缺失时走完整导入校验。
    // Get 只读取启动时加载的内存快照，不开启新的数据库事务。
    virtual bool GetImportValidation(const Guid &, std::string & validationJson) const {
        validationJson.clear();
        return false;
    }
    // 只接受当前已发布 Generation 的校验结果；相同内容不重复写库。
    virtual bool SaveImportValidation(const Guid &, const std::string &,
        const std::string &) { return false; }
};

#endif //__ILibraryManager_h__
