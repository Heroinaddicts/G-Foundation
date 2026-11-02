#ifndef __ShaderAuthoring_h__
#define __ShaderAuthoring_h__

#include "Header.h"
class IProject;
class IAssetsManager;
class ILibraryManager;
class QThread;

class ShaderAuthoring : public IShaderAuthoring {
public:
    ~ShaderAuthoring() override;
    bool CreateUnlitShader(const std::string&,const std::string&,std::string&,std::string&) override;
    bool CreateBuiltinMaterial(const std::string&,const std::string&,BuiltinMaterialPreset,std::string&,std::string&) override;
    bool CreateMaterial(const std::string&,const std::string&,const GameSproutRuntime::ResourceReference&,
        std::string&,std::string&) override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi * const api) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi * const api) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi * const api) override;
    void LaunchFinished(GameSproutEditor::GameSproutModuleApi * const api) override;
    void Update(GameSproutEditor::GameSproutModuleApi * const api) override;
    void Release(GameSproutEditor::GameSproutModuleApi * const api) override;
    bool CreateSource(const std::string&,const std::string&,const char*,const std::string&,std::string&,std::string&);
    IProject* _Project=nullptr;
    IAssetsManager* _Assets=nullptr;
    ILibraryManager* _Library=nullptr;
    QThread* _OwnerThread=nullptr;
};

#endif //__ShaderAuthoring_h__
