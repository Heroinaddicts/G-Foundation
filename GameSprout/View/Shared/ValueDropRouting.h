#pragma once
#include <QObject>
#include <QTreeView>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <functional>
#include <string>
#include <utility>

// A shared native-event adapter. Feature callbacks receive their own value
// context from the hosting browser; this adapter never changes selection/model.
class ValueDropRouting final : public QObject {
public:
    using Callback=std::function<bool(const QPoint&,const QMimeData*,bool,std::string&)>;
    using ErrorCallback=std::function<void(const std::string&)>;
    ValueDropRouting(QTreeView* tree,QObject* owner,Callback callback,ErrorCallback error)
        :QObject(owner),_Callback(std::move(callback)),_Error(std::move(error)){tree->viewport()->installEventFilter(this);}
protected:
    bool eventFilter(QObject*,QEvent* event) override {
        if(event->type()!=QEvent::DragEnter&&event->type()!=QEvent::DragMove&&event->type()!=QEvent::Drop)return false;
        auto* drop=static_cast<QDropEvent*>(event);const bool execute=event->type()==QEvent::Drop;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        const QPoint position=drop->position().toPoint();
#else
        const QPoint position=drop->pos();
#endif
        const auto callback=_Callback;const auto report=_Error;std::string error;bool accepted=false;
        try {accepted=callback&&callback(position,drop->mimeData(),execute,error);}
        catch(...) {error="拖放扩展执行失败。";}
        if(accepted) {drop->setDropAction(Qt::CopyAction);drop->accept();}
        else drop->ignore();
        if(execute&&!error.empty()&&report)report(error);
        return true;
    }
private:
    Callback _Callback;ErrorCallback _Error;
};
