#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <libpmem2.h>

#define CACHE_LINE_SIZE 64
#define FILE_SIZE 1024UL*1024UL

typedef struct node {
    struct node * left, * right;
    char * key;
    int val;
} node_t;

node_t ** tree = NULL;

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

struct pm_pool {
  union {
    struct {
      size_t cur;
      pthread_mutex_t mutex
    } d;
    char p[CACHE_LINE_SIZE];
  } meta;
  char addr[];
};

struct pm_pool * memory;
size_t pm_ptr;

char * PM_ALLOC(size_t size) {
  pthread_mutex_lock(&memory.meta.d.mutex);

  char * ret = memory.addr + memory.meta.d.cur;

  memory.meta.d.cur += size;

  flush(&memory.meta, sizeof(memory.meta));
  fence();

  pthread_mutex_unlock(&memory.meta.d.mutex);
  return ret;
}

node_t * new_node(char * k, int v) {
  node_t * n =
	PM_ALLOC(sizeof(node_t));
  n->left = n->right = NULL; 
  n->val = v;
  n->key = PM_ALLOC(strlen(k)+1);
  strcpy(n->key, k);
  return n;
}

void put_persist(char * k, int v) {
  node_t * n = put(key, val);
  FLUSH(n, sizeof(node_t));
  FENCE();
}

node_t * put(char * k, int v) {
  LOCK(&tree_lock)
  node_t * n = *tree;
  while (n) {
    int cmp = strcmp(n->key, k);
    if (cmp == 0) {
      n->val = v;
      UNLOCK(&tree_lock);
      return n;
    }
    else if (cmp > 0) {
      if(n->left == NULL) {
        n->left = new_node(k, v);
        UNLOCK(&tree_lock);
        return n;
      }
      n = n->left;
    }
    else {...} // Same as left
  }

  *tree = new_node(key, val);
  UNLOCK(&tree_lock);
  return *tree;
}	

node_t * get(char * k, int v) {
  LOCK(&tree_lock)
  node_t * n = *tree;
  while (n) {
    int cmp = strcmp(n->key, k);
    if (cmp == 0) {
      int res = n->val;
      UNLOCK(&tree_lock);
      return res;
    }
    else if (cmp > 0)
      n = n->left;
    else
      n = n->right;       
  }
  UNLOCK(&tree_lock);
  return DEAULT_VALUE;
}   

void * get_thread(void * arg) {

  return NULL;
}

void * put_thread(void * arg) {

  return NULL;
}

int main(int argc, char *argv[]) {
  size_t size;

  open_pmem_pool(argv[1], &memory, &size);

  pthread_mutex_init(ock, NULL);

  tree = PM_ALLOC(sizeof(node_t *));
  *tree = NULL;
  flush(tree, sizeof(node_t *));
  fence();
  
  put_persist("123", 123);
  put_persist("122", 122);
  put_persist("127", 127);
  put_persist("125", 125);
  put_persist("111", 111);


  printf("%ld\n", get("123"));
  printf("%ld\n", get("122"));
  printf("%ld\n", get("127"));
  printf("%ld\n", get("125"));
  printf("%ld\n", get("111"));
  printf("%ld\n", get("110"));
  printf("%ld\n", get("120"));
  return 0;
}