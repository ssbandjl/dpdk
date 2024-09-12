#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_config.h>
#include <rte_ethdev.h>
#include <unistd.h>
#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
uint16_t virport[64];
int virportnum = 0;
struct lcore_conf
{
    unsigned n_rx_port;
    unsigned rx_port_list[16];
    int pkts;
} __rte_cache_aligned;

static struct lcore_conf lcore_conf_info[RTE_MAX_LCORE];

static inline int port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
    uint16_t portid = port;
    struct rte_eth_conf port_conf;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    uint16_t q;
    struct rte_eth_dev_info dev_info;
    int istx=0;

    if (!rte_eth_dev_is_valid_port(port))
        return -1;
    // 需要判断是否是虚拟网卡
    // 因为动态创建的网卡也会遍历进来，需要额外处理
    for (int i = 0; i < virportnum; i++)
    {
        if (port == virport[i])
        {
            istx=1;
            break;
        }
    }
    uint16_t rx_rings = 0, tx_rings = 0;
    if (istx == 1)
    {
        tx_rings = 1;
    }
    else
    {
        rx_rings = 1;
    }

    memset(&port_conf, 0, sizeof(struct rte_eth_conf));

    retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0)
    {
        printf("Error during getting device (port %u) info: %s\n",
               port, strerror(-retval));
        return retval;
    }

    if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
        port_conf.txmode.offloads |=
                RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0)
        return retval;

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0)
        return retval;
    // 创建的虚拟设备与物理设备没有区别，都需要初始化
    // 如果是物理设备，就是接收数据；如果是虚拟设备，就是发送数据
    if (istx == 0)
    {
        for (q = 0; q < rx_rings; q++)
        {
            retval = rte_eth_rx_queue_setup(port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
            if (retval < 0)
                return retval;
            retval = rte_eth_dev_set_ptypes(port, RTE_PTYPE_UNKNOWN, NULL, 0);
            if (retval < 0)
            {
                    printf("Port %u, Failed to disable Ptype parsing\n", port);
                    return retval;
            }
        }
    }
    else
    {
        for (q = 0; q < tx_rings; q++)
        {
            retval = rte_eth_tx_queue_setup(port, q, nb_txd, rte_eth_dev_socket_id(port), NULL);
            if (retval < 0)
                return retval;
        }

    }

    retval = rte_eth_dev_start(port);
    if (retval < 0)
        return retval;

    char portname[32];
    char portargs[256];

    struct rte_ether_addr addr;
    retval = rte_eth_macaddr_get(port, &addr);
    if (retval != 0)
        return retval;

    printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n", port, RTE_ETHER_ADDR_BYTES(&addr));

    // 如果是物理设备，就创建一个对应的虚拟设备
    if(istx==0)
    {
        snprintf(portname, sizeof(portname), "virtio_user%u", port);
        // 修改一下mac，避免与物理设备一致
        addr.addr_bytes[5]=1;
        // 创建虚拟设备参数，指定路径，设备名称，mac地址等
        snprintf(portargs, sizeof(portargs), "path=/dev/vhost-net,queues=1,queue_size=%u,iface=%s,mac=" RTE_ETHER_ADDR_PRT_FMT, RX_RING_SIZE, portname, RTE_ETHER_ADDR_BYTES(&addr));
        
        // 把设备加入到系统
        if (rte_eal_hotplug_add("vdev", portname, portargs) < 0)
            rte_exit(EXIT_FAILURE, "Cannot create paired port for port %u\n", port);

        uint16_t virportid = -1;
        // 通过设备名称获取设备id
        if (rte_eth_dev_get_port_by_name(portname, &virportid) != 0)
        {
            rte_eal_hotplug_remove("vdev", portname);
                rte_exit(EXIT_FAILURE, "cannot find added vdev %s:%s:%d\n", portname, __func__, __LINE__);
        }
        // 记录下虚拟设备id
        virport[virportnum] = virportid;
        virportnum++;
    }
    
    // 虚拟设备不可以开启混杂模式
    if(istx==0)
    {
        retval = rte_eth_promiscuous_enable(port);
        if (retval != 0)
            return retval;
        for (int i = 0; i < RTE_MAX_LCORE; i++)
        {
            if (rte_lcore_is_enabled(i) == 0)
            {
                continue;
            }

            if (i == rte_get_main_lcore())
            {
                continue;
            }

            if (lcore_conf_info[i].n_rx_port > 0)
            {
                continue;
            }

            struct lcore_conf *qconf = &lcore_conf_info[i];
            qconf->rx_port_list[qconf->n_rx_port] = port;
            qconf->n_rx_port++;
            break;
        }
    }

    return 0;
}

static int lcore_main(void *param)
{
    int ret;
    int lcore_id = rte_lcore_id();
    struct lcore_conf *qconf = &lcore_conf_info[lcore_id];

    int master_coreid = rte_get_main_lcore();
    uint16_t port;
    if (qconf->n_rx_port == 0)
    {
        printf("lcore %u has nothing to do\n", lcore_id);
        return 0;
    }

    if (lcore_id == rte_get_main_lcore())
    {
        printf("do not receive data in main core\n");
        return 0;
    }

    RTE_ETH_FOREACH_DEV(port)
    if (rte_eth_dev_socket_id(port) >= 0 &&
        rte_eth_dev_socket_id(port) !=
        (int) rte_socket_id())
        printf("WARNING, port %u is on remote NUMA node to "
               "polling thread.\n\tPerformance will "
               "not be optimal.\n", port);

    printf("\nCore %u forwarding packets. [Ctrl+C to quit]\n", rte_lcore_id());
    uint16_t portid;
    for (;;)
    {
        for (int i = 0; i < qconf->n_rx_port; i++)
        {
            int port = qconf->rx_port_list[i];
            portid = port;
            struct rte_mbuf *bufs[BURST_SIZE];
            uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

            if (unlikely(nb_rx == 0))
                continue;
            uint16_t nb_tx = 0;
            for (int i = 0; i < virportnum; i++)
            {
                // 找一个虚拟网卡发送出去
                // 这里只有一个设备，可以这样
                // 如果有多个，需要设定好一一对应关系再发送
                nb_tx = rte_eth_tx_burst(virport[i], 0, bufs, nb_rx);
                break;
            }

            for (int j = nb_tx; j < nb_rx; j++)
            {
                // 数据发送完后，会自动释放，没有发送的数据，需要手动释放
                rte_pktmbuf_free(bufs[j]);
            }
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    unsigned nb_ports;
    uint16_t portid;
    memset(lcore_conf_info, 0, sizeof(lcore_conf_info));
    memset(virport, -1, sizeof(virport));

    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    nb_ports = rte_eth_dev_count_avail();
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    // 这里遍历需要注意，遍历期间动态创建的虚拟设备也会被遍历到
    RTE_ETH_FOREACH_DEV(portid)
    if (port_init(portid, mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n", portid);

    rte_eal_mp_remote_launch(lcore_main, NULL, SKIP_MAIN);
    int lcore_id;
    RTE_LCORE_FOREACH_WORKER(lcore_id)
    {
        if (rte_eal_wait_lcore(lcore_id) < 0)
        {
            ret = -1;
            break;
        }
    }

    rte_eal_cleanup();

    return 0;
}


/* 
build and run, then hot_plug device virtio_user0:

ip a
...
70: virtio_user0: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN group default qlen 1000
    link/ether 1a:e0:f5:1f:21:01 brd ff:ff:ff:ff:ff:ff

 */

