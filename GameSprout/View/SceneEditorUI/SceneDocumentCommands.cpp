#include "SceneEditorUI.h"
#include <QPointer>
#include <algorithm>

bool SceneEditorUI::RegisterDocumentHandler(const void* owner,const fDocumentActive& active,
    const fDocumentSave& save,const fDocumentClose& close) {
    if(!owner||!active||!save||!close||std::any_of(_DocumentHandlers.begin(),_DocumentHandlers.end(),
        [owner](const DocumentHandler& value){return value.Owner==owner;}))return false;
    _DocumentHandlers.push_back({++_NextDocumentHandler,owner,active,save,close});RefreshActions();return true;
}
void SceneEditorUI::UnregisterDocumentHandler(const void* owner) {
    std::erase_if(_DocumentHandlers,[owner](const DocumentHandler& value){return value.Owner==owner;});RefreshActions();
}
bool SceneEditorUI::ReadDocumentHandler(DocumentHandler& output) const {
    const auto handlers=_DocumentHandlers;
    for(const auto& handler:handlers) {
        const auto registered=[&]{return std::any_of(_DocumentHandlers.begin(),_DocumentHandlers.end(),
            [&](const DocumentHandler& value){return value.ID==handler.ID;});};
        if(!registered()||!handler.Active()||!registered())continue;
        output=handler;return true;
    }
    return false;
}
bool SceneEditorUI::CloseActiveDocument(const std::function<void()>& continuation) {
    DocumentHandler handler;if(!ReadDocumentHandler(handler))return false;
    const QPointer<QObject> alive(_Dispatch);
    handler.Close([this,alive,id=handler.ID,continuation] {
        if(!alive||!std::any_of(_DocumentHandlers.begin(),_DocumentHandlers.end(),
            [id](const DocumentHandler& value){return value.ID==id;}))return;
        DocumentHandler active;
        if(ReadDocumentHandler(active))return;
        if(continuation)continuation();
    });
    return true;
}
