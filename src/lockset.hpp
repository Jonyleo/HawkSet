#ifndef __HAWKSET_LOCKSET_HPP__
#define __HAWKSET_LOCKSET_HPP__

#include <iostream>
#include <memory>
#include <map>
#include <cstdint>
#include <bitset>
#include <functional>
#include <atomic>
#include <vector>
#include <unordered_set>

struct Lockset;
struct TimedLockset;

typedef TimedLockset const * pTimedLockset;
typedef Lockset const * pLockset;

extern PIN_MUTEX lock_register_mutex;

class Lockset {
public:
	static std::map<uint64_t, uint64_t> lock_index;
	static std::atomic_ulong lock_counter;

	void set_mutex_internal(uint64_t index, bool val);
	void set_mutex(uint64_t mutex, bool val);

public:
	std::vector<std::bitset<64>> locks = std::vector<std::bitset<64>>(1);

	Lockset() {}
	Lockset(pLockset ls) : locks(ls->locks) {}

	static void register_mutex(uint64_t mutex);
	static uint64_t get_index(uint64_t mutex);
	static std::string get_mutex_id(uint64_t index);
	static uint64_t register_special_mutex();

	void lock(uint64_t mutex);
	void unlock(uint64_t mutex);
	void lock_special(uint64_t mutex);
	void unlock_special(uint64_t mutex);
	void clear();

	bool short_intersect(const pLockset ls) const;
	bool empty() const;
	bool operator==(const Lockset& ls) const;
	bool operator!=(const Lockset& ls) const;

	void intersect(pLockset ls);
	
	friend std::ostream& operator<<(std::ostream& os, const Lockset& ls);

friend class TimedLockset;
};

class TimedLockset {
public:
	std::map<uint64_t, uint64_t> timestamps;

	TimedLockset() {}
	TimedLockset(const std::map<uint64_t, uint64_t> & ts) : timestamps(ts) {}
	TimedLockset(pTimedLockset tls) : timestamps(tls->timestamps) {}
	
	void lock(uint64_t mutex, uint64_t timestamp);
	void unlock(uint64_t mutex);
	void lock_special(uint64_t mutex, uint64_t timestamp);
	void unlock_special(uint64_t mutex);
	void clear();

	bool empty() const;
	bool operator==(const TimedLockset& tls) const;
	bool operator!=(const TimedLockset& tls) const;
	Lockset to_lockset() const;

	void intersect(pTimedLockset tls);

	
	friend std::ostream& operator<<(std::ostream& os, const TimedLockset& tls);
};

#endif
