#include <iostream>
#include <vector>

#include "lockset.hpp"
#include "vector_clock.hpp"

#include "cache.hpp"

void test_no_repeat_lockset() {
	// No repeated objects (pointer-wise)
	for(const auto item : locksets_cache) {
		int c = 0;
		for(const auto item_ : locksets_cache) {
			if(item == item_)
				c++;
		}
		assert(c == 1);
	}

	// No repeated objects (data-wise)
	for(const auto item : locksets_cache) {
		int c = 0;
		for(const auto item_ : locksets_cache) {
			if(*item == *item_)
				c++;
		}
		assert(c == 1);
	}
}

void test_no_repeat_timedlockset() {
	// No repeated objects (pointer-wise)
	for(const auto item : timedlocksets_cache) {
		int c = 0;
		for(const auto item_ : timedlocksets_cache) {
			if(item == item_)
				c++;
		}
		assert(c == 1);
	}

	// No repeated objects (data-wise)
	for(const auto item : timedlocksets_cache) {
		int c = 0;
		for(const auto item_ : timedlocksets_cache) {
			if(*item == *item_)
				c++;
		}
		assert(c == 1);
	}
}

void print_lockset_cache() {
	for(const auto item : locksets_cache) {
		std::cout << *item << std::endl;
	}
}


void print_timedlockset_cache() {
	for(const auto item : timedlocksets_cache) {
		std::cout << *item << std::endl;
	}
}

void test_lockset_cache() {
	std::vector<std::vector<uint64_t>> data { 
		{ 1, 4, 6, 7, 8 },
		{ 1, 4, 6, 7, 8 },
		{ 1, 4, 6, 7, 8 },
		{ 1, 4, 6, 7, 8, 9 },
		{ 1, 4, 5, 9, 10 },
		{ 1, 2, 5, 6, 7, 9, 10 },
		{ 1, 2, 3, 5, 7, 9, 10 },
		{ 1, 3, 5, 6, 8, 9, 10 },
		{ 2, 3, 4, 6, 7, 8, 10 },
		{ 2, 3, 4, 5, 8, 9, 10 },
		{ 1, 2, 3, 4, 7, 8 },
		{ 2, 4, 5, 7, 8, 9 },
		{ 1, 3, 5, 7, 9, 10 },
		{ 2, 4, 5, 7, 9, 10 },
		{ 1, 2, 3, 4, 8, 10 } 
	};

	std::vector<pLockset> locksets;

	for(const auto & d : data) {
		Lockset ls;

		for(const auto v : d) {
			ls.lock(v);
		}

		locksets.push_back(lockset_cache_get(&ls));
	}

	assert(locksets_cache.size() == 13);

	intersect_lockset(locksets[3], locksets[4]);

	assert(locksets_cache.size() == 14);

	test_no_repeat_lockset();
}

void test_timedlockset_cache() {
	std::vector<std::map<uint64_t, uint64_t>> data { 
		{ {1, 3}, {4, 5}, {6, 4}, {7, 0}, {8, 6} },
		{ {1, 3}, {4, 5}, {6, 4}, {7, 0}, {8, 6} },
		{ {1, 3}, {4, 5}, {6, 4}, {7, 0}, {8, 6} },
		{ {1, 3}, {4, 4}, {6, 4}, {7, 7}, {8, 1}, {9, 2} },
		{ {1, 8}, {4, 8}, {5, 6}, {9, 3}, {10, 4} },
		{ {1, 0}, {2, 2}, {5, 5}, {6, 0}, {7, 6}, {9, 1}, {10, 2} },
		{ {1, 6}, {2, 3}, {3, 2}, {5, 8}, {7, 3}, {9, 1}, {10, 0} },
		{ {1, 0}, {3, 6}, {5, 5}, {6, 7}, {8, 6}, {9, 4}, {10, 2} },
		{ {2, 0}, {3, 7}, {4, 0}, {6, 3}, {7, 7}, {8, 7}, {10, 4} },
		{ {2, 2}, {3, 5}, {4, 1}, {5, 5}, {8, 3}, {9, 3}, {10, 6} },
		{ {1, 4}, {2, 8}, {3, 4}, {4, 0}, {7, 3}, {8, 6} },
		{ {2, 8}, {4, 8}, {5, 0}, {7, 3}, {8, 5}, {9, 2} },
		{ {1, 5}, {3, 3}, {5, 5}, {7, 3}, {9, 3}, {10, 2} },
		{ {2, 2}, {4, 7}, {5, 4}, {7, 6}, {9, 8}, {10, 5} },
		{ {1, 4}, {2, 0}, {3, 0}, {4, 8}, {8, 3}, {10, 8} } 
	};

	std::vector<pTimedLockset> locksets;

	for(const auto & d : data) {
		TimedLockset ls;

		for(const auto v : d) {
			ls.lock(v.first, v.second);
		}

		locksets.push_back(timedlockset_cache_get(&ls));
	}
	locksets_cache.clear();

	test_no_repeat_lockset();
	test_no_repeat_timedlockset();

	assert(timedlocksets_cache.size() == 13);

	intersect_timedlockset(locksets[2], locksets[3]);
	intersect_timedlockset(locksets[2], locksets[3]);

	assert(locksets_cache.size() == 1);

	test_no_repeat_lockset();
	test_no_repeat_timedlockset();
}

int main() {
	test_lockset_cache();
	test_timedlockset_cache();
	return 0;
}