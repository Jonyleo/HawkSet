#ifndef __HAWKSET_CACHE_HPP__
#define __HAWKSET_CACHE_HPP__

#include <cassert>

#include "sync.hpp"

/* * *
 *
 * Helper function definitions for Lockset Caching 
 * 
 * */

template <>
struct std::hash<const Lockset> {
	std::size_t operator()(const Lockset &ls) const {
        size_t seed = 0;
        int macro = 0;
		for(std::bitset<64> ele : ls.locks) {
			for(int i = 0; i < 64; i++) {
				if (ele.test(i)) {
					seed ^= macro + i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
				}
			}
			
			macro++;
		}
		return seed;
    }
};

template <>
struct std::equal_to<const Lockset> {
	bool operator()(const Lockset &ls1, const Lockset &ls2) const {
		return ls1 == ls2;
    }
};

template <>
struct std::hash<pLockset> {
	std::size_t operator()(const pLockset &ls) const {
		return std::hash<const Lockset>{}(*ls);
    }
};

template <>
struct std::equal_to<pLockset> {
	bool operator()(const pLockset &ls1, const pLockset &ls2) const {
		return std::equal_to<const Lockset>{}(*ls1, *ls2);
    }
};


template <>
struct std::hash<const TimedLockset> {
	std::size_t operator()(const TimedLockset &tls) const {
        size_t seed = 0;
        int macro = 0;
		for(const auto& entry : tls.timestamps) {
			seed ^= macro + entry.first + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			seed ^= macro + entry.second + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			
			macro++;
		}
		return seed;
    }
};

template <>
struct std::equal_to<const TimedLockset> {
	bool operator()(const TimedLockset &tls1, const TimedLockset &tls2) const {
		return tls1 == tls2;
    }
};

template <>
struct std::hash<pTimedLockset> {
	std::size_t operator()(const pTimedLockset &ls) const {
		return std::hash<const TimedLockset>{}(*ls);
    }
};

template <>
struct std::equal_to<pTimedLockset> {
	bool operator()(const pTimedLockset &ls1, const pTimedLockset &ls2) const {
		return std::equal_to<const TimedLockset>{}(*ls1, *ls2);
    }
};

MUTEX_DECL lockset_cache_mutex;
std::unordered_set<pLockset> locksets_cache;
pLockset empty_lockset = new Lockset();

MUTEX_DECL timedlockset_cache_mutex;
std::unordered_set<pTimedLockset> timedlocksets_cache;

pLockset lockset_cache_get(pLockset ls) {
	MUTEX_LOCK(lockset_cache_mutex);

	auto lockset_iter = locksets_cache.find(ls);

	if(lockset_iter == locksets_cache.end()) {
		bool inserted = false;

		std::tie(lockset_iter, inserted) = 
		locksets_cache.insert(
			new Lockset(ls)
		);

		assert(inserted);
	} 

	auto ret = *lockset_iter;
	
	MUTEX_UNLOCK(lockset_cache_mutex);
	return ret;
}

pTimedLockset timedlockset_cache_get(pTimedLockset tls) {
	MUTEX_LOCK(timedlockset_cache_mutex);

	auto timedlockset_iter = timedlocksets_cache.find(tls);

	if(timedlockset_iter == timedlocksets_cache.end()) {
		bool inserted = false;

		std::tie(timedlockset_iter, inserted) =
		timedlocksets_cache.insert(
			new TimedLockset(tls->timestamps)
		);

		assert(inserted);
	}

	auto ret = *timedlockset_iter;

	MUTEX_UNLOCK(timedlockset_cache_mutex);
	return ret;
}

pLockset intersect_lockset(pLockset ls1, pLockset ls2) {
	if(ls1->empty())
		return empty_lockset;

	if(ls2->empty())
		return empty_lockset;

	if(ls1 == ls2)
		return ls1;

	Lockset result(ls1);

	result.intersect(ls2);

	return lockset_cache_get(&result);
}

pLockset intersect_timedlockset(pTimedLockset tls1, pTimedLockset tls2) {
	if(tls1->empty())
		return empty_lockset;

	if(tls2->empty())
		return empty_lockset;

	if(tls1 == tls2) {
		Lockset result = std::move(tls1->to_lockset());
		return lockset_cache_get(&result);
	}

	TimedLockset result(tls1);

	result.intersect(tls2);

	Lockset ls = std::move(result.to_lockset());

	return lockset_cache_get(&ls);
}

#endif