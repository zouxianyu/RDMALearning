#ifndef COMM_MPI_H
#define COMM_MPI_H

#include <stdint.h>

int init_mpi(void);
int finalize_mpi(void);

void create_mpi_datatype(void);

int mpi_buffer_exchange(void * buffer,
                        void *** pack_param,
                        uint64_t * remotes,
                        void * register_buffer);

int mpi_worker_exchange(void *** param_worker_addrs);


#endif
