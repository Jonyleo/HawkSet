#include <iostream>
#include <vector>

#include "vector_clock.hpp"

#include "unit_test.hpp"

enum VectorClockOp { // Arg1       | Arg2        |  Expected
	UPDATE_THREAD, 	 // VectorClock, ThreadId    -> Nop
	UPDATE,			 // VectorClock, VectorClock -> Nop
	IS_CONCURRENT,   // VectorClock, VectorClock -> Bool
	CHECK_CLOCK 	 // VectorClock, ThreadId    -> Int
};



class VectorClockTest : public Test<std::map<uint64_t, uint64_t>, VectorClockOp, uint64_t, uint64_t, uint64_t> {
private:
	std::vector<VectorClock> vector_clocks;

public:
	VectorClockTest(std::string name, std::string description) : Test(name, description) {}

	void preamble() {
		for(int i = 0; i < data.size(); i++) {
			vector_clocks.emplace_back();
			for(const auto entry : data[i]) {
				for(int count = 0; count < entry.second; count++)
					vector_clocks[i].update(entry.first);
			}
		}
	}

	void cleanup() {}

	bool do_op(VectorClockOp op, uint64_t expected, std::tuple<uint64_t, uint64_t> args) {
		bool res;
				
		switch(op) {
			case VectorClockOp::UPDATE_THREAD:
				vector_clocks[std::get<0>(args)].update(std::get<1>(args));
				break;
			case VectorClockOp::UPDATE:
				vector_clocks[std::get<0>(args)].update(vector_clocks[std::get<1>(args)]);
				break;
			case VectorClockOp::IS_CONCURRENT:
				res = vector_clocks[std::get<0>(args)].is_concurrent(vector_clocks[std::get<1>(args)]);
				if(res != expected) {
					print_fail();
					std::cout << "--- VectorClock::is_concurrent expected " << (bool) expected 
					<< " but received " << (bool) res << " ---" << std::endl;
					std::cout << vector_clocks[std::get<0>(args)] << std::endl;
					std::cout << vector_clocks[std::get<1>(args)] << std::endl;
					print_op();
					return false;
				}
				break;
			case VectorClockOp::CHECK_CLOCK:
				int val = vector_clocks[std::get<0>(args)].clock[std::get<1>(args)];
				if(val != expected) {
					print_fail();
					std::cout << "--- VectorClock value expected " << expected 
					<< " but received " << val << " ---" << std::endl;
					std::cout << vector_clocks[std::get<0>(args)] << " - " << std::get<1>(args) << std::endl;
					print_op();
					return false;
				}
				break;
		}

		return true;
	}
};


VectorClockTest test_update_thread() {
	VectorClockTest test_data("update_thread", "update vector clock for a specific tid");

	test_data.add_data({{0,1}, {1,5}, {2, 3}});

	test_data.add_op(VectorClockOp::UPDATE_THREAD, NONCE, 0, 0);
	test_data.add_op(VectorClockOp::UPDATE_THREAD, NONCE, 0, 0);
	test_data.add_op(VectorClockOp::UPDATE_THREAD, NONCE, 0, 4);
	test_data.add_op(VectorClockOp::UPDATE_THREAD, NONCE, 0, 1);
	test_data.add_op(VectorClockOp::UPDATE_THREAD, NONCE, 0, 1);
	test_data.add_op(VectorClockOp::UPDATE_THREAD, NONCE, 0, 3);
	test_data.add_op(VectorClockOp::UPDATE_THREAD, NONCE, 0, 2);
	test_data.add_op(VectorClockOp::UPDATE_THREAD, NONCE, 0, 2);


	test_data.add_op(VectorClockOp::CHECK_CLOCK, 3, 0, 0);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 7, 0, 1);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 5, 0, 2);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 1, 0, 3);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 1, 0, 4);

	return test_data;
}

VectorClockTest test_update() {
	VectorClockTest test_data("update", "update vector clock with another vector clock");

	test_data.add_data({{0,1}, {1,5}, {2, 3}});
	test_data.add_data({{0,3}, {1,2}, {2, 2}});
	test_data.add_data({{0,1}, {1,1}, {3, 1}});

	test_data.add_op(VectorClockOp::UPDATE, NONCE, 0, 1);

	test_data.add_op(VectorClockOp::CHECK_CLOCK, 3, 0, 0);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 5, 0, 1);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 3, 0, 2);

	test_data.add_op(VectorClockOp::UPDATE, NONCE, 0, 2);

	test_data.add_op(VectorClockOp::CHECK_CLOCK, 3, 0, 0);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 5, 0, 1);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 3, 0, 2);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 1, 0, 3);

	// Check if operand clocks did not get updated
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 3, 1, 0);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 2, 1, 1);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 2, 1, 2);

	test_data.add_op(VectorClockOp::CHECK_CLOCK, 1, 2, 0);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 1, 2, 1);
	test_data.add_op(VectorClockOp::CHECK_CLOCK, 1, 2, 3);

	return test_data;
}	
	
VectorClockTest test_is_concurrent() {
	VectorClockTest test_data("is_concurrent", "check if 2 vector clocks are concurrent");

	test_data.add_data({{0,1}, {1,5}, {2, 3}});
	test_data.add_data({{0,3}, {1,2}, {2, 2}});
	test_data.add_data({{0,3}, {1,2}, {2, 2}, {3, 3}});
	test_data.add_data({{0,1}, {1,1}, {3, 1}});

	test_data.add_op(VectorClockOp::IS_CONCURRENT, true, 0, 1);
	test_data.add_op(VectorClockOp::IS_CONCURRENT, false, 1, 2);
	test_data.add_op(VectorClockOp::IS_CONCURRENT, false, 2, 3);

	return test_data;
}	


int main() {
	test_update_thread().run();
	test_update().run();
	test_is_concurrent().run();
	return 0;
}