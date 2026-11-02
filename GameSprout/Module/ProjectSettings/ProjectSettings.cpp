#include "ProjectSettings.h"
#include "ProjectSettingsFormat.h"
#include "IProject.h"
#include "IPlayMode.h"
#include "ISceneManager.h"
#include <QDebug>
#include <QFile>
#include <QThread>
#include <limits>
#include <algorithm>

namespace {
class SettingsTransaction {
public:
    explicit SettingsTransaction(bool& busy) : _Busy(busy) { _Busy = true; }
    ~SettingsTransaction() { _Busy = false; }
private:
    bool& _Busy;
};
}
ProjectSettings::~ProjectSettings() { Release(nullptr); }
void ProjectSettings::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    g_Self = this; g_GameSproutApi = api;
    auto* editor = api ? api->_EditorApi : nullptr;
    _Project = editor ? editor->GetGameSproutModule<IProject>("Project") : nullptr;
    _Play = editor ? editor->GetGameSproutModule<IPlayMode>("PlayMode") : nullptr;
    _Scenes = editor ? editor->GetGameSproutModule<ISceneManager>("SceneManager") : nullptr;
    _Runtime = editor ? editor->_Runtime : nullptr;
}
bool ProjectSettings::OwnerThread() const { return _OwnerThread && QThread::currentThread() == _OwnerThread; }
bool ProjectSettings::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    if (_Ready) return true;
    _OwnerThread = QThread::currentThread();
    if (!_Project || !_Runtime) { qCritical("ProjectSettings requires Project and the shared Runtime."); return false; }
    std::string error;
    if (!Load(error)) { qCritical().noquote() << QString::fromStdString(error); return false; }
    _Revision = 1; _Ready = true; return true;
}
bool ProjectSettings::ValidateChange(const GameSproutRuntime::RuntimeProjectSettings& data, std::string& error) const {
    if (!GameSproutRuntime::ValidateProjectSettings(data, error)) return false;
    if (data.NextTagID < _Settings.NextTagID) { error = "NextTagID 不能倒退，已删除的 Tag ID 不可复用。"; return false; }
    std::set<std::uint64_t> oldIDs, candidateIDs, removed;
    for (const auto& tag : _Settings.Tags) oldIDs.insert(tag.ID);
    for (const auto& tag : data.Tags) {
        candidateIDs.insert(tag.ID);
        if (!oldIDs.count(tag.ID) && tag.ID < _Settings.NextTagID) {
            error = "禁止复用已分配过的 Tag ID：" + std::to_string(tag.ID); return false;
        }
    }
    std::set_difference(oldIDs.begin(), oldIDs.end(), candidateIDs.begin(), candidateIDs.end(), std::inserter(removed, removed.end()));
    if (_Scenes && !removed.empty()) for (const auto id : _Scenes->GetReferencedTagIDs()) {
        if (removed.count(id)) {
            error = "Tag 仍被场景文档或撤销历史引用，不能删除：" + std::to_string(id); return false;
        }
    }
    return CheckSourceReferences(removed, error) && _Runtime->CanApplyProjectSettings(data, error);
}
bool ProjectSettings::SaveSettings(const GameSproutRuntime::RuntimeProjectSettings& data, std::uint64_t expected, std::string& error) {
    error.clear();
    if (!OwnerThread() || !_Ready || !_Runtime || _Busy || _Runtime->IsExecuting()) {
        error = "项目设置只能在所属主线程的空闲编辑阶段修改。"; return false;
    }
    if (_Play && _Play->GetState().State != EditorPlayState::Edit) { error = "运行期间不能修改项目设置。"; return false; }
    if (expected != _Revision) { error = "项目设置已被修改，请重新加载后再保存。"; return false; }
    if (_Revision == std::numeric_limits<std::uint64_t>::max()) { error = "项目设置修订号已耗尽。"; return false; }
    SettingsTransaction transaction(_Busy);
    if (!ValidateChange(data, error)) return false;
    auto candidate = data;
    const auto bytes = ProjectSettingsFormat::Encode(candidate);
    QFile disk(_Path);
    if (!disk.open(QIODevice::ReadOnly) || disk.size() > 64 * 1024 * 1024 ||
        disk.readAll() != _DiskSnapshot || disk.error() != QFileDevice::NoError) {
        error = "项目设置文件已被外部修改或无法读取，未覆盖磁盘内容。"; return false;
    }
    disk.close();
    if (bytes == ProjectSettingsFormat::Encode(_Settings)) return true;
    if (!Write(bytes, error)) return false;
    // Preflight and publication are synchronous on the same owner thread; no
    // Runtime updates or external listener callbacks run between these calls.
    if (!_Runtime->SetProjectSettings(candidate, error)) {
        std::string restoreError;
        if (!Write(_DiskSnapshot, restoreError)) error += "; 恢复磁盘快照失败：" + restoreError;
        return false;
    }
    _Settings = std::move(candidate); _DiskSnapshot = bytes; ++_Revision;
    Notify(); return true;
}
bool ProjectSettings::RegisterSettingsListener(IProjectSettingsListener* listener) {
    if (!OwnerThread() || !listener || _NextListener == std::numeric_limits<std::uint64_t>::max()) return false;
    for (const auto& entry : _Listeners) if (entry.second == listener) return false;
    _Listeners.emplace(++_NextListener, listener); return true;
}
void ProjectSettings::UnregisterSettingsListener(IProjectSettingsListener* listener) {
    if (!OwnerThread()) return;
    for (auto current = _Listeners.begin(); current != _Listeners.end();)
        if (current->second == listener) current = _Listeners.erase(current); else ++current;
}
void ProjectSettings::Notify() {
    const auto snapshot = _Settings;
    const auto last = _NextListener;
    auto current = _Listeners.begin();
    while (current != _Listeners.end() && current->first <= last) {
        const auto id = current->first;
        try { current->second->OnProjectSettingsChanged(snapshot, _Revision); }
        catch (...) { qWarning("ProjectSettings listener threw an exception."); }
        current = _Listeners.upper_bound(id);
    }
}
void ProjectSettings::Release(GameSproutEditor::GameSproutModuleApi*) {
    _Listeners.clear(); _Project = nullptr; _Play = nullptr; _Scenes = nullptr; _Runtime = nullptr;
    _OwnerThread = nullptr; _Ready = false; _Busy = false; _Revision = 0; _NextListener = 0;
    _Root.clear(); _Path.clear(); _DiskSnapshot.clear(); _Settings = {};
    if (g_Self == this) g_Self = nullptr; g_GameSproutApi = nullptr;
}
