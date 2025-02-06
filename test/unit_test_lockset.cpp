#include <iostream>

#include "lockset.hpp"

#include "unit_test.hpp"

enum LocksetOp {      // Arg1   | Arg2    |  Expected
	LOCK, 			  // Lockset, Mutex   -> Nop
	UNLOCK,			  // Lockset, Mutex   -> Nop
	LOCK_SPECIAL,	  // Lockset, Mutex   -> Nop 
	UNLOCK_SPECIAL,   // Lockset, Mutex   -> Nop
	EMPTY,			  // Lockset, Nop     -> Bool
	CLEAR,			  // Lockset, Nop     -> Nop
	INTERSECT,		  // Lockset, Lockset -> Nop
	SHORT_INTERSECT,  // Lockset, Lockset -> Bool
	EQUALS   		  // Lockset, Lockset -> Bool
};



class LocksetTest : public Test<std::vector<uint64_t>, LocksetOp, uint64_t, uint64_t, uint64_t> {
private:
	std::vector<Lockset> locksets;

public:
	LocksetTest(std::string name, std::string description) : Test(name, description) {}

	void preamble() {
		for(int i = 0; i < data.size(); i++) {
			locksets.emplace_back();
			for(uint64_t l : data[i]) {
				locksets[i].lock(l);
			}
		}
	}

	void cleanup() {}

	bool do_op(LocksetOp op, uint64_t expected, std::tuple<uint64_t, uint64_t> args) {
		bool res;
				
		switch(op) {
			case LocksetOp::LOCK:
				locksets[std::get<0>(args)].lock(std::get<1>(args));
				break;
			case LocksetOp::UNLOCK:
				locksets[std::get<0>(args)].unlock(std::get<1>(args));
				break;
			case LocksetOp::LOCK_SPECIAL:
				locksets[std::get<0>(args)].lock_special(std::get<1>(args));
				break;
			case LocksetOp::UNLOCK_SPECIAL:
				locksets[std::get<0>(args)].unlock_special(std::get<1>(args));
				break;
			case LocksetOp::EMPTY:
				res = locksets[std::get<0>(args)].empty();
				if(res != expected) {
					print_fail();
					std::cout << "--- Lockset::empty expected " << (bool) expected 
					<< " but received " << (bool) res << " ---" << std::endl;
					print_op();
					return false;
				}
				break;
			case LocksetOp::CLEAR:
				locksets[std::get<0>(args)].clear();
				break;
			case LocksetOp::INTERSECT:
				locksets[std::get<0>(args)].intersect(& locksets[std::get<1>(args)]);
				break;
			case LocksetOp::SHORT_INTERSECT:
				res = locksets[std::get<0>(args)].short_intersect(& locksets[std::get<1>(args)]);
				if(res != expected) {
					print_fail();
					std::cout << "--- Lockset::short_intersect expected " << (bool) expected 
					<< " but received " << (bool) res << " ---" << std::endl;
					print_op();
					return false;
				}
				break;
			case LocksetOp::EQUALS:
				if(locksets[std::get<0>(args)].operator==(locksets[std::get<1>(args)]) != expected) {
					print_fail();
					std::cout << "--- Lockset::equals expected " << locksets[std::get<0>(args)]
					<< " but received " << locksets[std::get<1>(args)] << " ---" << std::endl;
					print_op();
					return false;
				}
				break;
		}

		return true;
	}
};



LocksetTest test_empty() {
	LocksetTest test_data("empty", "intersect empty locksets");

	test_data.add_data({});
	test_data.add_data({});

	test_data.add_op(LocksetOp::EMPTY, true, 0, 0);
	test_data.add_op(LocksetOp::EMPTY, true, 1, 0);
	test_data.add_op(LocksetOp::INTERSECT, NONCE, 0, 1);
	test_data.add_op(LocksetOp::EMPTY, true, 0, 0);
	test_data.add_op(LocksetOp::EMPTY, true, 1, 0);

	return test_data;
}

LocksetTest test_lock_unlock() {
	LocksetTest test_data("lock_unlock", "lock and unlock, including special");

	test_data.add_data({});
	
	test_data.add_op(LocksetOp::EMPTY, true, 0, 0);
	test_data.add_op(LocksetOp::LOCK, NONCE,  0, 0);
	test_data.add_op(LocksetOp::LOCK_SPECIAL, NONCE, 0, 7);
	test_data.add_op(LocksetOp::EMPTY, false, 0, 0);
	test_data.add_op(LocksetOp::UNLOCK, NONCE,  0, 0);
	test_data.add_op(LocksetOp::UNLOCK_SPECIAL, NONCE, 0, 7);
	test_data.add_op(LocksetOp::EMPTY, true, 0, 0);

	return test_data;
}	

LocksetTest test_clear() {
	LocksetTest test_data("clear", "clear lockset");

	test_data.add_data({});

	test_data.add_op(LocksetOp::EMPTY, true, 0, 0);

	for(int i = 0; i < 100; i++)
		test_data.add_op(LocksetOp::LOCK, NONCE, 0, i+10000);

	for(int i = 0; i < 100; i++)
		test_data.add_op(LocksetOp::LOCK_SPECIAL, NONCE, 0, i);
	
	test_data.add_op(LocksetOp::EMPTY, false, 0, 0);

	test_data.add_op(LocksetOp::CLEAR, false, 0, 0);

	test_data.add_op(LocksetOp::EMPTY, true, 0, 0);

	return test_data;
}

LocksetTest test_equal() {
	LocksetTest test_data("equal_simple", "compare locksets");

	test_data.add_data({});
	test_data.add_data({1});
	test_data.add_data({1});
	
	test_data.add_op(LocksetOp::EQUALS, false, 0, 1);
	test_data.add_op(LocksetOp::EQUALS, false, 0, 2);
	test_data.add_op(LocksetOp::EQUALS, true, 1, 2);

	test_data.add_op(LocksetOp::EQUALS, false, 1, 0);
	test_data.add_op(LocksetOp::EQUALS, false, 2, 0);
	test_data.add_op(LocksetOp::EQUALS, true, 2, 1);

	return test_data;
}	
	
LocksetTest test_intersect() {
	LocksetTest test_data("intersect_simple", "intersect locksets");

	test_data.add_data({1});
	test_data.add_data({1,2});
	test_data.add_data({1});
	test_data.add_data({2});
	test_data.add_data({});
	test_data.add_data({1});
	test_data.add_data({1,2});
	test_data.add_data({});
	test_data.add_data({2});
	
	test_data.add_op(LocksetOp::INTERSECT, NONCE, 0, 1);
	test_data.add_op(LocksetOp::EQUALS, true, 0, 2);
	test_data.add_op(LocksetOp::INTERSECT, NONCE, 0, 3);
	test_data.add_op(LocksetOp::EQUALS, true, 0, 4);

	test_data.add_op(LocksetOp::SHORT_INTERSECT, true, 5, 6);
	test_data.add_op(LocksetOp::SHORT_INTERSECT, false, 5, 7);
	test_data.add_op(LocksetOp::SHORT_INTERSECT, false, 5, 8);

	return test_data;
}	

int main() {
	test_empty().run();
	test_lock_unlock().run();
	test_clear().run();
	test_equal().run();
	test_intersect().run();
	return 0;
}