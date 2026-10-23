#ifndef __RedisHeader_h__
#define __RedisHeader_h__

#include "IRedis.h"
#include "ITaskApi.h"

#if defined(_WIN32)
#include <winsock2.h>
#endif

#include <hiredis/hiredis.h>
#include "SafeThread.hpp"
#include "XLock.hpp"

#include <set>

#endif //__RedisHeader_h__
