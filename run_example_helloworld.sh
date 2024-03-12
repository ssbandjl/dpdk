export RTE_LOG_LEVEL=8
or --log-level=8

cd build/examples;gdb --args ./dpdk-helloworld -c f -n 4 --log-level=8

#ali u22
cd build/examples;gdb --args ./dpdk-helloworld -c 1 -n 2
