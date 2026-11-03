#ifndef __LibraryManager_h__
#define __LibraryManager_h__

#include "Header.h"
#include <map>
#include "IIDEWindow.h"
#include "ITaskApi.h"
#include <QLockFile>
#include <QDateTime>

class LibraryManager : public ILibraryManager {
public:
    virtual ~LibraryManager() {}

    const std::string & GetLibraryRoot() const override { return _LibraryRoot; }
    std::vector<std::string> GetManifestGuids() const override;
    bool GetManifest(const Guid & guid, AssetImportManifest & manifest) const override;
    bool CommitImport(const std::string & stage, AssetImportManifest & manifest, std::string & error) override;
    bool RemoveAsset(const Guid & guid, std::string & error, const std::vector<LDBWriteOperation> & additionalWrites = {}) override;
    std::vector<std::string> FindDependents(const std::string & sourcePath) const override;
    std::vector<std::string> FindReferenceDependents(const Guid&) const override;
    IAsset * QueryAsset(const Guid & guid, uint64_t localID) override;
    bool GetImportValidation(const Guid & guid, std::string & validationJson) const override;
    bool SaveImportValidation(const Guid & guid, const std::string & generation,
        const std::string & validationJson) override;

    ILDBSession * GetLDBSession() const override {
        return _LDBSession;
    }

private:
    void BeforeInitialize(GameSproutModuleApi * const api) override;
    bool Initialize(GameSproutModuleApi * const api) override;
    bool Launch(GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutModuleApi * const api) override;
    void Update(GameSproutModuleApi * const api) override;
    void Release(GameSproutModuleApi * const api) override;

private:
    void StartGarbageCollection();
    void LoadImportValidation();
    bool DecodeManifest(const std::string & key, const void * data, std::size_t length,
        AssetImportManifest & manifest) const;
    bool RecoverPublications();
    bool PublishFlatImport(const std::string & stage, AssetImportManifest & manifest, std::string & error);
    Foundation::ITaskApi * _Tasks = nullptr;
    IIDEWindow * _IDE = nullptr;
    UInt64 _TaskOwner = Foundation::InvalidTaskOwnerId;
    size_t _CleanupTasks = 0;
    QDateTime _StartupTime;
    QLockFile * _Lock = nullptr;
    bool _PublicationBlocked = false;
    std::string _LibraryRoot;
    ILDBSession * _LDBSession = nullptr;
    bool _ImportValidationReady = false;
    std::map<std::string, std::string> _ImportValidations;
    struct DecodedManifest {
        std::vector<std::uint8_t> Bytes;
        AssetImportManifest Value;
    };
    // Cache parsed values, never borrowed LMDB memory. GetManifest still reads
    // the current row synchronously and compares bytes before using this value.
    mutable std::map<std::string, DecodedManifest> _DecodedManifests;
    std::map<std::string, std::vector<IAsset *>> _Loaded;
    void ClearLoaded(const std::string & guid);
    bool BuildIndexOperations(const AssetImportManifest * oldManifest,
        const AssetImportManifest * newManifest, std::vector<LDBWriteOperation> & operations);
    bool BuildTextureSearchIndexOperations(const AssetImportManifest* oldManifest,
        const AssetImportManifest* newManifest,std::vector<LDBWriteOperation>& operations);
    bool BuildGuidReferenceIndexOperations(const AssetImportManifest* oldManifest,
        const AssetImportManifest* newManifest,std::vector<LDBWriteOperation>& operations);
};

#endif //__LibraryManager_h__
