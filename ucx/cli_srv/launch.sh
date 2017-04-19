#!/bin/bash

ulimit -c unlimited

export LD_LIBRARY_PATH="/sandbox/ucx/lib:$LD_LIBRARY_PATH"

#export UCX_NET_DEVICES=mlx5_0:1
#export UCX_TLS=ud

/home/user/ompi/v2.x/build_ucx/install/bin/mpirun \
        -np $1 --mca btl self,vader,tcp \
        --mca pml ob1 --mca mtl ^mxm  \
        --map-by node \
        ./ucx_clisrv
