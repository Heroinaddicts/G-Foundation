#include "IDEDockerManager.h"
#include "IDEDockerDragController.h"
#include "IDEDockerFinal.h"
#include "IDEDockerFloatingHost.h"
#include <QDockWidget>
#include <QLayout>
#include <QMainWindow>
#include <QTabBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
// 布局事务只保存面板身份、标签组和分割比例。View/原生渲染窗口不被重新创建。
struct LayoutNode {
    QList<IDEDockerFinal*> Tabs;
    IDEDockerFinal* Active = nullptr;
    QRect Rect;
    QRect Planned;
    QSize Minimum;
    int TabHeight = 0;
    int First = -1, Second = -1;
    Qt::Orientation Axis = Qt::Horizontal;
    float Ratio = 0.5f;
    int InsertedExtent = 0;
    bool InsertedFirst = false;
    bool IsLeaf() const { return First < 0 && Second < 0; }
};
class LayoutTree {
public:
    std::vector<LayoutNode> Nodes;
    int Root = -1;
    int Leaf(const QList<IDEDockerFinal*>& tabs, IDEDockerFinal* active, const QRect& rect, int tabHeight = 0) {
        LayoutNode node;node.Tabs=tabs;node.Active=active;node.Rect=rect;node.TabHeight=tabHeight;
        Nodes.push_back(node);return static_cast<int>(Nodes.size()-1);
    }
    int Branch(int first,int second,Qt::Orientation axis,float ratio) {
        LayoutNode node;node.First=first;node.Second=second;node.Axis=axis;node.Ratio=ratio;
        node.Rect=Nodes[first].Rect.united(Nodes[second].Rect);
        Nodes.push_back(node);return static_cast<int>(Nodes.size()-1);
    }
    int Build(const std::vector<int>& leaves) {
        if(leaves.empty())return -1;
        if(leaves.size()==1)return leaves.front();
        std::vector<int> first,second;
        Qt::Orientation axis=Qt::Horizontal;
        bool split=false;
        // Qt 的停靠布局是矩形分割；寻找没有穿过任何标签组的完整切线。
        for(Qt::Orientation candidate:{Qt::Horizontal,Qt::Vertical}) {
            for(int leaf:leaves) {
                const int cut=candidate==Qt::Horizontal?Nodes[leaf].Rect.right():Nodes[leaf].Rect.bottom();
                first.clear();second.clear();bool crossing=false;
                for(int index:leaves) {
                    const QRect rect=Nodes[index].Rect;
                    const int start=candidate==Qt::Horizontal?rect.left():rect.top();
                    const int end=candidate==Qt::Horizontal?rect.right():rect.bottom();
                    if(end<=cut)first.push_back(index);
                    else if(start>cut)second.push_back(index);
                    else {crossing=true;break;}
                }
                if(!crossing&&!first.empty()&&!second.empty()){split=true;axis=candidate;break;}
            }
            if(split)break;
        }
        if(!split) {
            // 尚未显示/正在恢复的 Qt 几何可能无效；仍保留全部面板并构造有效分割。
            first.assign(leaves.begin(),leaves.begin()+leaves.size()/2);
            second.assign(leaves.begin()+leaves.size()/2,leaves.end());
        }
        const int left=Build(first),right=Build(second);
        const float a=axis==Qt::Horizontal?Nodes[left].Rect.width():Nodes[left].Rect.height();
        const float b=axis==Qt::Horizontal?Nodes[right].Rect.width():Nodes[right].Rect.height();
        return Branch(left,right,axis,a>0&&b>0?a/(a+b):0.5f);
    }
    int Find(IDEDockerFinal* docker,int node) const {
        if(node<0)return -1;
        if(Nodes[node].IsLeaf())return Nodes[node].Tabs.contains(docker)?node:-1;
        const int found=Find(docker,Nodes[node].First);
        return found>=0?found:Find(docker,Nodes[node].Second);
    }
    int Remove(IDEDockerFinal* docker,int node) {
        if(node<0)return -1;
        if(Nodes[node].IsLeaf()) {
            auto& item=Nodes[node];
            item.Tabs.removeAll(docker);
            if(item.Tabs.empty())return -1;
            if(item.Active==docker)item.Active=item.Tabs.front();
            if(item.Tabs.size()==1)item.TabHeight=0;
            return node;
        }
        const int first=Remove(docker,Nodes[node].First),second=Remove(docker,Nodes[node].Second);
        if(first<0)return second;
        if(second<0)return first;
        Nodes[node].First=first;Nodes[node].Second=second;return node;
    }
    int Parent(int child,int node) const {
        if(node<0||Nodes[node].IsLeaf())return -1;
        if(Nodes[node].First==child||Nodes[node].Second==child)return node;
        const int found=Parent(child,Nodes[node].First);
        return found>=0?found:Parent(child,Nodes[node].Second);
    }
    void Collect(int node,QList<IDEDockerFinal*>& panes) const {
        if(node<0)return;
        if(Nodes[node].IsLeaf()){panes.append(Nodes[node].Active);return;}
        Collect(Nodes[node].First,panes);Collect(Nodes[node].Second,panes);
    }
    void Groups(int node,QList<QList<IDEDockerFinal*>>& groups) const {
        if(node<0)return;
        if(Nodes[node].IsLeaf()){groups.append(Nodes[node].Tabs);return;}
        Groups(Nodes[node].First,groups);Groups(Nodes[node].Second,groups);
    }
    int Cover(const QList<IDEDockerFinal*>& panes,int node) const {
        if(node<0||Nodes[node].IsLeaf())return node;
        bool first=false,second=false;
        for(IDEDockerFinal* pane:panes) {
            first|=Find(pane,Nodes[node].First)>=0;
            second|=Find(pane,Nodes[node].Second)>=0;
        }
        if(first&&second)return node;
        if(first)return Cover(panes,Nodes[node].First);
        if(second)return Cover(panes,Nodes[node].Second);
        return -1;
    }
    int Replace(int node,int oldNode,int replacement) {
        if(node==oldNode)return replacement;
        if(node<0||Nodes[node].IsLeaf())return node;
        const int first=Replace(Nodes[node].First,oldNode,replacement);
        const int second=Replace(Nodes[node].Second,oldNode,replacement);
        Nodes[node].First=first;Nodes[node].Second=second;return node;
    }
    QDockWidget* Anchor(int node) const {
        while(!Nodes[node].IsLeaf())node=Nodes[node].First;
        return Nodes[node].Tabs.front()->GetDockWidget();
    }
    void Restore(QMainWindow* window,int node) const {
        const LayoutNode& item=Nodes[node];
        if(item.IsLeaf()) {
            auto* anchor=item.Tabs.front()->GetDockWidget();anchor->show();
            for(IDEDockerFinal* tab:item.Tabs)if(tab!=item.Tabs.front()) {
                window->tabifyDockWidget(anchor,tab->GetDockWidget());tab->GetDockWidget()->show();
            }
            item.Active->GetDockWidget()->raise();return;
        }
        auto* first=Anchor(item.First);auto* second=Anchor(item.Second);
        window->splitDockWidget(first,second,item.Axis);
        Restore(window,item.First);Restore(window,item.Second);
    }
    QSize CalculateMinimum(int node, int separator) {
        auto& item=Nodes[node];
        if(item.IsLeaf()) {
            QSize minimum{1,1};
            for(auto* tab:item.Tabs) {
                auto* dock=tab->GetDockWidget();
                QSize size=dock->minimumSize().expandedTo(dock->minimumSizeHint());
                if(item.Tabs.size()==1&&dock->titleBarWidget()&&dock->titleBarWidget()->maximumHeight()==0) {
                    // The sole member regains its title after leaving a native tab group.
                    size.rheight()+=TitleHeight;
                }
                minimum=minimum.expandedTo(size);
            }
            if(item.Tabs.size()>1)minimum.rheight()+=item.TabHeight;
            return item.Minimum=minimum;
        }
        const auto first=CalculateMinimum(item.First,separator);
        const auto second=CalculateMinimum(item.Second,separator);
        return item.Minimum=item.Axis==Qt::Horizontal
            ? QSize(first.width()+second.width()+separator,std::max(first.height(),second.height()))
            : QSize(std::max(first.width(),second.width()),first.height()+second.height()+separator);
    }
    void Plan(int node, const QRect& rect, int separator) {
        auto& item=Nodes[node];
        item.Planned=rect;
        if(item.IsLeaf())return;
        const bool horizontal=item.Axis==Qt::Horizontal;
        const int total=std::max(2,(horizontal?rect.width():rect.height())-separator);
        const QSize aMin=Nodes[item.First].Minimum,bMin=Nodes[item.Second].Minimum;
        const int firstMin=horizontal?aMin.width():aMin.height();
        const int secondMin=horizontal?bMin.width():bMin.height();
        const int low=firstMin+secondMin<=total?firstMin:1;
        const int high=firstMin+secondMin<=total?total-secondMin:total-1;
        const int preferred=item.InsertedExtent>0
            ? (item.InsertedFirst?item.InsertedExtent:total-item.InsertedExtent)
            : int(std::lround(total*item.Ratio));
        const int first=std::clamp(preferred,low,high);
        QRect a=rect,b=rect;
        if(horizontal) {
            a.setWidth(first);
            b.setLeft(rect.left()+first+separator);
        } else {
            a.setHeight(first);
            b.setTop(rect.top()+first+separator);
        }
        Plan(item.First,a,separator);
        Plan(item.Second,b,separator);
    }
    void CollectSizes(int node,QList<QDockWidget*>& docks,QList<int>& widths,QList<int>& heights) const {
        const auto& item=Nodes[node];
        if(item.IsLeaf()) {
            docks.append(item.Active->GetDockWidget());
            widths.append(std::max(1,item.Planned.width()));
            // Qt uses the supplied extent for the entire tab group, including its tab bar.
            heights.append(std::max(1,item.Planned.height()));
            return;
        }
        CollectSizes(item.First,docks,widths,heights);
        CollectSizes(item.Second,docks,widths,heights);
    }
    void Resize(QMainWindow* window) const {
        QList<QDockWidget*> docks;
        QList<int> widths,heights;
        CollectSizes(Root,docks,widths,heights);
        window->resizeDocks(docks,widths,Qt::Horizontal);window->resizeDocks(docks,heights,Qt::Vertical);
    }
    int TitleHeight = 26;
};

QRect Workspace(const QMainWindow* window) {
    QRect rect=window->contentsRect();
    if(const auto* menu=window->menuWidget();menu&&menu->isVisible())rect.setTop(std::max(rect.top(),menu->geometry().bottom()+1));
    for(auto* toolbar:window->findChildren<QToolBar*>(QString(),Qt::FindDirectChildrenOnly))
        if(toolbar->isVisible()&&window->toolBarArea(toolbar)==Qt::TopToolBarArea)
            rect.setTop(std::max(rect.top(),toolbar->geometry().bottom()+1));
    if(auto* status=window->findChild<QStatusBar*>(QString(),Qt::FindDirectChildrenOnly);status&&status->isVisible())
        rect.setBottom(std::min(rect.bottom(),status->geometry().top()-1));
    return rect;
}

LayoutTree Capture(const IDEDockerManager* manager,QMainWindow* window=nullptr) {
    if(!window)window=manager->Window();
    const auto& dockers=manager->Dockers();
    LayoutTree tree;std::vector<int> leaves;QList<IDEDockerFinal*> seen;
    for(IDEDockerFinal* docker:dockers) {
        auto* dock=docker->GetDockWidget();
        if(seen.contains(docker)||manager->DockHost(dock)!=window||dock->isHidden())continue;
        QList<IDEDockerFinal*> tabs{docker};
        const auto nativePeers=window->tabifiedDockWidgets(dock);
        for(IDEDockerFinal* peer:dockers)if(peer!=docker&&!peer->GetDockWidget()->isHidden()&&!peer->GetDockWidget()->isFloating()&&nativePeers.contains(peer->GetDockWidget()))tabs.append(peer);
        IDEDockerFinal* active=docker;
        for(IDEDockerFinal* peer:tabs)if(peer->GetDockWidget()->geometry().left()>=0&&peer->GetDockWidget()->geometry().top()>=0){active=peer;break;}
        // 布局提交时保存真实标签身份顺序；快照不能切换标签，尤其不能在 closeEvent 中激活待关闭面板。
        for(IDEDockerFinal* member:tabs) {
            const auto saved=manager->TabOrder(member);if(saved.empty())continue;
            QList<IDEDockerFinal*> ordered;
            for(IDEDockerFinal* tab:saved)if(tabs.contains(tab)&&!ordered.contains(tab))ordered.append(tab);
            for(IDEDockerFinal* tab:tabs)if(!ordered.contains(tab))ordered.append(tab);
            tabs=ordered;break;
        }
        auto* activeDock=active->GetDockWidget();
        auto rect=activeDock->geometry();
        int tabHeight=0;
        if(tabs.size()>1)for(auto* bar:window->findChildren<QTabBar*>(QString(),Qt::FindDirectChildrenOnly)) {
            if(!bar->isHidden()&&bar->count()==tabs.size()&&rect.left()<=bar->geometry().center().x()&&
                rect.right()>=bar->geometry().center().x()&&std::abs(rect.top()-bar->geometry().bottom()-1)<=6) {
                tabHeight=rect.top()-bar->geometry().top();
                rect.setTop(bar->geometry().top());
                // A native reset can change order before the cached identities are refreshed.
                QList<IDEDockerFinal*> remaining=tabs, ordered;
                for(int index=0;index<bar->count();++index) {
                    auto match=std::find_if(remaining.begin(),remaining.end(),[bar,index](IDEDockerFinal* tab) {
                        return tab->GetDockWidget()->windowTitle()==bar->tabText(index);
                    });
                    if(match==remaining.end())break;
                    if(std::any_of(std::next(match),remaining.end(),[match](IDEDockerFinal* tab) {
                        return tab->GetDockWidget()->windowTitle()==(*match)->GetDockWidget()->windowTitle();
                    }))break;
                    ordered.append(*match);
                    remaining.erase(match);
                }
                if(remaining.empty())tabs=ordered;
                break;
            }
        }
        if(tabs.size()==1)if(auto* title=activeDock->titleBarWidget();title&&title->minimumHeight()>0)
            tree.TitleHeight=title->minimumHeight();
        seen.append(tabs);leaves.push_back(tree.Leaf(tabs,active,rect,tabHeight));
    }
    tree.Root=tree.Build(leaves);return tree;
}

int FindSeparator(const LayoutTree& tree,const QMainWindow* window) {
    int separator=17;
    for(const auto& a:tree.Nodes) {
        if(!a.IsLeaf()||!a.Rect.isValid())continue;
        for(const auto& b:tree.Nodes) {
            if(!b.IsLeaf()||!b.Rect.isValid())continue;
            if(a.Rect.top()<=b.Rect.bottom()&&b.Rect.top()<=a.Rect.bottom()) {
                const int gap=b.Rect.left()-a.Rect.right()-1;
                if(gap>0)separator=std::min(separator,gap);
            }
            if(a.Rect.left()<=b.Rect.right()&&b.Rect.left()<=a.Rect.right()) {
                const int gap=b.Rect.top()-a.Rect.bottom()-1;
                if(gap>0)separator=std::min(separator,gap);
            }
        }
    }
    // Qt's stylesheet separator can be thicker than its native style metric.
    return separator<=16?separator:std::max(6,window->style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent));
}

int AddSplit(LayoutTree& tree,IDEDockerFinal* source,IDEDockerFinal* target,DockerDropPosition position,
    bool root,const QList<IDEDockerFinal*>& targetSpan,float sourceFraction,const QRect& workspace) {
    tree.Root=tree.Remove(source,tree.Root);
    int old=tree.Root;
    if(!root) {
        old=targetSpan.empty()?tree.Find(target,tree.Root):tree.Cover(targetSpan,tree.Root);
    }
    if(old<0&&!root)return -1;
    const QRect rect=old>=0?tree.Nodes[old].Rect:workspace;
    const int leaf=tree.Leaf({source},source,rect);
    if(old<0){tree.Root=leaf;return leaf;}
    const bool before=position==DockerDropPosition::Left||position==DockerDropPosition::Top;
    const bool horizontal=position==DockerDropPosition::Left||position==DockerDropPosition::Right;
    const int available=horizontal?rect.width():rect.height();
    const int desired=root?200:std::max(100,int(std::lround(available/3.0)));
    const auto axis=horizontal?Qt::Horizontal:Qt::Vertical;
    const bool restoreExtent=sourceFraction>0&&sourceFraction<1;
    const int branch=tree.Branch(before?leaf:old,before?old:leaf,axis,
        restoreExtent?(before?sourceFraction:1-sourceFraction):0.5f);
    if(!restoreExtent) {
        tree.Nodes[branch].InsertedExtent=desired;
        tree.Nodes[branch].InsertedFirst=before;
    }
    tree.Root=root?branch:tree.Replace(tree.Root,old,branch);
    return leaf;
}
}

QRect IDEDockerManager::PreviewSplit(IDEDockerFinal* source,IDEDockerFinal* target,DockerDropPosition position,
    bool root,QMainWindow* host) const {
    if(!_Dockers.contains(source)||source==target||position==DockerDropPosition::Tabs||
        (!root&&!_Dockers.contains(target)))return {};
    auto* targetDock=target?target->GetDockWidget():nullptr;
    const bool standalone=targetDock&&targetDock->isFloating();
    if(!host)host=DockHost(targetDock);
    if(!host)host=_Window;
    const QRect workspace=standalone?QRect(QPoint(),targetDock->size()):Workspace(host);
    LayoutTree tree;
    if(standalone)tree.Root=tree.Leaf({target},target,workspace);
    else tree=Capture(this,host);
    const int separator=FindSeparator(tree,host);
    const int moved=AddSplit(tree,source,target,position,root,{},-1,workspace);
    if(moved<0||!workspace.isValid())return {};
    const auto minimum=tree.CalculateMinimum(tree.Root,separator);
    if(minimum.width()>workspace.width()||minimum.height()>workspace.height())return {};
    tree.Plan(tree.Root,workspace,separator);
    QRect result=tree.Nodes[moved].Planned;
    result.moveTopLeft((standalone?static_cast<QWidget*>(targetDock):host)->mapToGlobal(result.topLeft()));
    return result;
}

bool IDEDockerManager::DropDocker(IDEDockerFinal* source,IDEDockerFinal* target,DockerDropPosition position,bool root,
    int tabIndex,const QList<IDEDockerFinal*>& targetSpan,float sourceFraction,QMainWindow* host) {
    if(!_Dockers.contains(source)||(!root&&!_Dockers.contains(target))||source==target||
        (root&&position==DockerDropPosition::Tabs))return false;
    auto* sourceDock=source->GetDockWidget();
    auto* targetDock=target?target->GetDockWidget():nullptr;
    auto* previousHost=IDEDockerFloatingHost::For(sourceDock);
    const bool promote=targetDock&&targetDock->isFloating();
    auto* floatingHost=promote?IDEDockerFloatingHost::Promote(_Window,targetDock):
        qobject_cast<IDEDockerFloatingHost*>(host?host:DockHost(targetDock));
    if(promote) {
        if(!floatingHost)return false;
        ObserveFloatingHost(floatingHost);
    }
    host=floatingHost?static_cast<QMainWindow*>(floatingHost):(host?host:_Window);
    ForgetClosedDocker(source);
    if(host==_Window)RestoreMaximized();
    host->layout()->activate();

    if(position==DockerDropPosition::Tabs) {
        if(!sourceDock->isFloating()&&host->tabifiedDockWidgets(targetDock).contains(sourceDock)) {
            source->Activate();
            return true;
        }
        if(floatingHost)floatingHost->AddPanel(sourceDock);
        else {
            if(previousHost)previousHost->RemovePanel(sourceDock);
            if(sourceDock->isFloating())sourceDock->setFloating(false);
        }
        // A dock brought back from floating can return to its old Qt tab slot.
        // Detach it before attaching it to the destination group.
        host->removeDockWidget(sourceDock);
        host->tabifyDockWidget(targetDock,sourceDock);
        sourceDock->show();
        sourceDock->raise();
        if(!floatingHost&&previousHost)previousHost->CloseIfNoVisiblePanels();
        PlaceTab(host,source,tabIndex);
        RequestRefresh();
        return true;
    }

    const QRect workspace=Workspace(host);
    LayoutTree tree=Capture(this,host);
    const int separator=FindSeparator(tree,host);
    if(AddSplit(tree,source,target,position,root,targetSpan,sourceFraction,workspace)<0)return false;
    tree.CalculateMinimum(tree.Root,separator);
    tree.Plan(tree.Root,workspace,separator);
    const bool updates=host->updatesEnabled();
    host->setUpdatesEnabled(false);
    if(previousHost&&previousHost!=floatingHost)previousHost->RemovePanel(sourceDock);
    if(floatingHost&&IDEDockerFloatingHost::For(sourceDock)!=floatingHost)floatingHost->AddPanel(sourceDock);
    // 所有可见停靠面板进入同一分割树，避免四个 Qt dock-area 环绕空中央区的限制。
    // 浮动面板不参与本次重排；关闭的面板保留原对象，之后仍能从窗口菜单打开。
    QList<QDockWidget*> closed;
    for(IDEDockerFinal* docker:_Dockers) {
        auto* dock=docker->GetDockWidget();
        if(docker!=source&&(dock->isFloating()||dock->parentWidget()!=host))continue;
        const bool hidden=dock->isHidden()&&docker!=source;
        if(dock->isFloating())dock->setFloating(false);
        host->removeDockWidget(dock);host->addDockWidget(Qt::LeftDockWidgetArea,dock);
        // add 清除原标签占位，再移出；构建分割树时只放入已到达的叶子，
        // 不能让尚未构建的面板作为原生兄弟节点改变 splitDockWidget 的插入层级。
        host->removeDockWidget(dock);dock->hide();
        if(hidden)closed.append(dock);
    }
    host->addDockWidget(Qt::LeftDockWidgetArea,tree.Anchor(tree.Root));
    tree.Restore(host,tree.Root);
    for(QDockWidget* dock:closed){host->addDockWidget(Qt::LeftDockWidgetArea,dock);dock->hide();}
    QList<QList<IDEDockerFinal*>> groups;
    tree.Groups(tree.Root,groups);
    for(const auto& group:groups)for(IDEDockerFinal* tab:group) {
        tab->SetTabbedTitle(group.size()>1);
        _TabOrders.insert(tab,group);
    }
    host->layout()->activate();
    tree.Resize(host);
    host->layout()->activate();
    sourceDock->show();sourceDock->raise();
    host->setUpdatesEnabled(updates);
    if(host==_Window)RememberHorizontalDockWidths();
    if(previousHost&&previousHost!=floatingHost)previousHost->CloseIfNoVisiblePanels();
    RequestRefresh();
    return true;
}

void IDEDockerManager::RememberClosedDocker(IDEDockerFinal* docker,bool confirmed) {
    if(!_Dockers.contains(docker)||docker->GetDockWidget()->isFloating())return;
    auto previous=_ClosedPlacements.find(docker);
    if(previous!=_ClosedPlacements.end()) {
        if(previous->Closed)return;
        if(confirmed&&docker->GetDockWidget()->isHidden()){previous->Closed=true;return;}
    }
    LayoutTree tree=Capture(this);
    const int leaf=tree.Find(docker,tree.Root);if(leaf<0)return;
    ClosedPlacement placement;
    placement.Closed=confirmed;
    placement.Peers=tree.Nodes[leaf].Tabs;
    placement.TabIndex=placement.Peers.indexOf(docker);placement.Peers.removeAll(docker);
    const int parent=tree.Parent(leaf,tree.Root);
    if(parent>=0) {
        const LayoutNode& branch=tree.Nodes[parent];
        const bool first=branch.First==leaf;
        tree.Collect(first?branch.Second:branch.First,placement.Span);
        placement.Position=branch.Axis==Qt::Horizontal?(first?DockerDropPosition::Left:DockerDropPosition::Right)
                                                      :(first?DockerDropPosition::Top:DockerDropPosition::Bottom);
        placement.Root=parent==tree.Root;
        placement.Fraction=first?branch.Ratio:1-branch.Ratio;
    }
    _ClosedPlacements.insert(docker,placement);
}

void IDEDockerManager::RestoreClosedDocker(IDEDockerFinal* docker) {
    const auto found=_ClosedPlacements.find(docker);if(found==_ClosedPlacements.end())return;
    const ClosedPlacement placement=found.value();_ClosedPlacements.erase(found);
    if(!placement.Closed)return;
    if(docker->GetDockWidget()->isFloating())return;
    for(IDEDockerFinal* peer:placement.Peers) {
        if(_Dockers.contains(peer)&&!peer->GetDockWidget()->isHidden()&&!peer->GetDockWidget()->isFloating()) {
            DropDocker(docker,peer,DockerDropPosition::Tabs,false,placement.TabIndex);return;
        }
    }
    QList<IDEDockerFinal*> visible;
    for(IDEDockerFinal* pane:placement.Span)
        if(_Dockers.contains(pane)&&!pane->GetDockWidget()->isHidden()&&!pane->GetDockWidget()->isFloating())visible.append(pane);
    if(!visible.empty())DropDocker(docker,visible.front(),placement.Position,placement.Root,-1,visible,placement.Fraction);
}

void IDEDockerManager::FloatDocker(IDEDockerFinal* source,const QPoint& globalPosition) {
    if(!_Dockers.contains(source))return;
    ForgetClosedDocker(source);
    RestoreMaximized();
    auto* dock=source->GetDockWidget();const QSize size=dock->size();
    auto* previousHost=IDEDockerFloatingHost::For(dock);
    if(previousHost) {
        previousHost->RemovePanel(dock);
        _Window->addDockWidget(Qt::LeftDockWidgetArea,dock);
        previousHost->CloseIfNoVisiblePanels();
    }
    dock->setFloating(true);dock->resize(size);dock->show();
    const int titleHeight=dock->titleBarWidget()?dock->titleBarWidget()->sizeHint().height():0;
    dock->move(globalPosition-QPoint(dock->width()/2,titleHeight/2));dock->raise();RequestRefresh();
}
