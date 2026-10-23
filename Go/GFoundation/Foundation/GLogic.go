package Foundation

import (
	"GFoundationApi"
	"GFoundationUtils"
	"fmt"
	"plugin"
	"strings"
	"sync"
)

type GLogic struct {
	modules map[string]GFoundationApi.IModule
}

var (
	glinstance *GLogic
	glonce     sync.Once
)

func GLogicInstance() *GLogic {
	glonce.Do(func() {
		glinstance = &GLogic{
			modules: make(map[string]GFoundationApi.IModule),
		}
	})

	return glinstance
}

func (l *GLogic) Launch() {
	lp := LauncherParamsInstance()
	p := lp.GetString("modules")
	names := strings.Split(p, ",")

	dir := GFoundationUtils.GetCurrentExeDir()
	if lp.Has("modules_dir") {
		dir = lp.GetString("module_dir")
	}

	for _, item := range names {
		fmt.Println(item)
		p, err := plugin.Open(dir + "/" + item + ".so")
		if err != nil {
			panic(err)
		}

		symbol, err := p.Lookup("GetModule")
		if err != nil {
			panic(err)
		}

		getModule := symbol.(func() GFoundationApi.IModule)
		module := getModule()
		if module.Initialize(GFoundationInstance()) == false {
			fmt.Printf("module %s Initialize faild\n", item)
			return
		}
		fmt.Printf("module %s Initialize succeed\n", item)
		l.modules[item] = module
	}

	for name, module := range l.modules {
		if module.Launch(GFoundationInstance()) == false {
			fmt.Printf("module %s Launch faild\n", name)
			return
		}
		fmt.Printf("module %s Launch succeed\n", name)
	}

	for name, module := range l.modules {
		module.LaunchFinished(GFoundationInstance())
		fmt.Printf("module %s LaunchFinished\n", name)
	}
}

func (l *GLogic) Update() {
	for _, module := range l.modules {
		module.Update(GFoundationInstance())
	}
}

func (l *GLogic) GetModule(name string) GFoundationApi.IModule {
	return l.modules[name]
}
