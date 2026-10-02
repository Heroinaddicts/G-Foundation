package Foundation

import (
	"GFoundation/Api"
	"GFoundation/Utils"
	"fmt"
	"plugin"
	"strings"
	"sync"
)

type moduleinfo struct {
	module Api.IModule
	name   string
}

type GLogic struct {
	modules []moduleinfo
}

var (
	glinstance *GLogic
	glonce     sync.Once
)

func GLogicInstance() *GLogic {
	glonce.Do(func() {
		glinstance = &GLogic{
			modules: make([]moduleinfo, 0),
		}
	})

	return glinstance
}

func (l *GLogic) Launch() {
	lp := LauncherParamsInstance()
	p := lp.GetString("modules")
	names := strings.Split(p, ",")

	dir := Utils.GetCurrentExeDir()
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

		getModule := symbol.(func() Api.IModule)
		module := getModule()
		if module.Initialize(GFoundationInstance()) == false {
			fmt.Printf("module %s Initialize faild\n", item)
			return
		}
		fmt.Printf("module %s Initialize succeed\n", item)
		l.modules = append(l.modules, moduleinfo{module, item})
	}

	for _, module := range l.modules {
		if module.module.Launch(GFoundationInstance()) == false {
			fmt.Printf("module %s Launch faild\n", module.name)
			return
		}
		fmt.Printf("module %s Launch succeed\n", module.name)
	}

	for _, module := range l.modules {
		module.module.LaunchFinished(GFoundationInstance())
		fmt.Printf("module %s LaunchFinished\n", module.name)
	}
}

func (l *GLogic) Update() {
	for _, module := range l.modules {
		module.module.Update(GFoundationInstance())
	}
}
