#ifndef __HAWKSET_VECTOR_CLOCK_HPP__
#define __HAWKSET_VECTOR_CLOCK_HPP__

#include <map>
#include <fstream>
#include <cstdint>
#include <atomic>

struct VectorClock {
	std::map<uint64_t, uint64_t> clock;

	void update(uint64_t tid) {
		if(!clock.contains(tid))
			clock[tid] = 0;

		clock[tid]++;
	}

	void update(const VectorClock& vc) {
		for(auto it = vc.clock.begin(); it != vc.clock.end(); it++) {
			uint64_t timestamp = it->second;
			uint64_t tid = it->first;
			if(!clock.contains(tid) ||  clock[tid] < timestamp)
				clock[tid] = timestamp;
		}
	}

	bool is_concurrent(const VectorClock& vc) const {
		bool greater = false;
		bool lesser = false;

		for(auto it = vc.clock.begin(); it != vc.clock.end(); it++) {
			uint64_t timestamp = it->second;
			uint64_t tid = it->first;
			if(!clock.contains(tid)) {
				lesser = true;
			}
			else {
				if(clock.at(tid) < timestamp) {
					lesser = true;
				}
				else if (clock.at(tid) > timestamp) {
					greater = true;
				}
			}
		}

		if(!lesser)
			return false;

		if(greater)
			return true;

		for(auto it = clock.begin(); it != clock.end(); it++) {
			uint64_t tid = it->first;
			if(!vc.clock.contains(tid)) {
				greater = true;
				break;
			}
		}

		return greater && lesser;
	}

	friend std::ostream& operator<<(std::ostream& os, const VectorClock& vc) {
		if(vc.clock.size() == 0) {
			os << "{0}" << std::endl;
			return os;
		}

		os << "[ ";

		for(auto it = vc.clock.begin(); it != vc.clock.end(); it++) {
			os << "(" <<it->first << ": " << it->second << ") ";
		}
		os << "]";

		return os;
	}
};

#endif