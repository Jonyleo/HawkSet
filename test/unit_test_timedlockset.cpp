#include <iostream>

#include "lockset.hpp"

#include "unit_test.hpp"

enum TimedLocksetOp { // Arg1   | Arg2   | Arg3 |  Expected
	LOCK, 			  // Lockset, Mutex,   TS   -> Nop
	UNLOCK,			  // Lockset, Mutex,   Nop  -> Nop
	LOCK_SPECIAL,	  // Lockset, Mutex,   TS   -> Nop 
	UNLOCK_SPECIAL,   // Lockset, Mutex,   Nop  -> Nop
	EMPTY,			  // Lockset, Nop,     Nop  -> Bool
	CLEAR,			  // Lockset, Nop,     Nop  -> Nop
	INTERSECT,		  // Lockset, Lockset, Nop  -> Nop
	EQUALS,   		  // Lockset, Lockset, Nop  -> Bool
	TO_LOCKSET_EMPTY  // Lockset, Nop,     Nop  -> Bool (Checks if result is empty)
};



class TimedLocksetTest : public Test<std::map<uint64_t, uint64_t>, TimedLocksetOp, uint64_t, uint64_t, uint64_t, uint64_t> {
private:
	std::vector<TimedLockset> locksets;

public:
	TimedLocksetTest(std::string name, std::string description) : Test(name, description) {}

	void preamble() {
		for(int i = 0; i < data.size(); i++) {
			locksets.emplace_back();
			for(const auto entry : data[i]) {
				locksets[i].lock(entry.first, entry.second);
			}
		}
	}

	void cleanup() {}

	bool do_op(TimedLocksetOp op, uint64_t expected, std::tuple<uint64_t, uint64_t, uint64_t> args) {
		bool res;
				
		switch(op) {
			case TimedLocksetOp::LOCK:
				locksets[std::get<0>(args)].lock(std::get<1>(args), std::get<2>(args));
				break;
			case TimedLocksetOp::UNLOCK:
				locksets[std::get<0>(args)].unlock(std::get<1>(args));
				break;
			case TimedLocksetOp::LOCK_SPECIAL:
				locksets[std::get<0>(args)].lock_special(std::get<1>(args), std::get<2>(args));
				break;
			case TimedLocksetOp::UNLOCK_SPECIAL:
				locksets[std::get<0>(args)].unlock_special(std::get<1>(args));
				break;
			case TimedLocksetOp::EMPTY:
				res = locksets[std::get<0>(args)].empty();
				if(res != expected) {
					print_fail();
					std::cout << "--- TimedLockset::empty expected " << (bool) expected 
					<< " but received " << (bool) res << " ---" << std::endl;
					print_op();
					return false;
				}
				break;
			case TimedLocksetOp::CLEAR:
				locksets[std::get<0>(args)].clear();
				break;
			case TimedLocksetOp::INTERSECT:
				locksets[std::get<0>(args)].intersect(& locksets[std::get<1>(args)]);
				break;
			case TimedLocksetOp::EQUALS:
				if(locksets[std::get<0>(args)].operator==(locksets[std::get<1>(args)]) != expected) {
					print_fail();
					std::cout << "--- TimedLockset::equals expected " << locksets[std::get<0>(args)]
					<< " but received " << locksets[std::get<1>(args)] << " ---" << std::endl;
					print_op();
					return false;
				}
				break;
			case TimedLocksetOp::TO_LOCKSET_EMPTY:
				res = locksets[std::get<0>(args)].to_lockset().empty();
				if(res != expected) {
					print_fail();
					std::cout << "--- TimedLockset::to_lockset::empty expected " << (bool) expected 
					<< " but received " << (bool) res << " ---" << std::endl;
					print_op();
					return false;
				}
				
				break;
		}

		return true;
	}
};


TimedLocksetTest test_empty() {
	TimedLocksetTest test_data("empty", "intersect empty locksets");

	test_data.add_data({});
	test_data.add_data({});

	test_data.add_op(TimedLocksetOp::EMPTY, true, 0, NONCE, NONCE);
	test_data.add_op(TimedLocksetOp::EMPTY, true, 1, NONCE, NONCE);

	test_data.add_op(TimedLocksetOp::INTERSECT, NONCE, 0, 1, NONCE);

	test_data.add_op(TimedLocksetOp::EMPTY, true, 0, NONCE, NONCE);
	test_data.add_op(TimedLocksetOp::EMPTY, true, 1, NONCE, NONCE);

	return test_data;
}

TimedLocksetTest test_lock_unlock() {
	TimedLocksetTest test_data("lock_unlock", "lock and unlock, including special");

	test_data.add_data({});
	
	test_data.add_op(TimedLocksetOp::EMPTY, true, 0, NONCE, NONCE);

	test_data.add_op(TimedLocksetOp::LOCK, NONCE,  0, 0, 5);
	test_data.add_op(TimedLocksetOp::LOCK_SPECIAL, NONCE, 0, 7, 10);
	
	test_data.add_op(TimedLocksetOp::EMPTY, false, 0, NONCE, NONCE);
	
	test_data.add_op(TimedLocksetOp::UNLOCK, NONCE,  0, 0, NONCE);
	test_data.add_op(TimedLocksetOp::UNLOCK_SPECIAL, NONCE, 0, 7, NONCE);
	
	test_data.add_op(TimedLocksetOp::EMPTY, true, 0, NONCE, NONCE);

	return test_data;
}	

TimedLocksetTest test_clear() {
	TimedLocksetTest test_data("clear", "clear lockset");

	test_data.add_data({});

	test_data.add_op(TimedLocksetOp::EMPTY, true, 0, NONCE, NONCE);

	for(int i = 0; i < 100; i++)
		test_data.add_op(TimedLocksetOp::LOCK, NONCE, 0, i+10000, 5);

	for(int i = 0; i < 100; i++)
		test_data.add_op(TimedLocksetOp::LOCK_SPECIAL, NONCE, 0, i, 6);
	
	test_data.add_op(TimedLocksetOp::EMPTY, false, 0, NONCE, NONCE);

	test_data.add_op(TimedLocksetOp::CLEAR, false, 0, NONCE, NONCE);

	test_data.add_op(TimedLocksetOp::EMPTY, true, 0, NONCE, NONCE);

	return test_data;
}

TimedLocksetTest test_equal() {
	TimedLocksetTest test_data("equal_simple", "compare locksets");

	test_data.add_data({});
	test_data.add_data({{1, 5}});
	test_data.add_data({{1, 5}});
	test_data.add_data({{1, 3}});
	
	test_data.add_op(TimedLocksetOp::EQUALS, false, 0, 1, NONCE);
	test_data.add_op(TimedLocksetOp::EQUALS, false, 0, 2, NONCE);
	test_data.add_op(TimedLocksetOp::EQUALS, true, 1, 2, NONCE);
	test_data.add_op(TimedLocksetOp::EQUALS, false, 0, 3, NONCE);

	test_data.add_op(TimedLocksetOp::EQUALS, false, 1, 0, NONCE);
	test_data.add_op(TimedLocksetOp::EQUALS, false, 2, 0, NONCE);
	test_data.add_op(TimedLocksetOp::EQUALS, true, 2, 1, NONCE);
	test_data.add_op(TimedLocksetOp::EQUALS, false, 3, 0, NONCE);

	return test_data;
}	
	
TimedLocksetTest test_intersect() {
	TimedLocksetTest test_data("intersect_simple", "intersect locksets");

	test_data.add_data({{1, 5}});
	test_data.add_data({{1, 5}, {2, 7}});
	test_data.add_data({{1, 5}});
	test_data.add_data({{1, 3}});
	test_data.add_data({{2, 7}});
	test_data.add_data({});
	

	test_data.add_op(TimedLocksetOp::INTERSECT, NONCE, 0, 1, NONCE);
	test_data.add_op(TimedLocksetOp::EQUALS, true, 0, 2, NONCE);
	test_data.add_op(TimedLocksetOp::INTERSECT, NONCE, 0, 3, NONCE);
	test_data.add_op(TimedLocksetOp::EQUALS, true, 0, 5, NONCE);
	test_data.add_op(TimedLocksetOp::INTERSECT, NONCE, 1, 4, NONCE);
	test_data.add_op(TimedLocksetOp::EQUALS, true, 1, 4, NONCE);
	
	return test_data;
}	

TimedLocksetTest test_to_lockset() {
	TimedLocksetTest test_data("to_lockset_simple", "convert timedlockset to lockset");

	test_data.add_data({{1, 5}});
	test_data.add_data({{1, 5}, {2, 7}});
	test_data.add_data({{1, 5}});
	test_data.add_data({{1, 3}});
	test_data.add_data({{2, 7}});
	test_data.add_data({});
	
	test_data.add_op(TimedLocksetOp::TO_LOCKSET_EMPTY, false, 0, NONCE, NONCE);
	test_data.add_op(TimedLocksetOp::TO_LOCKSET_EMPTY, false, 1, NONCE, NONCE);
	test_data.add_op(TimedLocksetOp::TO_LOCKSET_EMPTY, false, 2, NONCE, NONCE);
	test_data.add_op(TimedLocksetOp::TO_LOCKSET_EMPTY, false, 3, NONCE, NONCE);
	test_data.add_op(TimedLocksetOp::TO_LOCKSET_EMPTY, false, 4, NONCE, NONCE);
	test_data.add_op(TimedLocksetOp::TO_LOCKSET_EMPTY, true, 5, NONCE, NONCE);
	
	return test_data;
}	

int main() {
	test_empty().run();
	test_lock_unlock().run();
	test_clear().run();
	test_equal().run();
	test_intersect().run();
	test_to_lockset().run();
	return 0;
}