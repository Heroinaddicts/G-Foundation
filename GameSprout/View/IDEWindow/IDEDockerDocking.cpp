#include "IDEDockerManager.h"
#include "IDEDockerDragController.h"
#include "IDEDockerFinal.h"
#include <QDockWidget>
#include <QLayout>
#include <QMainWindow>
#include <QTabBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QStyle>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
// 布局事务只保存面板身份、标签组和分割比例。View/原生渲染窗口不被重新创建。
struct LayoutNode {
    QList<IDEDockerFinal*> Tabs;
    IDEDockerFinal* Active = nullptr;
    QRect Rect;
    int First = -1, Second = -1;
    Qt::Orientation Axis = Qt::Horizontal;
    float Ratio = 0.5f;
    bool IsLeaf() const { return First < 0 && Second < 0; }
};
class LayoutTree {
public:
    std::vector<LayoutNode> Nodes;
    int Root = -1;
    int Leaf(const QList<IDEDockerFinal*>& tabs, IDEDockerFinal* active, const QRect& rect) {
        LayoutNode node;node.Tabs=tabs;node.Active=active;node.Rect=rect;
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
            Nodes[node].Tabs.removeAll(docker);
            if(Nodes[node].Tabs.empty())return -1;
            if(Nodes[node].Active==docker)Nodes[node].Active=Nodes[node].Tabs.front();
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
    void Plan(QMainWindow* window,int node,QSize extent,int separator,QList<QDockWidget*>& docks,QList<int>& widths,QList<int>& heights) const {
        const LayoutNode& item=Nodes[node];
        if(item.IsLeaf()) {
            auto* active=item.Active->GetDockWidget();int tabHeight=0;
            if(item.Tabs.size()>1)for(QTabBar* bar:window->findChildren<QTabBar*>(QString(),Qt::FindDirectChildrenOnly)) {
                if(bar->isVisible()&&bar->geometry().center().x()>=active->geometry().left()&&bar->geometry().center().x()<=active->geometry().right()
                    &&std::abs(active->geometry().top()-bar->geometry().bottom()-1)<=6) {tabHeight=bar->height();break;}
            }
            docks.append(active);widths.append(std::max(1,extent.width()));heights.append(std::max(1,extent.height()-tabHeight));return;
        }
        const int total=std::max(2,(item.Axis==Qt::Horizontal?extent.width():extent.height())-separator);
        const int first=std::clamp(int(std::lround(total*item.Ratio)),1,total-1);
        QSize a=extent,b=extent;
        if(item.Axis==Qt::Horizontal){a.setWidth(first);b.setWidth(total-first);}
        else {a.setHeight(first);b.setHeight(total-first);}
        Plan(window,item.First,a,separator,docks,widths,heights);Plan(window,item.Second,b,separator,docks,widths,heights);
    }
    void Resize(QMainWindow* window,int node) const {
        // 一次性给每个标签组传递相互一致的叶子尺寸；逐层把整窗跨度传给局部节点会挤压其他分支。
        QSize extent=window->contentsRect().size();
        if(window->menuBar()->isVisible())extent.rheight()-=window->menuBar()->height();
        if(auto* status=window->findChild<QStatusBar*>(QString(),Qt::FindDirectChildrenOnly);status&&status->isVisible())extent.rheight()-=status->height();
        QList<QDockWidget*> docks;QList<int> widths,heights;
        Plan(window,node,extent,window->style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent),docks,widths,heights);
        window->resizeDocks(docks,widths,Qt::Horizontal);window->resizeDocks(docks,heights,Qt::Vertical);
    }
};
LayoutTree Capture(IDEDockerManager* manager) {
    QMainWindow* window=manager->Window();
    const auto& dockers=manager->Dockers();
    LayoutTree tree;std::vector<int> leaves;QList<IDEDockerFinal*> seen;
    for(IDEDockerFinal* docker:dockers) {
        auto* dock=docker->GetDockWidget();
        if(seen.contains(docker)||dock->isFloating()||dock->isHidden())continue;
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
        seen.append(tabs);leaves.push_back(tree.Leaf(tabs,active,active->GetDockWidget()->geometry()));
    }
    tree.Root=tree.Build(leaves);return tree;
}
}

bool IDEDockerManager::DropDocker(IDEDockerFinal* source,IDEDockerFinal* target,DockerDropPosition position,bool root,int tabIndex,const QList<IDEDockerFinal*>& targetSpan,float sourceFraction) {
    if(!_Dockers.contains(source)||(!root&&!_Dockers.contains(target))||(!root&&source==target))return false;
    ForgetClosedDocker(source);
    RestoreMaximized();_Window->layout()->activate();
    LayoutTree tree=Capture(this);
    const int original=tree.Find(source,tree.Root),destination=tree.Find(target,tree.Root);
    const bool sameGroup=original>=0&&original==destination;
    if(position==DockerDropPosition::Tabs&&sameGroup&&tabIndex<0){source->Activate();return true;}
    int originalIndex=-1;
    if(original>=0)originalIndex=tree.Nodes[original].Tabs.indexOf(source);
    tree.Root=tree.Remove(source,tree.Root);
    if(position==DockerDropPosition::Tabs&&!root) {
        const int leaf=tree.Find(target,tree.Root);if(leaf<0)return false;
        auto& group=tree.Nodes[leaf];
        int insertion=tabIndex<0?group.Tabs.size():tabIndex;
        if(sameGroup&&originalIndex>=0&&tabIndex>originalIndex)--insertion;
        group.Tabs.insert(std::clamp(insertion,0,int(group.Tabs.size())),source);group.Active=source;
    } else {
        const bool before=position==DockerDropPosition::Left||position==DockerDropPosition::Top;
        const auto axis=position==DockerDropPosition::Left||position==DockerDropPosition::Right?Qt::Horizontal:Qt::Vertical;
        const int old=root?tree.Root:targetSpan.empty()?tree.Find(target,tree.Root):tree.Cover(targetSpan,tree.Root);
        if(old<0&&!root)return false;
        const QRect rect=old>=0?tree.Nodes[old].Rect:_Window->rect();
        const int leaf=tree.Leaf({source},source,rect);
        if(old<0)tree.Root=leaf;
        else {
            const float fraction=sourceFraction>0&&sourceFraction<1?sourceFraction:(root?0.25f:0.5f);
            const int branch=tree.Branch(before?leaf:old,before?old:leaf,axis,before?fraction:1-fraction);
            tree.Root=root?branch:tree.Replace(tree.Root,old,branch);
        }
    }
    if(tree.Root<0)return false;
    const bool updates=_Window->updatesEnabled();_Window->setUpdatesEnabled(false);
    // 所有可见停靠面板进入同一分割树，避免四个 Qt dock-area 环绕空中央区的限制。
    // 浮动面板不参与本次重排；关闭的面板保留原对象，之后仍能从窗口菜单打开。
    QList<QDockWidget*> closed;
    for(IDEDockerFinal* docker:_Dockers) {
        auto* dock=docker->GetDockWidget();
        if(dock->isFloating()&&docker!=source)continue;
        const bool hidden=dock->isHidden()&&docker!=source;
        if(dock->isFloating())dock->setFloating(false);
        _Window->removeDockWidget(dock);_Window->addDockWidget(Qt::LeftDockWidgetArea,dock);
        // add 清除原标签占位，再移出；构建分割树时只放入已到达的叶子，
        // 不能让尚未构建的面板作为原生兄弟节点改变 splitDockWidget 的插入层级。
        _Window->removeDockWidget(dock);dock->hide();
        if(hidden)closed.append(dock);
    }
    _Window->addDockWidget(Qt::LeftDockWidgetArea,tree.Anchor(tree.Root));
    tree.Restore(_Window,tree.Root);
    for(QDockWidget* dock:closed){_Window->addDockWidget(Qt::LeftDockWidgetArea,dock);dock->hide();}
    _Window->layout()->activate();tree.Resize(_Window,tree.Root);_Window->layout()->activate();
    source->GetDockWidget()->show();source->GetDockWidget()->raise();
    QList<QList<IDEDockerFinal*>> groups;tree.Groups(tree.Root,groups);
    for(const auto& group:groups)for(IDEDockerFinal* tab:group)_TabOrders.insert(tab,group);
    _Window->setUpdatesEnabled(updates);RequestRefresh();return true;
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
    dock->setFloating(true);dock->resize(size);dock->show();
    const int titleHeight=dock->titleBarWidget()?dock->titleBarWidget()->sizeHint().height():0;
    dock->move(globalPosition-QPoint(dock->width()/2,titleHeight/2));dock->raise();RequestRefresh();
}
