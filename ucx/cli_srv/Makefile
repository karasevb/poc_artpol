
CC = /home/user/ompi/v2.x/build_ucx/install/bin/mpicc
CFLAGS = -g -O0 -I/sandbox/ucx/include/
LDFLAGS = -L/sandbox/ucx/lib -lucp -lucs

all: ucx_clisrv ucp_hello_world

ucx_clisrv: ucx_clisrv.c
	$(CC) -o ucx_clisrv ucx_clisrv.c $(CFLAGS) $(LDFLAGS)
	
ucp_hello_world: ucp_hello_world.c
	$(CC) -o ucp_hello_world ucp_hello_world.c $(CFLAGS) $(LDFLAGS) 
	