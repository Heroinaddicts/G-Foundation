package Foundation

import (
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync"
)

type LauncherParams struct {
	params map[string]string
}

var (
	lpinstance *LauncherParams
	lponce     sync.Once
)

func LauncherParamsInstance() *LauncherParams {
	lponce.Do(func() {
		lpinstance = &LauncherParams{
			params: make(map[string]string),
		}

		lpinstance.parse(os.Args[1:])
	})

	return lpinstance
}

func (p *LauncherParams) parse(args []string) {
	for _, arg := range args {
		if !strings.HasPrefix(arg, "--") {
			continue
		}

		arg = arg[2:]

		index := strings.IndexByte(arg, '=')

		if index < 0 {
			p.params[arg] = ""
			continue
		}

		key := arg[:index]
		value := arg[index+1:]

		p.params[key] = value

		fmt.Printf("--%s=%s\n", key, value)
	}
}

func (p *LauncherParams) Has(name string) bool {
	_, ok := p.params[name]
	return ok
}

func (p *LauncherParams) GetString(name string) string {
	return p.params[name]
}

func (p *LauncherParams) GetInt(name string) int {
	value := p.params[name]

	result, err := strconv.Atoi(value)
	if err != nil {
		panic(fmt.Sprintf(
			"LauncherParams: '%s' is not int: '%s'",
			name,
			value,
		))
	}

	return result
}

func (p *LauncherParams) GetInt64(name string) int64 {
	value := p.params[name]

	result, err := strconv.ParseInt(value, 10, 64)
	if err != nil {
		panic(fmt.Sprintf(
			"LauncherParams: '%s' is not int64: '%s'",
			name,
			value,
		))
	}

	return result
}

func (p *LauncherParams) GetFloat(name string) float64 {
	value := p.params[name]

	result, err := strconv.ParseFloat(value, 64)
	if err != nil {
		panic(fmt.Sprintf(
			"LauncherParams: '%s' is not float: '%s'",
			name,
			value,
		))
	}

	return result
}

func (p *LauncherParams) GetBool(name string) bool {
	value := p.params[name]

	result, err := strconv.ParseBool(value)
	if err != nil {
		panic(fmt.Sprintf(
			"LauncherParams: '%s' is not bool: '%s'",
			name,
			value,
		))
	}

	return result
}

func (p *LauncherParams) GetStrings(name string) []string {
	value := p.GetString(name)

	if value == "" {
		return nil
	}

	return strings.Split(value, ";")
}
