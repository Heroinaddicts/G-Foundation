module HttpTest

go 1.27.1

require GFoundationApi v0.0.0

replace GFoundationApi => ../../GFoundationApi

require GFoundationUtils v0.0.0

replace GFoundationUtils => ../../GFoundationUtils

require GFoundationTests/StressKit v0.0.0
replace GFoundationTests/StressKit => ../StressKit
