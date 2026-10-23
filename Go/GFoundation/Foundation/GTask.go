package Foundation

import (
	"GFoundationApi"
	"container/list"
	"fmt"
	"runtime"
	"sync"
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

		go g.gtask.runTask(taskInfo)
	}
}

type GTask struct {
	resultMu    sync.Mutex
	results     []Result
	resultBatch []Result
	orderQueue  []chan *TaskInfo
	procCount   int64
}

func NewGTask() *GTask {
	n := runtime.GOMAXPROCS(0)
	fmt.Printf("get proc count %d\n", n)

	t := &GTask{
		results:     make([]Result, 0, 1024),
		resultBatch: make([]Result, 0, 1024),
		orderQueue:  make([]chan *TaskInfo, n),
		procCount:   int64(n),
	}

	for i := range t.orderQueue {
		t.orderQueue[i] = make(chan *TaskInfo, 16384)

		go func(queue <-chan *TaskInfo) {
			for info := range queue {
				t.runTask(info)
			}
		}(t.orderQueue[i])
	}

	return t
}

func (t *GTask) PushTask(mask int64, task GFoundationApi.TaskFunction, taskCompletedCallback GFoundationApi.TaskCompletedCallback) {
	info := &TaskInfo{task: task, taskCompleted: taskCompletedCallback}
	if GFoundationApi.Unorder == mask {
		go t.runTask(info)
	} else {
		t.orderQueue[mask%t.procCount] <- info
	}
}

func (t *GTask) runTask(info *TaskInfo) {
	success, context := info.task()
	// Producers park on contention instead of spinning on the shared queue.
	// Store values so each completion does not allocate another Result.
	t.resultMu.Lock()
	t.results = append(t.results, Result{context: context, success: success, taskInfo: info, group: info.group})
	t.resultMu.Unlock()
}

func (t *GTask) CreateTaskGroup() GFoundationApi.ITaskGroup {
	return NewTaskGroup(t)
}

func (t *GTask) Update() {
	// Only dispatch the results available at entry. New completions belong to
	// the next Update, so callbacks cannot keep this drain running forever.
	t.resultMu.Lock()
	batch := t.results
	t.results = t.resultBatch[:0]
	t.resultMu.Unlock()
	for i := range batch {
		p := batch[i]
		batch[i] = Result{}
		if p.taskInfo.taskCompleted != nil {
			p.taskInfo.taskCompleted(p.success, p.context)
		}
		if p.group != nil {
			if p.success == true {
				p.group.successCount++
			} else {
				p.group.faildCount++
			}

			if p.group.taskCount == p.group.faildCount+p.group.successCount && p.group.allCompleted != nil {
				p.group.allCompleted(p.group.successCount, p.group.faildCount)
			}
		}
	}
	t.resultBatch = batch[:0]
}
