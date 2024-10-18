#ifndef __HAWKSET_UNIT_TEST_HPP__
#define __HAWKSET_UNIT_TEST_HPP__

#define NONCE 0

template<typename DataType, typename OpType, typename ExpectedType, typename... ArgTypes>
class Test {
private:
	std::string name, description;

	std::vector<OpType> operations;
	std::vector<std::tuple<ArgTypes...>> arguments;
	std::vector<ExpectedType> _expected;

	int current_op = 0;

protected:
	std::vector<DataType> data;
	
	void print_fail() {
		std::cout << "\033[0;31m--- " << name << ": " << description << " - FAILED ---\033[0m" << std::endl;
	}
	void print_success() {
		std::cout << "\033[0;32m--- " << name << ": " << description << " - SUCCEEDED ---\033[0m" << std::endl;
	}

	void print_op() {
		std::cout << "--- Operation " << current_op << " ---" << std::endl;
	}
public:

	Test(std::string name, std::string description) : name(name), description(description) {}

	void add_op(OpType op, ExpectedType expected, ArgTypes... args) {
		operations.emplace_back(op);
		_expected.emplace_back(expected);
		arguments.emplace_back(args...);
	}

	void add_data(DataType input){
		data.push_back(input);
	}

	virtual void preamble() = 0;
	virtual bool do_op(OpType op, ExpectedType expected, std::tuple<ArgTypes...> args) = 0;
	virtual void cleanup() = 0;

	void run() {
		preamble();

		for(int i = 0; i < operations.size(); i++) {
			current_op = i;
			if(!do_op(operations[i], _expected[i], arguments[i])) {
				cleanup();
				
				exit(-1);
			}
		}

		cleanup();

		print_success();
	}
};

#endif