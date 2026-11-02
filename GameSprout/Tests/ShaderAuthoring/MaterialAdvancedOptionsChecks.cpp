#include "MaterialInspector.h"
#include "../../Module/Shared/AssetValueBinary.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <iostream>
#include <stdexcept>

static void Check(bool condition,const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
#include "../AssetsImport/MaterialSchemaChecks.h"

int main(int argc,char** argv) {
    QApplication application(argc,argv);
    try {
        CheckMaterialSchemas();
        MaterialInspector inspector(nullptr);inspector.resize(340,700);inspector.show();
        inspector.Bind(nullptr,nullptr,nullptr);inspector.SetContext("material",false);
        unsigned changes=0;inspector.Changed=[&] { ++changes; };
        Json::Value document;document["Version"]=1;document["Shader"]="GameSprout/Lit";
        inspector.Load(document);QCoreApplication::processEvents();
        auto* instancing=inspector.findChild<QCheckBox*>("enableGPUInstancingCheck");
        auto* highlights=inspector.findChild<QCheckBox*>("specularHighlightsCheck");
        auto* priority=inspector.findChild<QSpinBox*>("sortingPrioritySpin");
        Check(instancing&&highlights&&priority,"Advanced options are missing from Designer form");
        Check(!changes&&instancing->isChecked()&&highlights->isChecked()&&highlights->isVisible()&&priority->value()==0,
            "Loading defaults emitted changes or displayed wrong options");
        instancing->setChecked(false);highlights->setChecked(false);priority->setValue(17);
        Check(changes==3,"Explicit user changes did not emit editor events");
        std::string error;Json::Value captured;
        Check(inspector.Read(captured,error),error);
        Check(captured["EnableGPUInstancing"]==false&&captured["SpecularHighlights"]==false&&captured["SortingPriority"]==17,
            "Advanced options were not captured for source saving");
        QByteArray bytes;Json::Value restored;
        Check(AssetValueBinary::Encode(captured,bytes)&&AssetValueBinary::Decode(bytes,restored)&&restored==captured,
            "Material engine options did not survive the current binary artifact encoding");
        inspector.Load(restored);
        Check(changes==3&&!instancing->isChecked()&&!highlights->isChecked()&&priority->value()==17,
            "Saved material values did not reload silently");
        inspector.SetContext("embedded",true);inspector.Load(restored);
        Check(!instancing->isEnabled()&&!highlights->isEnabled()&&!priority->isEnabled(),
            "Embedded material advanced options became editable");
        inspector.SetContext("material",false);restored["Shader"]="GameSprout/Preview";inspector.Load(restored);
        Check(!highlights->isVisible()&&instancing->isEnabled(),"Lit-only option leaked into preview material");
        restored["SortingPriority"]="not-an-integer";inspector.Load(restored);
        Check(!inspector.Read(captured,error)&&!priority->isEnabled(),"Invalid authored render option was accepted by the Inspector");
        std::cout<<"PASS material engine options, typed validation, Designer events, embedded read-only and binary roundtrip\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL "<<error.what()<<'\n';return 1;
    }
}
