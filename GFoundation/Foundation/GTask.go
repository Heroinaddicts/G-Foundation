package Foundation

import (
	"GFoundation/Api"
	"GFoundation/Utils"
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
	task          Api.TaskFunction
	taskCompleted Api.TaskCompletedCallback
	group         *TaskGroup
}

type TaskGroup struct {
	taskCount    int
	successCount int
	faildCount   int
	allCompleted Api.TaskGroupAllCompletedCallback

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

func (g *TaskGroup) AddTask(task Api.TaskFunction, completed Api.TaskCompletedCallback) {
	g.taskCount++
	g.tasks.PushBack(&TaskInfo{task, completed, g})
}

func (g *TaskGroup) SetAllCompletedCallback(allCompleted Api.TaskGroupAllCompletedCallback) {
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
	resultQueue *Utils.SPSCQueue[*Result]
	orderQueue  []chan *TaskInfo
	procCount   int64
}

func NewGTask() *GTask {
	n := runtime.GOMAXPROCS(0)
	fmt.Printf("get proc count %d\n", n)

	t := &GTask{
		resultQueue: Utils.NewSPSCQueue[*Result](16384),
		orderQueue:  make([]chan *TaskInfo, n),
		procCount:   int64(n),
	}

	for i := range t.orderQueue {
		t.orderQueue[i] = make(chan *TaskInfo, 16384)

		go func() {
			for {
				p := <-t.orderQueue[i]
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
			}
		}()
	}

	return t
}

func (t *GTask) PushTask(mask int64, task Api.TaskFunction, taskCompletedCallback Api.TaskCompletedCallback) {
	if Api.Unorder == mask {
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

func (t *GTask) CreateTaskGroup() Api.ITaskGroup {
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
