CC=gcc
CFLAGS=-O0
LDFLAGS=-lucp -luct -lucs -lucm 

.PHONY: all clean
all: ucx_bw

ucx_bw: ucp_client_server.c
	${CC} ${CFLAGS} -o $@ $^ ${LDFLAGS}

clean:
	rm ucx_bw
