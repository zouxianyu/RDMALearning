#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ucp/api/ucp.h>
#include <mpi.h>

#include "comm-mpi.h"
#include "errors.h"
#include "common.h"

MPI_Datatype mpi_worker_exchange_dt;
MPI_Datatype mpi_buffer_exchange_dt; 

int mpi_worker_exchange(void *** param_worker_addrs)
{
    struct worker_exchange * rx;
    struct worker_exchange * dx;
    void ** worker_addresses;
    size_t worker_len;
    void * worker_address;
    int error;
    int i;
    int ret = 0;

    /* allocate */
    worker_addresses = (void **) malloc(sizeof(void *) * size);
    if (NULL == worker_addresses) {
        return ERR_NO_MEMORY;
    }


    error = ucp_worker_get_address(ucp_worker,
                                   (ucp_address_t **) &worker_address,
                                   &worker_len);
    if(error < 0) {
        free(worker_addresses);
        return -1;
    }

    /* pack */
    rx = (struct worker_exchange *) 
        malloc(sizeof(struct worker_exchange) * size);
    if (NULL == rx) {
        ret = ERR_NO_MEMORY;
        goto fail_pack;
    }

    dx = (struct worker_exchange *) malloc(sizeof(struct worker_exchange));
    if (NULL == dx) {
        ret = ERR_NO_MEMORY;
        free(rx);
        goto fail_pack;
    }

    dx->worker_len = worker_len;
    memcpy(&dx->worker, worker_address, worker_len);

    /* exchange */
    error = MPI_Allgather(dx,
                          1,
                          mpi_worker_exchange_dt,
                          rx,
                          1,
                          mpi_worker_exchange_dt,
                          MPI_COMM_WORLD);
    if (error != MPI_SUCCESS) {
        ret = -1;
        goto fail_exchange;
    }

    /* set up */
    for (i = 0; i < size; i++) {
        worker_addresses[i] = malloc(rx[i].worker_len);
        if (NULL == worker_addresses[i]) {
            ret = ERR_NO_MEMORY;
            goto fail_setup;
        }

        memcpy(worker_addresses[i], rx[i].worker, rx[i].worker_len);        
    }

    free(dx);
    free(rx);
    *param_worker_addrs = worker_addresses;
    
    return ret;

fail_setup:
    for (--i; i >= 0; i--) {
        free(worker_addresses[i]);
    }
fail_exchange:
    free(rx);
    free(dx);
fail_pack:
    free(worker_addresses);
    free(endpoints);

    return ret;
}

int mpi_buffer_exchange(void * buffer,
                        void *** pack_param,
                        uint64_t * remotes,
                        void * register_buffer)
{
    int error = 0;
    void ** pack = NULL;
    struct data_exchange * dx;
    struct data_exchange * rx = NULL;
    ucp_mem_h * mem = (ucp_mem_h *)register_buffer;
    size_t pack_size; 
    int ret = 0, i;
    ucs_status_t status;

    pack = (void **) malloc(sizeof(void *)*size);
    if (NULL == pack) {
        ret = ERR_NO_MEMORY;
        goto fail_mpi;
    }

    status = ucp_rkey_pack(ucp_context, *mem, &pack[my_pe], 
                           &pack_size);
    if (status != UCS_OK) {
        ret = error;
        goto fail_mpi;
    }

    remotes[my_pe] = (uint64_t)buffer;
/*
    TODO:
1. I need to pack all of my data into a buffer, preferably a MPI_Datatype
2. I need to perform a mpi_allgather() on the data
3. I need to loop through the data and pull out the necessary parts
*/

    /* step 1: create a data type */
    rx = (struct data_exchange *)malloc(
                                    sizeof(struct data_exchange)*size);
    dx = (struct data_exchange *)malloc(sizeof(struct data_exchange));
    dx->pack_size = pack_size;
    memcpy(&dx->pack, pack[my_pe], pack_size);
    dx->remote = remotes[my_pe];

    /* step 2: perform the allgather on the data */
    MPI_Allgather(dx, 
                  1, 
                  mpi_buffer_exchange_dt, 
                  rx, 
                  1, 
                  mpi_buffer_exchange_dt, 
                  MPI_COMM_WORLD);


    /* step 3: loop over rx and pull out the necessary parts */ 
    /* obtain the network information here... */
    for (i = 0; i < size; i++) {
        if (i == my_pe) {
            continue;
        }
        /*FIXME: i'm ignoring the worker length and pack size */
        remotes[i] = rx[i].remote;
        pack[i] = malloc(rx[i].pack_size);
        if (NULL == pack[i]) {
            ret = ERR_NO_MEMORY;
            goto fail_purge_arrays;
        }
        memcpy(pack[i], rx[i].pack, pack_size);
    }
    
    free(rx);
    free(dx);
    *pack_param = pack; 

    return ret;

fail_purge_arrays:
    for (--i; i >= 0; i--) {
        free(pack[i]);
    }
fail_mpi:
    if (rx != NULL) {
        free(rx);
    }
    if (NULL != pack) {
        free(pack);
    }
    return ret;
}

void create_mpi_datatype(void)
{
    int buffer_nr_items = 3;
    MPI_Aint buffer_displacements[3];
    int buffer_block_lengths[3] = {1,1,600};
    MPI_Datatype buffer_exchange_types[3] = {MPI_UINT64_T,
                                             MPI_UINT64_T,
                                             MPI_BYTE};

    /* MPI Datatype for ucp worker address exchange */
    int worker_nr_items = 2;
    MPI_Aint worker_displacements[2];
    int worker_block_lengths[2] = {1, 600};
    MPI_Datatype worker_exchange_types[2] = {MPI_UINT64_T, MPI_BYTE};

    buffer_displacements[0] = offsetof(struct data_exchange, pack_size);
    buffer_displacements[1] = offsetof(struct data_exchange, remote);
    buffer_displacements[2] = offsetof(struct data_exchange, pack);

    worker_displacements[0] = offsetof(struct worker_exchange, worker_len);
    worker_displacements[1] = offsetof(struct worker_exchange, worker);        

    /* create an exchange data type for group creation/buffer registration */
    MPI_Type_create_struct(buffer_nr_items, 
                           buffer_block_lengths, 
                           buffer_displacements, 
                           buffer_exchange_types, 
                           &mpi_buffer_exchange_dt);
    MPI_Type_commit(&mpi_buffer_exchange_dt);

    /* create an exchange data type for UCP worker information exchange */
    MPI_Type_create_struct(worker_nr_items,
                           worker_block_lengths,
                           worker_displacements,
                           worker_exchange_types,
                           &mpi_worker_exchange_dt);
    MPI_Type_commit(&mpi_worker_exchange_dt);
}

int init_mpi(void)
{
    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_pe);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    create_mpi_datatype(); 
    return 0;
}

int finalize_mpi(void)
{
    MPI_Type_free(&mpi_worker_exchange_dt);
    MPI_Type_free(&mpi_buffer_exchange_dt);
    MPI_Finalize();
}
