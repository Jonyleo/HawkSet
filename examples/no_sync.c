#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <libpmem2.h>

#define CACHE_LINE_SIZE 64
#define FILE_SIZE 1024UL*1024UL

void flush(char * addr, int size) {
	while(size > 0) {
		asm volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)(addr)));

		size -= CACHE_LINE_SIZE;
		addr += CACHE_LINE_SIZE;
	} 
}

void fence() {
    asm volatile("sfence":::"memory");
}

void open_pmem_pool(const char * path, char **addr, size_t * size) {
	int fd;
	struct pmem2_config *cfg;
	struct pmem2_map *map;
	struct pmem2_source *src;
	pmem2_persist_fn persist;

	if(access(path, F_OK) != 0) {
		if ((fd = open(path, O_RDWR | O_CREAT, 0666)) < 0) {
			perror("open(create)");
			exit(1);
		}

		if(ftruncate(fd, FILE_SIZE) != 0) {
			perror("ftruncate");
			exit(1);
		}
	} else {
		if ((fd = open(path, O_RDWR, 0666)) < 0) {
			perror("open");
			exit(1);
		}
	}

	if (pmem2_config_new(&cfg)) {
		pmem2_perror("pmem2_config_new");
		exit(1);
	}

	if (pmem2_source_from_fd(&src, fd)) {
		pmem2_perror("pmem2_source_from_fd");
		exit(1);
	}

	if (pmem2_config_set_required_store_granularity(cfg,
			PMEM2_GRANULARITY_PAGE)) {
		pmem2_perror("pmem2_config_set_required_store_granularity");
		exit(1);
	}

	if (pmem2_map_new(&map, cfg, src)) {
		pmem2_perror("pmem2_map_new");
		exit(1);
	}

	*addr = (char *) pmem2_map_get_address(map);
	*size = pmem2_map_get_size(map);
}


void * write_value(void * addr) {
	((int *) addr)[0] = 12341234;
	flush((char *) addr, sizeof(int));
	fence();
	return NULL;
}

int main(int argc, char *argv[]) {
	char * addr;
	size_t size;

	open_pmem_pool(argv[1], &addr, &size);


	pthread_t tid;

	if(pthread_create(&tid, NULL, write_value, addr)) {
		perror("pthread_create");
		exit(-1);
	}

	printf("%d\n", ((int *)addr)[0]);

	if(pthread_join(tid, NULL)) {
		perror("pthread_join");
		exit(-1);
	}

	return 0;
}