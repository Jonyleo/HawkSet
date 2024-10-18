#ifndef __HAWKSET_SYNC_HPP__
#define __HAWKSET_SYNC_HPP__


#ifdef USE_PIN_SYNC
#include "pin.H"
#define MUTEX_DECL PIN_MUTEX
#define MUTEX_LOCK(M) PIN_MutexLock(&M)
#define MUTEX_UNLOCK(M) PIN_MutexUnlock(&M)
// TODO REST
#else
#include <mutex>
#define MUTEX_DECL std::mutex
#define MUTEX_LOCK(M) M.lock()
#define MUTEX_UNLOCK(M) M.unlock()
#endif

#endif