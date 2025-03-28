CC:= gcc
CFLAGS:= -pthread -lpmem2
LIBS:= -lpmem2

EXECUTABLES:=simple_race two_bugs no_race no_sync n_threads_init disjoint_access adq_rel test bug_fiesta

CFLAGS += -ggdb -DDEBUG -O0


all: ${EXECUTABLES}

bug_fiesta: bug_fiesta.c
	$(CC) $(CFLAGS) -o bug_fiesta bug_fiesta.c $(LIBS)

simple_race: simple_race.c
	$(CC) $(CFLAGS) -o simple_race simple_race.c $(LIBS)

two_bugs: two_bugs.c
	$(CC) $(CFLAGS) -o two_bugs two_bugs.c $(LIBS)

no_race: no_race.c
	$(CC) $(CFLAGS) -o no_race no_race.c $(LIBS)

no_sync: no_sync.c
	$(CC) $(CFLAGS) -o no_sync no_sync.c $(LIBS)

n_threads_init: n_threads_init.c
	$(CC) $(CFLAGS) -o n_threads_init n_threads_init.c $(LIBS)

disjoint_access: disjoint_access.c
	$(CC) $(CFLAGS) -o disjoint_access disjoint_access.c $(LIBS)

test: test.cpp
	$(CC) $(CFLAGS) -o test test.cpp $(LIBS) -L/usr/local/lib -lpmemobj


adq_rel: adq_rel.c
	$(CC) $(CFLAGS) -o adq_rel adq_rel.c $(LIBS)


clean:
	rm ${EXECUTABLES} -f

