#pragma once

#include <QDir>
#include <QString>
#include <QFileSystemWatcher>
#include <QDebug>
#include <unordered_map>

#include "IAssetsManager.h"
#include "IProject.h"
#include "IConsole.h"
#include "IGuidCreator.h"
#include "IIDEWindow.h"

#include "SafeSystem.hpp"
#include "SafeString.hpp"
UsingGameSprout;

inline extern GameSproutApi * g_GameSproutApi = nullptr;
inline extern IProject * g_IProject = nullptr;
inline extern IConsole * g_IConsole = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;

template<typename T>
class Asset : public IAsset {
public:
    virtual ~Asset() {}

    IAssetMeta * GetMeta() const override {
        Q_ASSERT(nullptr != _Meta);
        return _Meta;
    }

    void SetMeta(T * meta) {
        Q_ASSERT(nullptr == _Meta);
        if (nullptr == _Meta) {
            _Meta = meta;
        }

    }

private:
    T * _Meta = nullptr;
};

