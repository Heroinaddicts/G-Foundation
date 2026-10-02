package Api

type TaskFunction func() (bool, any)
type TaskCompletedCallback func(success bool, context any)
type TaskGroupAllCompletedCallback func(successCount int, faildCount int)

type ITaskGroup interface {
	AddTask(task TaskFunction, completed TaskCompletedCallback)
	SetAllCompletedCallback(allCompleted TaskGroupAllCompletedCallback)
	Start()
}

type ITaskApi interface {
	PushTask(task TaskFunction, taskCompletedCallback TaskCompletedCallback)
	CreateTaskGroup() ITaskGroup
}
