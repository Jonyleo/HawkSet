CC:= g++
CFLAGS:= -pthread -std=c++2a
INCLUDE:= -I../src

EXECUTABLES:=unit_test_lockset unit_test_timedlockset unit_test_vector_clock test_cache

CFLAGS += -ggdb -DDEBUG -O0

all: ${EXECUTABLES}

run: ${EXECUTABLES}
	@$(foreach EXE, $(EXECUTABLES),echo $(EXE) && ./$(EXE) && echo &&) : ; :

unit_test_lockset: unit_test_lockset.cpp lockset.o ../src/lockset.hpp unit_test.hpp
	$(CC) $(CFLAGS) -o unit_test_lockset lockset.o unit_test_lockset.cpp $(INCLUDE)

unit_test_timedlockset: unit_test_timedlockset.cpp lockset.o ../src/lockset.hpp unit_test.hpp
	$(CC) $(CFLAGS) -o unit_test_timedlockset lockset.o unit_test_timedlockset.cpp $(INCLUDE)

lockset.o: ../src/lockset.hpp ../src/lockset.cpp
	$(CC) $(CFLAGS) -o lockset.o -c ../src/lockset.cpp $(INCLUDE)


unit_test_vector_clock: unit_test_vector_clock.cpp lockset.o ../src/vector_clock.hpp unit_test.hpp
	$(CC) $(CFLAGS) -o unit_test_vector_clock unit_test_vector_clock.cpp $(INCLUDE)


test_cache: test_cache.cpp lockset.o ../src/lockset.hpp ../src/vector_clock.hpp 
	$(CC) $(CFLAGS) -o test_cache lockset.o test_cache.cpp $(INCLUDE)

clean:
	rm ${EXECUTABLES} *.o -f

