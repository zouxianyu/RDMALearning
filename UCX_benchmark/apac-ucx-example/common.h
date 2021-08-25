#ifndef COMMON_H
#define COMMON_H

// 4 kB page
#define PAGESIZE   (1<<12)
// 2 MB page
#define HUGEPAGE   (1<<21) 

struct data_exchange {
    size_t pack_size;
    uint64_t remote;
    char pack[600]; 
};

struct worker_exchange {
    size_t worker_len;
    char worker[600];
};

extern ucp_context_h ucp_context;
extern ucp_worker_h ucp_worker;
extern ucp_ep_h * endpoints;
extern ucp_rkey_h * rkeys;
extern ucp_mem_h register_buffer;
extern uint64_t * remote_addresses;

extern int my_pe;
extern int size;


#endif
