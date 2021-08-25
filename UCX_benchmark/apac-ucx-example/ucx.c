#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <mpi.h>
#include <pmix.h>
#include <ucp/api/ucp.h>

#include "comm-mpi.h"
#include "errors.h"
#include "common.h"

#include <emmintrin.h>
#include <smmintrin.h>

ucp_context_h ucp_context;
ucp_worker_h ucp_worker;
ucp_ep_h * endpoints;
ucp_rkey_h * rkeys;
ucp_mem_h register_buffer;
uint64_t * remote_addresses;

int my_pe;
int size;

double TIME()
{
    double retval;
    struct timeval tv;
    if (gettimeofday(&tv, NULL)) {
        perror("gettimeofday");
        abort();
    }

    retval = ((double) tv.tv_sec) * 1e6 + tv.tv_usec;
    return retval;
}


/* 
 * This will exchange networking information with all other PEs and 
 * register an allocated buffer with the local NIC. Will create endpoints 
 * if they are not already created. 
 */
int reg_buffer(void * buffer, size_t length)
{
    int i = 0;
    int error = 0;
    void ** pack = NULL;
    ucs_status_t status;
    ucp_mem_map_params_t mem_map_params;

    rkeys = (ucp_rkey_h *) malloc(sizeof(ucp_rkey_h) * size);
    if (NULL == rkeys) {
        error = ERR_NO_MEMORY;
        goto fail;
    }
    
    remote_addresses = (uint64_t *) malloc(sizeof(uint64_t) * size);
    if (NULL == remote_addresses) {
        error = ERR_NO_MEMORY;
        goto fail_endpoints;
    }
    
    mem_map_params.address    = buffer;
    mem_map_params.length     = length;
    mem_map_params.field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS
                              | UCP_MEM_MAP_PARAM_FIELD_LENGTH;
    status = ucp_mem_map(ucp_context, 
                        &mem_map_params, 
                        &register_buffer);
    if (UCS_OK != status) {
        error = -1;
        goto fail_full;
    }

    error = mpi_buffer_exchange(buffer,
                                &pack,
                                remote_addresses,
                                &register_buffer);
    if (OK != error) {
        goto fail_full;
    }

    /* unpack keys into rkey array */
    for (i = 0; i < size; i++) {
        int rkey_error;

        rkey_error = ucp_ep_rkey_unpack(endpoints[i], 
                                        pack[i], 
                                        &rkeys[i]);
        if (UCS_OK != rkey_error) {
            error = -1;
            goto fail_full;
        }

        ucp_rkey_buffer_release(pack[i]); 
        pack[i] = NULL;
    }

    // NOTE: it's OK to keep pack if going to unpack on other endpoints later
    free(pack);

    return OK;

fail_full:
    free(remote_addresses);
fail_endpoints:
    free(endpoints);
fail:
    free(rkeys);

    register_buffer = NULL;
    rkeys = NULL;
    remote_addresses = NULL;

    return error;
}

/*
 * This function creates the ucp endpoints used for communication by SharP.
 * This leverages MPI to perform the data exchange
 */
static inline int create_ucp_endpoints(void)
{
    int error = 0;
    void ** worker_addresses = NULL;
    ucp_ep_params_t ep_params;
    int i;
    
    endpoints = (ucp_ep_h *) malloc(size * sizeof(ucp_ep_h));
    if (NULL == endpoints) {
        return ERR_NO_MEMORY;
    }
    
    error = mpi_worker_exchange(&worker_addresses);
    if (OK != error) {
        free(endpoints);
        return -1;
    }
    
    for (i = 0; i < size; i++) {
        ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
        ep_params.address = (ucp_address_t *) worker_addresses[i];
        error = ucp_ep_create(ucp_worker,
                              &ep_params,
                              &endpoints[i]);
        if (UCS_OK != error) {
            free(endpoints);
            return -1;
        }
        free(worker_addresses[i]);
    }
    free(worker_addresses);
     
    return OK;
}

int comm_init()
{
    ucp_params_t ucp_params;
    ucp_config_t * config;
    ucs_status_t status;
    int error = 0;
    ucp_worker_params_t worker_params; 

    status = ucp_config_read(NULL, NULL, &config);
    if (status != UCS_OK) {
        return -1;
    }

    ucp_params.features = UCP_FEATURE_RMA | UCP_FEATURE_AMO64 | UCP_FEATURE_AMO32;
    ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES;

    status = ucp_init(&ucp_params, config, &ucp_context);
    if (status != UCS_OK) {
        return -1;
    }

    ucp_config_release(config);
    worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;
    worker_params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    status = ucp_worker_create(ucp_context, 
                               &worker_params, 
                               &ucp_worker);
    if (status != UCS_OK) {
        return -1;
    } 

    /* initialize communication channel for exchanges */
    init_mpi();

    /* create our endpoints here */
    error = create_ucp_endpoints();
    if (error != OK) {
        return -1;
    } 

    return 0;
}
/******************************/



void barrier()
{
    MPI_Barrier(MPI_COMM_WORLD);
}

int comm_finalize()
{
    barrier();
    ucp_request_param_t req_param = {0};
    ucs_status_ptr_t req;

    req = ucp_worker_flush_nbx(ucp_worker, &req_param);
    if (UCS_OK != req) {
        if (UCS_PTR_IS_ERR(req)) {
            abort();
        } else {
            while (ucp_request_check_status(req) == UCS_INPROGRESS) {
                ucp_worker_progress(ucp_worker);
            }
            ucp_request_free(req);
        }
    }

    for (int i = 0; i < size; i++) {
        if (rkeys[i]) {
            ucp_rkey_destroy(rkeys[i]);
        }

        if (endpoints[i]) {
            ucp_ep_destroy(endpoints[i]);
        }
    }

    free(remote_addresses);
    free(endpoints);
    ucp_mem_unmap(ucp_context, register_buffer);
    ucp_worker_destroy(ucp_worker);
    ucp_cleanup(ucp_context);

    finalize_mpi();
}   

int cmpfunc(const void * a, const void * b) 
{
    return ((*(double *)a) - (*(double *)b));
}

void bench(char *shared_ptr, char *sdata, int iter, int warmup, size_t data_size)
{
    double start, end;
    double bw = 0.0;
    double total = 0.0;
    ucp_request_param_t req_param = {0};
    ucs_status_ptr_t ucp_status;


    /* provide a warmup between endpoints */
    for (int i = 0; i < warmup; i++) {
        if (my_pe == 0) {
            ucp_status = ucp_put_nbx(endpoints[1], sdata, data_size, remote_addresses[1], rkeys[1], &req_param);
        } else {
            ucp_status = ucp_put_nbx(endpoints[0], sdata, data_size, remote_addresses[0], rkeys[0], &req_param);
        }
        if (UCS_OK != ucp_status) {
            if (UCS_PTR_IS_ERR(ucp_status)) {
                abort();
            } else {
                while (UCS_INPROGRESS == ucp_request_check_status(ucp_status)) {
                    ucp_worker_progress(ucp_worker);
                }
                ucp_request_free(ucp_status);
            }
        }
    }

    barrier();
    /* TODO: change this code to perform ping-pong latency */
    // if (my_pe == 0) {
    //     int j = 0;
    //     start = MPI_Wtime();
    //     for (int i = 0; i < iter; i++) {
    //         ucp_status = ucp_put_nbx(endpoints[1], &sdata[i * data_size], data_size, remote_addresses[1] + i * data_size, rkeys[1], &req_param);
    //         if (UCS_PTR_IS_PTR(ucp_status)) {
    //             ucp_request_free(ucp_status);
    //         } 
    //     }
    //     ucp_status = ucp_worker_flush_nbx(ucp_worker, &req_param);
    //     if (UCS_OK != ucp_status) {
    //         if (UCS_PTR_IS_ERR(ucp_status)) {
    //             abort();
    //         } else {
    //             while (UCS_INPROGRESS == ucp_request_check_status(ucp_status)) {
    //                 ucp_worker_progress(ucp_worker);
    //             }
    //             ucp_request_free(ucp_status);
    //         }
    //     }
    //     end = MPI_Wtime();

    //     total = iter / (end - start);
    //     bw = (1.0 * iter * data_size) / (end - start);

    //     printf("%-10ld", data_size);
    //     printf("%15.2f", ((end - start) * 1e6) / iter);
    //     printf("%15.2f", total);
    //     printf("%15.2f", bw / (1024 * 1024));
    //     printf("\n");
    // }

    if(my_pe == 0){
        // client
        start = MPI_Wtime();
        for (int i = 0; i < iter; i++) {
            *sdata = i;
            ucp_status = ucp_put_nbx(endpoints[1], sdata, data_size, remote_addresses[1], rkeys[1], &req_param);
            if (UCS_PTR_IS_PTR(ucp_status)) {
                ucp_request_free(ucp_status);
            } 
            ucp_status = ucp_worker_flush_nbx(ucp_worker, &req_param);
            if (UCS_OK != ucp_status) {
                if (UCS_PTR_IS_ERR(ucp_status)) {
                    abort();
                } else {
                    while (UCS_INPROGRESS == ucp_request_check_status(ucp_status)) {
                        ucp_worker_progress(ucp_worker);
                    }
                    ucp_request_free(ucp_status);
                }
            }
            printf("client: %d sent\n", i);
            // while(*shared_ptr != i){
            //     // printf("client: %d waiting on %d\n", i, *shared_ptr);
            //     // sleep(1);
            // }
            // do{
            //     _mm_clflush(shared_ptr);
            // }while(*shared_ptr != i);

            __m128i noncached;
            do{
                noncached = _mm_stream_load_si128(shared_ptr);
            }while(*(char *)&noncached != i);
            printf("client: %d received\n", i);
        }
        end = MPI_Wtime();

        total = iter / (end - start);
        bw = (1.0 * iter * data_size) / (end - start);

        printf("%-10ld", data_size);
        printf("%15.2f", ((end - start) * 1e6) / iter);
        // printf("%15.2f", total);
        // printf("%15.2f", bw / (1024 * 1024));
        printf("\n");
    }else{
        // server
        for (int i = 0; i < iter; i++) {
            // while(*shared_ptr != i){
            //     // printf("server: %d waiting on %d\n", i, *shared_ptr);
            //     // sleep(1);
            // }
            // do{
            //     _mm_clflush(shared_ptr);
            // }while(*shared_ptr != i);

            __m128i noncached;
            do{
                noncached = _mm_stream_load_si128(shared_ptr);
            }while(*(char *)&noncached != i);
            printf("server: %d received\n", i);
            *sdata = i;
            ucp_status = ucp_put_nbx(endpoints[0], sdata, data_size, remote_addresses[0], rkeys[0], &req_param);
            if (UCS_PTR_IS_PTR(ucp_status)) {
                ucp_request_free(ucp_status);
            } 
            ucp_status = ucp_worker_flush_nbx(ucp_worker, &req_param);
            if (UCS_OK != ucp_status) {
                if (UCS_PTR_IS_ERR(ucp_status)) {
                    abort();
                } else {
                    while (UCS_INPROGRESS == ucp_request_check_status(ucp_status)) {
                        ucp_worker_progress(ucp_worker);
                    }
                    ucp_request_free(ucp_status);
                }
            }
            printf("server: %d sent\n", i);
        }
    }

    barrier();
}


int main(void) 
{
    void * mybuff;
    char * shared_ptr;
    char * sdata;
    ucp_request_param_t req_param;
    
    /* initialize the runtime and communication components */
    comm_init();
    mybuff = malloc(HUGEPAGE * 8); // 16 MB
    sdata = (char *)malloc(HUGEPAGE * 8);
    memset(mybuff, 0xff, HUGEPAGE * 8);
    memset(sdata, 0xff, HUGEPAGE * 8);
    
    barrier();

    /* register memory  */
    reg_buffer(mybuff, HUGEPAGE * 8);
    
    shared_ptr = (char *) mybuff;

    // for (int i = 0; i < HUGEPAGE; i++) {
    //     shared_ptr[i] = (char) i;
    // }

    barrier();

    if (my_pe == 0) {
        printf("%-10s%15s%15s%15s\n", "Size", "Latency us", "Msg/s", "BW MB/s");
    }

    for (int i = 1; i <= HUGEPAGE * 8; i *= 2) {
        bench(shared_ptr, sdata, 100, 10, i);
    }

    comm_finalize();
    free(sdata);
    free(mybuff);
    return 0;
}
