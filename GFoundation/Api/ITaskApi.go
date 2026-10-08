package Api

type TaskFunction func() (bool, any)
type TaskCompletedCallback func(success bool, context any)
type TaskGroupAllCompletedCallback func(successCount int, faildCount int)

type ITaskGroup interface {
	AddTask(task TaskFunction, completed TaskCompletedCallback)
	SetAllCompletedCallback(allCompleted TaskGroupAllCompletedCallback)
	Start()
}

const (
	Unorder int64 = -1
)

type ITaskApi interface {
	PushTask(mask int64, task TaskFunction, taskCompletedCallback TaskCompletedCallback)
	CreateTaskGroup() ITaskGroup
}
