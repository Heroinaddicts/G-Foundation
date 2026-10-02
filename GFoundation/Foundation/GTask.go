package Foundation

import (
	"GFoundation/Api"
	"GFoundation/Utils"
	"container/list"
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

		go func() {
			ret, context := taskInfo.task()
			g.gtask.resultQueue.Push(&Result{
				success:  ret,
				context:  context,
				taskInfo: taskInfo,
				group:    g,
			})
		}()
	}
}

type GTask struct {
	resultQueue *Utils.SPSCQueue[*Result]
}

func NewGTask() *GTask {
	return &GTask{
		resultQueue: Utils.NewSPSCQueue[*Result](1024),
	}
}

func (t *GTask) PushTask(task Api.TaskFunction, taskCompletedCallback Api.TaskCompletedCallback) {
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
