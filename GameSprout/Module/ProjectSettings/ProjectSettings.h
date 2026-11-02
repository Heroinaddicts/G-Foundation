#pragma once
#include "Header.h"
#include <QByteArray>
#include <QString>
#include <map>
#include <set>
class IProject;
class IPlayMode;
class ISceneManager;
class QThread;

class ProjectSettings final : public IProjectSettings {
public:
    ~ProjectSettings() override;
    GameSproutRuntime::RuntimeProjectSettings GetSettings() const override { return _Settings; }
    std::uint64_t GetRevision() const override { return _Revision; }
    bool SaveSettings(const GameSproutRuntime::RuntimeProjectSettings&, std::uint64_t, std::string&) override;
    bool RegisterSettingsListener(IProjectSettingsListener*) override;
    void UnregisterSettingsListener(IProjectSettingsListener*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override { return _Ready; }
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    bool Load(std::string& error);
    bool Write(const QByteArray& bytes, std::string& error) const;
    bool CheckSourceReferences(const std::set<std::uint64_t>& removed, std::string& error) const;
    bool ValidateChange(const GameSproutRuntime::RuntimeProjectSettings&, std::string&) const;
    bool OwnerThread() const;
    void Notify();
    IProject* _Project = nullptr;
    IPlayMode* _Play = nullptr;
    ISceneManager* _Scenes = nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime = nullptr;
    QThread* _OwnerThread = nullptr;
    GameSproutRuntime::RuntimeProjectSettings _Settings;
    std::uint64_t _Revision = 0;
    std::uint64_t _NextListener = 0;
    std::map<std::uint64_t, IProjectSettingsListener*> _Listeners;
    QString _Root, _Path;
    QByteArray _DiskSnapshot;
    bool _Ready = false;
    bool _Busy = false;
};
