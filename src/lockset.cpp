#include <cstring>
#include <cassert>

#include "lockset.hpp"


MUTEX_DECL lock_register_mutex;
std::map<uint64_t, uint64_t> Lockset::lock_index;
std::atomic_ulong Lockset::lock_counter;

void Lockset::set_mutex_internal(uint64_t index, bool val) {
	uint64_t macro = index / 64;
	uint64_t micro = index % 64;

	if (macro >= locks.size()) {
		locks.resize(macro+1, false);
	}

	locks[macro].set(micro, val); 
}

void Lockset::set_mutex(uint64_t mutex, bool val) {
	set_mutex_internal(get_index(mutex), val);
}


void Lockset::register_mutex(uint64_t mutex) {
	MUTEX_LOCK(lock_register_mutex);
	if(!Lockset::lock_index.contains(mutex)) {
		Lockset::lock_index[mutex] = lock_counter++;
	}
	MUTEX_UNLOCK(lock_register_mutex);
}

uint64_t Lockset::get_index(uint64_t mutex) {
	return Lockset::lock_index[mutex];
}

std::string Lockset::get_mutex_id(uint64_t index) {
	for(const auto & entry : lock_index) {
		if(entry.second == index)
			return std::to_string(entry.first);
	}

	if(index < lock_counter)
		return "S" + std::to_string(index);

	return "N/A";
}

uint64_t Lockset::register_special_mutex() {
	return lock_counter++;
};

void Lockset::lock_special(uint64_t mutex) {
	set_mutex_internal(mutex, true);
}

void Lockset::unlock_special(uint64_t mutex) {
	set_mutex_internal(mutex, false);
}

void Lockset::lock(uint64_t mutex) {
	register_mutex(mutex);
	set_mutex(mutex, true);
}

void Lockset::unlock(uint64_t mutex) {
	register_mutex(mutex);
	set_mutex(mutex, false);
}

bool Lockset::short_intersect(const pLockset ls) const {
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

void Lockset::clear() {
	locks.clear();
}

bool Lockset::empty() const {
	for(auto  &v : locks) {
		if(v.any())
			return false;
	}

	return true;
}

bool Lockset::operator==(const Lockset& ls) const {
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

bool Lockset::operator!=(const Lockset& ls) const {
	return ! this->operator==(ls);
}

void Lockset::intersect(pLockset ls) {
	uint64_t min_size = std::min(this->locks.size(), ls->locks.size());

	this->locks.resize(min_size);

	for(size_t i = 0; i < min_size; i++) 
		this->locks[i] = this->locks[i] & ls->locks[i]; 
}

std::ostream& operator<<(std::ostream& os, const Lockset& ls) {
	os << "LOCKSET: [ ";
	for(const auto& entry : Lockset::lock_index) {
		uint64_t lock_i = entry.second;


		uint64_t macro = lock_i / 64;
		uint64_t micro = lock_i % 64;

		if (macro >= ls.locks.size())
			continue;

		if(ls.locks[macro].test(micro)) {
			os << Lockset::get_mutex_id(lock_i) << " ";
		}
	}
	os << "]";

	return os;
}

void TimedLockset::lock_special(uint64_t mutex, uint64_t timestamp) {
	timestamps[mutex] = timestamp;
}

void TimedLockset::unlock_special(uint64_t mutex) {
	timestamps.erase(mutex);
}

void TimedLockset::lock(uint64_t mutex, uint64_t timestamp) {
	Lockset::register_mutex(mutex);
	timestamps[Lockset::get_index(mutex)] = timestamp;
}

void TimedLockset::unlock(uint64_t mutex) {
	Lockset::register_mutex(mutex);
	timestamps.erase(Lockset::get_index(mutex));
}

void TimedLockset::clear() {
	timestamps.clear();
}


bool TimedLockset::empty() const {
	return timestamps.empty();
}

void TimedLockset::intersect(pTimedLockset tls) {	
	for(auto it = this->timestamps.begin(); it != this->timestamps.end();) {
		if(!tls->timestamps.contains(it->first) || tls->timestamps.at(it->first) != it->second)
			it = this->timestamps.erase(it);
		else
			++it;

	}
}


bool TimedLockset::operator==(const TimedLockset& tls) const {
	return this->timestamps == tls.timestamps;
}
bool TimedLockset::operator!=(const TimedLockset& tls) const {
	return ! this->operator==(tls);
}

Lockset TimedLockset::to_lockset() const {
	Lockset result;

	for(const auto & entry : this->timestamps) {
		result.lock_special(entry.first);
	}

	return result;
}

std::ostream& operator<<(std::ostream& os, const TimedLockset& tls) {
	os << "TIMEDLOCKSET: [ ";
	for(const auto& entry : tls.timestamps) {
		uint64_t mutex = entry.first;
		uint64_t ts = entry.second;

		os << "(" << Lockset::get_mutex_id(mutex) << "->" << ts << ") ";
	}
	os << "]";

	return os;
}