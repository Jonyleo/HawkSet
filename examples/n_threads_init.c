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

pthread_mutex_t lock;
pthread_mutex_t init_lock;

void * global = NULL;
int irh;

void * initialize(void * addr) {
	pthread_mutex_lock(&init_lock);

	((int *) addr)[0] += 1;
	flush((char *) addr, sizeof(int));
	fence();	

	if(((int *) addr)[0] >= irh) {
		global = addr;
	}

	pthread_mutex_unlock(&init_lock);

	return NULL;
}

void * read_global(void * v) {
	pthread_mutex_lock(&lock);
	printf("%d\n", ((int *)global)[0]);
	pthread_mutex_unlock(&lock);
	return NULL;
}

void usage(char * file) {
	printf("Usage for %s: ./%s <pm-file> <n>\n", file, file);
	printf("\tpm-file: persistent memory backed file\n");
	printf("\tn: number of threads for init (>=2)\n\n");
	printf("\tThe init threads all access the same shared value without races\n"
		    "\tbetween them and finally publish the pointer\n\n");
	printf("\tIf n >= irh then no bug should be reported, if n < irh a bug should be reported\n");
}
 
int main(int argc, char *argv[]) {
	char * addr;
	size_t size;

	if(argc != 3) {
		usage(argv[0]);
		exit(-1);
	}

	open_pmem_pool(argv[1], &addr, &size);
	irh =  atoi(argv[2]);
	if(irh < 2) {
		usage(argv[0]);
		exit(-1);
	}

	pthread_mutex_init(&lock, NULL);
	pthread_mutex_init(&init_lock, NULL);

	pthread_t tid[irh];

	*((int *) addr) = 1;
	flush((char *) addr, sizeof(int));
	fence();	

	for(int i = 1; i < irh; i++) {
		if(pthread_create(tid + i, NULL, initialize, addr)) {
			perror("pthread_create");
			exit(-1);
		}
	}

	while(!global);

	

	if(pthread_create(tid, NULL, read_global, NULL)) {
		perror("pthread_create");
		exit(-1);
	}

	for(int i = 0; i < irh; i++) {
		if(pthread_join(tid[i], NULL)) {
			perror("pthread_join");
			exit(-1);
		}
	}

	pthread_mutex_destroy(&lock);
	pthread_mutex_destroy(&init_lock);

	return 0;
}