#include "lockset.hpp"

PIN_MUTEX lock_register_mutex;
std::map<uint64_t, uint64_t> LockSet::lock_index;
std::atomic_ulong LockSet::lock_counter;

PIN_MUTEX lockset_cache_mutex;
std::unordered_set<pLockSet> locksets_cache;
pLockSet empty_lockset = NULL; 

PIN_MUTEX timedlockset_cache_mutex;
std::unordered_set<pTimedLockSet> timedlocksets_cache;
pTimedLockSet empty_timedlockset = NULL; 

void LockSet::set_mutex_internal(uint64_t index, bool val) {
	uint64_t macro = index / 64;
	uint64_t micro = index % 64;

	if (macro >= locks.size()) {
		locks.resize(macro+1, false);
	}

	locks[macro].set(micro, val); 
}

void LockSet::set_mutex(uint64_t mutex, bool val) {
	set_mutex_internal(get_index(mutex), val);
}


void LockSet::register_mutex(uint64_t mutex) {
	PIN_MutexLock(&lock_register_mutex);
	if(!LockSet::lock_index.contains(mutex)) {
		LockSet::lock_index[mutex] = lock_counter++;
	}
	PIN_MutexUnlock(&lock_register_mutex);
}

uint64_t LockSet::get_index(uint64_t mutex) {
	return LockSet::lock_index[mutex];
}

uint64_t LockSet::register_special_mutex() {
	return lock_counter++;
};

void LockSet::lock_special(uint64_t mutex) {
	set_mutex_internal(mutex, true);
}

void LockSet::unlock_special(uint64_t mutex) {
	set_mutex_internal(mutex, false);
}

void LockSet::lock(uint64_t mutex) {
	register_mutex(mutex);
	set_mutex(mutex, true);
}

void LockSet::unlock(uint64_t mutex) {
	register_mutex(mutex);
	set_mutex(mutex, false);
}

bool LockSet::simple_intersect(const pLockSet ls) const {
	if(this->empty() || ls->empty())
		return false;

	if(this == ls)
		return true;

	uint64_t min_size = std::min(locks.size(), ls->locks.size());

	for(size_t i = 0; i < min_size; i++) {
		std::bitset<64> res = locks.at(i) & ls->locks.at(i);

		if(res.any())
			return true;
	}

	return false;
}

bool LockSet::empty() const {
	for(auto  &v : locks) {
		if(v.any())
			return false;
	}

	return true;
}

bool LockSet::operator==(const LockSet& ls) const {
	if(locks.size() == ls.locks.size())
		return ! memcmp(locks.data(), ls.locks.data(), locks.size() * sizeof(std::bitset<64>));

	std::size_t min = std::min(locks.size(), ls.locks.size());

	if(memcmp(locks.data(), ls.locks.data(), min * sizeof(std::bitset<64>)) != 0) {
		return false;
	}

	auto & remainder = locks.size() != min ? this->locks : ls.locks;

	for (uint64_t i = min; i < remainder.size(); i++) {
		if(remainder[i].any())
			return false;
	}
	return true;
}

bool LockSet::operator!=(const LockSet& ls) const {
	return ! this->operator==(ls);
}



pLockSet LockSet::intersect(pLockSet ls) {
	if(this->empty())
		return this;

	if(ls->empty())
		return ls;

	if(this == ls)
		return this;

	LockSet result(this);

	uint64_t min_size = std::min(this->locks.size(), ls->locks.size());

	result.locks.resize(min_size);

	for(size_t i = 0; i < min_size; i++) 
		result.locks[i] = this->locks[i] & ls->locks[i]; 

	return lockset_cache_get(&result);
}

std::ostream& operator<<(std::ostream& os, const LockSet& ls) {
	os << "LOCKSET: [ ";
	for(const auto& entry : LockSet::lock_index) {
		uint64_t lock_id = entry.second;


		uint64_t macro = lock_id / 64;
		uint64_t micro = lock_id % 64;

		if (macro >= ls.locks.size())
			continue;

		if(ls.locks[macro].test(micro)) {
			os << lock_id << " ";
		}
	}
	os << "]";

	return os;
}

void TimedLockSet::lock_special(uint64_t mutex, uint64_t timestamp) {
	lockset->lock_special(mutex);
	timestamps[mutex] = timestamp;
}

void TimedLockSet::unlock_special(uint64_t mutex) {
	lockset->unlock_special(mutex);
	timestamps.erase(mutex);
}

void TimedLockSet::lock(uint64_t mutex, uint64_t timestamp) {
	lockset->lock(mutex);
	timestamps[LockSet::get_index(mutex)] = timestamp;
}

void TimedLockSet::unlock(uint64_t mutex) {
	lockset->unlock(mutex);
	timestamps.erase(LockSet::get_index(mutex));
}

bool TimedLockSet::empty() const {
	return lockset->empty();
}


pLockSet TimedLockSet::intersect(pTimedLockSet tls) const {
	if(this->empty())
		return this->lockset;

	if(tls->empty())
		return tls->lockset;

	if(this == tls)
		return this->lockset;

	pLockSet initial_result = this->lockset->intersect(tls->lockset);
	LockSet result(initial_result);

	uint64_t size = result.locks.size();
	bool modified = false;

	for(size_t i = 0; i < size; i++) {
		std::bitset<64> *res = &result.locks[i];
		std::bitset<64> tmp = *res;

		for(size_t j = 0; tmp.any(); tmp >>= 1, j++) {
			if(tmp.test(0)) {
				uint64_t mutex = 64*i + j;
				if(!this->timestamps.contains(mutex) || !tls->timestamps.contains(mutex) 
					|| this->timestamps.at(mutex) != tls->timestamps.at(mutex)) {
					res->set(j, 0);
					modified = true;
				}
			} 
		}
	}
	if(modified)
		return lockset_cache_get(&result);
	else
		return initial_result;
}

pLockSet lockset_cache_get(pLockSet ls) {
	PIN_MutexLock(&lockset_cache_mutex);

	auto lockset_iter = locksets_cache.find(ls);

	if(lockset_iter == locksets_cache.end()) {
		bool inserted = false;

		std::tie(lockset_iter, inserted) = 
		locksets_cache.insert(
			new LockSet(ls)
		);

		assert(inserted);
	} 

	auto ret = *lockset_iter;
	
	PIN_MutexUnlock(&lockset_cache_mutex);
	return ret;
}

pTimedLockSet timedlockset_cache_get(pTimedLockSet tls) {
	PIN_MutexLock(&timedlockset_cache_mutex);

	auto timedlockset_iter = timedlocksets_cache.find(tls);

	if(timedlockset_iter == timedlocksets_cache.end()) {
		bool inserted = false;

		std::tie(timedlockset_iter, inserted) =
		timedlocksets_cache.insert(
			new TimedLockSet(lockset_cache_get(tls->lockset), tls->timestamps)
		);

		assert(inserted);
	}

	auto ret = *timedlockset_iter;

	PIN_MutexUnlock(&timedlockset_cache_mutex);
	return ret;
}
