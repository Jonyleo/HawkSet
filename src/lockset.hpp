#ifndef __HAWKSET_LOCKSET_HPP__
#define __HAWKSET_LOCKSET_HPP__

#include <memory>
#include <map>
#include <cstdint>
#include <bitset>
#include <functional>
#include <atomic>
#include <unordered_set>

#include "pin.H"

struct LockSet;
struct TimedLockSet;

typedef TimedLockSet const * pTimedLockSet;
typedef LockSet const * pLockSet;

extern PIN_MUTEX lock_register_mutex;

class LockSet {
private:
	static std::map<uint64_t, uint64_t> lock_index;
	static std::atomic_ulong lock_counter;

	void set_mutex_internal(uint64_t index, bool val);
	void set_mutex(uint64_t mutex, bool val);

public:
	std::vector<std::bitset<64>> locks = std::vector<std::bitset<64>>(1);

	LockSet() {}
	LockSet(pLockSet ls) : locks(ls->locks) {}

	static void register_mutex(uint64_t mutex);
	static uint64_t get_index(uint64_t mutex);
	static uint64_t register_special_mutex();

	void lock(uint64_t mutex);
	void unlock(uint64_t mutex);
	void lock_special(uint64_t mutex);
	void unlock_special(uint64_t mutex);

	bool simple_intersect(const pLockSet ls) const;
	bool empty() const;
	bool operator==(const LockSet& ls) const;
	bool operator!=(const LockSet& ls) const;

	pLockSet intersect(pLockSet ls);
	
	friend std::ostream& operator<<(std::ostream& os, const LockSet& ls);

friend class TimedLockSet;
};

class TimedLockSet {
public:
	LockSet * lockset;
	std::map<uint64_t, uint64_t> timestamps;

	TimedLockSet() : lockset(new LockSet()) {}
	TimedLockSet(pLockSet ls, const std::map<uint64_t, uint64_t> & ts) : lockset((LockSet *)ls), timestamps(ts) {}
	TimedLockSet(pTimedLockSet tls) : lockset(tls->lockset), timestamps(tls->timestamps) {}
	
	void lock(uint64_t mutex, uint64_t timestamp);
	void unlock(uint64_t mutex);

	void lock_special(uint64_t mutex, uint64_t timestamp);
	void unlock_special(uint64_t mutex);

	bool empty() const;

	pLockSet intersect(pTimedLockSet tls) const;
};


/* * *
 *
 * Helper function definitions for LockSet Caching 
 * 
 * */

template <>
struct std::hash<const LockSet> {
	std::size_t operator()(const LockSet &ls) const {
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
struct std::equal_to<const LockSet> {
	bool operator()(const LockSet &ls1, const LockSet &ls2) const {
		return ls1 == ls2;
    }
};

template <>
struct std::hash<pLockSet> {
	std::size_t operator()(const pLockSet &ls) const {
		return std::hash<const LockSet>{}(*ls);
    }
};

template <>
struct std::equal_to<pLockSet> {
	bool operator()(const pLockSet &ls1, const pLockSet &ls2) const {
		return std::equal_to<const LockSet>{}(*ls1, *ls2);
    }
};


extern PIN_MUTEX lockset_cache_mutex;
extern std::unordered_set<pLockSet> locksets_cache;
pLockSet lockset_cache_get(pLockSet ls);

template <>
struct std::hash<const TimedLockSet> {
	std::size_t operator()(const TimedLockSet &tls) const {
        size_t seed = std::hash<const LockSet>{}(tls.lockset);
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
struct std::equal_to<const TimedLockSet> {
	bool operator()(const TimedLockSet &tls1, const TimedLockSet &tls2) const {
		if(*tls1.lockset != *tls2.lockset)
			return false;

		return tls1.timestamps == tls2.timestamps;
    }
};

template <>
struct std::hash<pTimedLockSet> {
	std::size_t operator()(const pTimedLockSet &ls) const {
		return std::hash<const TimedLockSet>{}(*ls);
    }
};

template <>
struct std::equal_to<pTimedLockSet> {
	bool operator()(const pTimedLockSet &ls1, const pTimedLockSet &ls2) const {
		return std::equal_to<const TimedLockSet>{}(*ls1, *ls2);
    }
};

extern PIN_MUTEX timedlockset_cache_mutex;
extern std::unordered_set<pTimedLockSet> timedlocksets_cache;
pTimedLockSet timedlockset_cache_get(pTimedLockSet tls);

#endif
