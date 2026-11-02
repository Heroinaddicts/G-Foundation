#include "IDEMenuBarFinal.h"
#include "IDEMenuFinal.h"
#include "ui_IDEWindow.h"
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QPointer>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int Calls=0;
void Invoke(){++Calls;}
int Count(QMenuBar* bar,const QString& title) {
    int count=0;for(auto* action:bar->actions())if(action->text()==title)++count;return count;
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        QMainWindow window;Ui::IDEWindowWindow form;form.setupUi(&window);
        auto* dock=new QDockWidget("Scene",&window);window.addDockWidget(Qt::LeftDockWidgetArea,dock);
        QPointer<QMenu> originalMenu=form.DockerWindowMenu;
        QPointer<QAction> originalAction=originalMenu->menuAction(),toggle=dock->toggleViewAction();
        originalMenu->addAction(toggle);window.show();QCoreApplication::processEvents();
        QPointer<QAction> ownedAction,ownedTopAction;
        QPointer<QMenu> ownedSubmenu;
        {
            IDEMenuBarFinal bar(window.menuBar());
            auto* borrowed=bar.CreateChildMenu(originalMenu->title());
            Check(borrowed&&bar.CreateChildMenu(originalMenu->title())==borrowed,"Existing menu wrapper not reused");
            Check(Count(window.menuBar(),originalMenu->title())==1,"Designer Window menu was duplicated");
            auto* entry=dynamic_cast<IDEMenuFinal*>(borrowed->CreateChildMenu("环境照明"));
            Check(entry,"Cannot extend existing Window menu");ownedAction=entry->GetAction();entry->SetClickCallback(&Invoke);
            Check(originalMenu->actions().contains(ownedAction)&&originalMenu->actions().contains(toggle),"Feature action replaced dock toggles");
            ownedAction->trigger();Check(Calls==1,"Borrowed menu child callback missing");
            auto* nested=dynamic_cast<IDEMenuFinal*>(borrowed->CreateChildMenu("Nested"));
            Check(nested&&nested->CreateChildMenu("Command"),"Nested child creation failed");ownedSubmenu=nested->GetAction()->menu();
            auto* owned=dynamic_cast<IDEMenuFinal*>(bar.CreateChildMenu("Owned Test Menu"));
            Check(owned,"Cannot create owned top-level menu");ownedTopAction=owned->GetAction();
        }
        Check(originalMenu&&originalAction&&toggle,"Borrowed wrapper destroyed a Designer menu or dock toggle");
        Check(!ownedAction&&!ownedSubmenu&&!ownedTopAction,"Owned menu children survived wrapper destruction");
        Check(window.menuBar()->actions().contains(originalAction)&&originalMenu->actions().contains(toggle),"Designer menu detached after wrapper destruction");
        Check(Count(window.menuBar(),originalMenu->title())==1&&Count(window.menuBar(),"Owned Test Menu")==0,"Wrong menu ownership after cleanup");
        const bool hidden=dock->isHidden();toggle->trigger();QCoreApplication::processEvents();
        Check(dock->isHidden()!=hidden,"Dock toggle stopped working after wrapper destruction");
        {
            IDEMenuFinal borrowed(originalMenu);Check(borrowed.GetAction()==originalAction,"Direct borrowed wrapper changed menu identity");
            Check(borrowed.CreateChildMenu("Temporary child"),"Cannot add child through direct borrowed wrapper");
        }
        Check(originalMenu&&originalAction&&toggle&&originalMenu->actions().size()==1,"Borrowed wrapper retained owned children or deleted host items");
        std::cout<<"PASS Designer menu reuse, extension callbacks, owned-child cleanup and surviving dock toggles\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
