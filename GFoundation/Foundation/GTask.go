package Foundation

import (
	"GFoundationApi"
	"GFoundationUtils"
	"container/list"
	"fmt"
	"runtime"
)

type Result struct {
	context  any
	success  bool
	taskInfo *TaskInfo
	group    *TaskGroup
}

type TaskInfo struct {
	task          GFoundationApi.TaskFunction
	taskCompleted GFoundationApi.TaskCompletedCallback
	group         *TaskGroup
}

type TaskGroup struct {
	taskCount    int
	successCount int
	faildCount   int
	allCompleted GFoundationApi.TaskGroupAllCompletedCallback

	tasks list.List

	gtask *GTask
}

func NewTaskGroup(gtask *GTask) *TaskGroup {
	return &TaskGroup{
		taskCount:    0,
		successCount: 0,
		faildCount:   0,
		allCompleted: nil,
		tasks:        list.List{},
		gtask:        gtask,
	}
}

func (g *TaskGroup) AddTask(task GFoundationApi.TaskFunction, completed GFoundationApi.TaskCompletedCallback) {
	g.taskCount++
	g.tasks.PushBack(&TaskInfo{task, completed, g})
}

func (g *TaskGroup) SetAllCompletedCallback(allCompleted GFoundationApi.TaskGroupAllCompletedCallback) {
	g.allCompleted = allCompleted
}

func (g *TaskGroup) Start() {
	for elem := g.tasks.Front(); elem != nil; elem = elem.Next() {
		taskInfo := elem.Value.(*TaskInfo)

		go func(info *TaskInfo) {
			ret, context := info.task()
			g.gtask.resultQueue.Push(&Result{
				success:  ret,
				context:  context,
				taskInfo: info,
				group:    g,
			})
		}(taskInfo)
	}
}

type GTask struct {
	resultQueue *GFoundationUtils.SPSCQueue[*Result]
	orderQueue  []chan *TaskInfo
	procCount   int64
	funcs       []func(index int)
}

func NewGTask() *GTask {
	n := runtime.GOMAXPROCS(0)
	fmt.Printf("get proc count %d\n", n)

	t := &GTask{
		resultQueue: GFoundationUtils.NewSPSCQueue[*Result](16384),
		orderQueue:  make([]chan *TaskInfo, n),
		procCount:   int64(n),
		funcs:       make([]func(index int), n),
	}

	for i := range t.orderQueue {
		t.orderQueue[i] = make(chan *TaskInfo, 16384)

		t.funcs[i] = func(index int) {
			p := <-t.orderQueue[index]
			ret, context := p.task()
			t.resultQueue.Push(&Result{
				success: ret,
				context: context,
				taskInfo: &TaskInfo{
					task:          p.task,
					taskCompleted: p.taskCompleted,
					group:         nil,
				},
				group: nil,
			})

			go t.funcs[index](index)
		}

		go t.funcs[i](i)
	}

	return t
}

func (t *GTask) PushTask(mask int64, task GFoundationApi.TaskFunction, taskCompletedCallback GFoundationApi.TaskCompletedCallback) {
	if GFoundationApi.Unorder == mask {
		go func() {
			ret, context := task()
			t.resultQueue.Push(&Result{
				success: ret,
				context: context,
				taskInfo: &TaskInfo{
					task:          task,
					taskCompleted: taskCompletedCallback,
					group:         nil,
				},
				group: nil,
			})
		}()
	} else {
		t.orderQueue[mask%t.procCount] <- &TaskInfo{
			task:          task,
			taskCompleted: taskCompletedCallback,
			group:         nil,
		}
	}

}

func (t *GTask) CreateTaskGroup() GFoundationApi.ITaskGroup {
	return NewTaskGroup(t)
}

func (t *GTask) Update() {
	for {
		p, ret := t.resultQueue.Pop()
		if false == ret {
			break
		}

		p.taskInfo.taskCompleted(p.success, p.context)
		if p.group != nil {
			if p.success == true {
				p.group.successCount++
			} else {
				p.group.faildCount++
			}

			if p.group.taskCount == p.group.faildCount+p.group.successCount {
				p.group.allCompleted(p.group.successCount, p.group.faildCount)
			}
		}
	}
}
