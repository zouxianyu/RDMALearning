CC=mpicc
CFLAGS=-O0
LDFLAGS=-lucp -luct -lucs -lucm 

.PHONY: all clean
all: ucx_mr_bw

ucx_mr_bw: ucx.c mpi.c 
	${CC} ${CFLAGS} -o $@ $^ ${LDFLAGS}

clean:
	rm ucx_mr_bw
