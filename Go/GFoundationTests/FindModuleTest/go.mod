module FindModuleTest

go 1.27.1

require GFoundationApi v0.0.0
replace GFoundationApi => ../../GFoundationApi

require GFoundationTests/Interface v0.0.0
replace GFoundationTests/Interface => ../Interface

require GFoundationTests/StressKit v0.0.0
replace GFoundationTests/StressKit => ../StressKit
