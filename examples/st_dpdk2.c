/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <stdint.h>
#include <stdlib.h>

#include<sys/types.h>
#include<sys/socket.h>
#include<sys/un.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<poll.h>

#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>


//#define IPV4_DST_IP_FILTER
#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

#define sock_buf_sz 8096
#define PRINTF(fmt,...) printf("[%s] "fmt, get_cur_time(), ##__VA_ARGS__)

in_addr_t qemu_pf_ip[4]={0};
int qemu_pf_number = 0;

static int connected_eda2dpdk = 0;
static int connected_dpdk2eda = 0;
static char dut2dpi_addr[100] = "/home/common/st_socket/st0/dut2dpi";
static char dpi2dut_addr[100] = "/home/common/st_socket/st0/dpi2dut";
/* st_dpdk.c base on Basic DPDK skeleton forwarding example. */

char* get_cur_time()
{
    static char s[20];
    time_t t;
    struct tm* ltime;

    time(&t);
    ltime=localtime(&t);
    strftime(s,20,"%H:%M:%S",ltime);
    return s;
}

static inline void
edamsg2pkt(struct rte_mbuf *pkt, unsigned char *edamsg)
{
    int edamsg_type = edamsg[0];
    int edamsg_data_len = ((int)edamsg[2]<<8) + ((int)edamsg[1]) - 3;
    unsigned char *edamsg_data = edamsg + 3;

    PRINTF("recv msg from eda, type %d, size %d\n", edamsg_type, edamsg_data_len);
    for(int i=0;i<edamsg_data_len;i++)
    {
		if (i%16==0) printf("\t");
        printf("%02x ", edamsg_data[i]);
        if (i%16==15) printf("\n");
    }
    printf("\n");

    rte_pktmbuf_reset_headroom(pkt);
    pkt->data_len = edamsg_data_len;
    pkt->pkt_len = edamsg_data_len;
    pkt->next = NULL;
    pkt->nb_segs = 1;
    pkt->ol_flags &= RTE_MBUF_F_EXTERNAL;

    rte_memcpy(rte_pktmbuf_mtod_offset(pkt, char *, 0),
            edamsg_data, edamsg_data_len);
}

static inline void
pkt2edamsg(unsigned char *edamsg, struct rte_mbuf *pkt)
{
    int edamsg_type = 12;
    int edamsg_len = pkt->pkt_len+3;
    unsigned char *edamsg_data = edamsg + 3;

    edamsg[0] = edamsg_type;
    edamsg[1] = edamsg_len&0xff;
    edamsg[2] = ((edamsg_len)>>8)&0xff;

    rte_memcpy(edamsg_data, rte_pktmbuf_mtod_offset(pkt, char *, 0),
            pkt->pkt_len);

    printf("\tsend msg to eda, type %d, size %d\n", edamsg_type, pkt->pkt_len);
    for(int i=0;i<pkt->pkt_len;i++)
    {
		if (i%16==0) printf("\t");
        printf("%02x ", edamsg_data[i]);
        if (i%16==15) printf("\n");
    }
    printf("\n");
}

static inline void
init_server(int* listenfd_o, int* epollfd_o, char* uds_addr)
{
    //创建一个监听socket
    int listenfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenfd == -1)
    {
        PRINTF("%s create listen socket error\n", __FUNCTION__);
        exit(0);
    }

    //设置重用IP地址和端口号
    int on = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, (char*)&on, sizeof(on));

    //将监听socker设置为非阻塞的
    int oldSocketFlag = fcntl(listenfd, F_GETFL, 0);
    int newSocketFlag = oldSocketFlag | O_NONBLOCK;
    if (fcntl(listenfd, F_SETFL, newSocketFlag) == -1)
    {
        close(listenfd);
        PRINTF("%s set listenfd to nonblock error\n", __FUNCTION__);
        exit(0);
    }

    //初始化服务器地址
    unlink(uds_addr);
    struct sockaddr_un bindaddr;
    bindaddr.sun_family = AF_UNIX;
    strcpy(bindaddr.sun_path,uds_addr);

    if (bind(listenfd, (struct sockaddr*) & bindaddr, sizeof(bindaddr)) == -1)
    {
        PRINTF("%s bind listen socker error\n", __FUNCTION__);
        close(listenfd);
        exit(0);
    }

    //启动监听
    if (listen(listenfd, SOMAXCONN) == -1)
    {
        PRINTF("%s listen error\n", __FUNCTION__);
        close(listenfd);
        exit(0);
    }

    //创建epollfd
    int epollfd = epoll_create(1);
    if (epollfd == -1)
    {
        PRINTF("%s create epollfd error\n", __FUNCTION__);
        close(listenfd);
        exit(0);
    }

    struct epoll_event listen_fd_event;
    listen_fd_event.data.fd = listenfd;
    listen_fd_event.events = EPOLLIN;
    //若取消注释这一行，则使用ET模式
    listen_fd_event.events |= EPOLLET;

    //将监听sokcet绑定到epollfd上
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &listen_fd_event) == -1)
    {
        PRINTF("%s epoll_ctl error\n", __FUNCTION__);
        close(listenfd);
        exit(0);
    }

    *listenfd_o = listenfd;
    *epollfd_o = epollfd;

    PRINTF("%s server start success\n", uds_addr);
    char cmd[256];
    sprintf(cmd,"chmod 777 %s",uds_addr);
    system(cmd);
}

static inline int
deal_listenfd_epollin(int listenfd, int epollfd)
{
    //侦听socket，接受新连接
    struct sockaddr_un clientaddr;
    socklen_t clientaddrlen = sizeof(clientaddr);
    int clientfd = accept(listenfd, (struct sockaddr*) & clientaddr, &clientaddrlen);
    if (clientfd != -1)
    {
        int oldSocketFlag = fcntl(clientfd, F_GETFL, 0);
        int newSocketFlag = oldSocketFlag | O_NONBLOCK;
        if (fcntl(clientfd, F_SETFL, newSocketFlag) == -1)
        {
            close(clientfd);
            PRINTF("%s set clientfd to nonblocking error\n", __FUNCTION__);
        }
        else
        {
            struct epoll_event client_fd_event;
            client_fd_event.data.fd = clientfd;
            //同时侦听新来连接socket的读写
            client_fd_event.events = EPOLLIN | EPOLLOUT;
            //若取消注释这一行，则使用ET模式
            client_fd_event.events |= EPOLLET;
            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientfd, &client_fd_event) != -1)
            {
                PRINTF("new client accepted, clientfd: %d\n", clientfd);
            }
            else
            {
                PRINTF("%s add client fd to epollfd error\n", __FUNCTION__);
                close(clientfd);
            }
        }
    }
    return clientfd;
}

static inline int
deal_clientfd_epollin(int clientfd, int epollfd, char *recvbuf)
{
    //读取数据
    int m = recv(clientfd, recvbuf, sock_buf_sz, 0);
    if (m == 0)
    {
        //对端关闭了连接，从epollfd上移除clientfd
        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL) != -1)
        {
            PRINTF("%s client disconnected, clientfd: %d\n", __FUNCTION__, clientfd);
        }
        close(clientfd);
    }
    else if (m < 0)
    {
        //出错
        if (errno != EWOULDBLOCK && errno != EINTR)
        {
            if (epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL) != -1)
            {
                PRINTF("%s client disconnected, clientfd: %d\n", __FUNCTION__, clientfd);
            }
            close(clientfd);
        }
    }
    else
    {
        //正常收到数据
        PRINTF("%s recv a msg from eda: %d\n", __FUNCTION__, clientfd);
        struct epoll_event client_fd_event;
        client_fd_event.data.fd = clientfd;
        //同时侦听新来连接socket的读写
        client_fd_event.events = EPOLLIN | EPOLLOUT;
        //若取消注释这一行，则使用ET模式
        client_fd_event.events |= EPOLLET;
        if (epoll_ctl(epollfd, EPOLL_CTL_MOD, clientfd, &client_fd_event) != -1)
        {
            //PRINTF("%s reset event successfully, mode: EPOLL_CTL_MOD, clientfd: %d\n", __FUNCTION__, clientfd);
        }
    }

    return m;
}

static inline int
deal_clientfd_epollout(int clientfd, int epollfd, char* sendbuf)
{
    int m = send(clientfd, sendbuf, sock_buf_sz, 0);

    if (m < 0)
    {
        //出错
        if (errno != EWOULDBLOCK && errno != EINTR)
        {
            if (epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL) != -1)
            {
                PRINTF("%s client disconnected, clientfd: %d\n", __FUNCTION__, clientfd);
            }
            close(clientfd);
        }
    }
    else
    {
        struct epoll_event client_fd_event;
        client_fd_event.data.fd = clientfd;
        //同时侦听新来连接socket的读写
        client_fd_event.events = EPOLLIN | EPOLLOUT;
        //若取消注释这一行，则使用ET模式
        client_fd_event.events |= EPOLLET;
        if (epoll_ctl(epollfd, EPOLL_CTL_MOD, clientfd, &client_fd_event) != -1)
        {
            //PRINTF("%s reset event successfully, mode: EPOLL_CTL_MOD, clientfd: %d\n", __FUNCTION__, clientfd);
        }
    }

    return m;
}

static inline uint16_t
deal_eda_dpdk_msg(int listenfd, int epollfd, struct rte_mbuf **mbufs, struct rte_mempool *mbuf_pool, int isEda2Dpdk)
{
    uint16_t trans_cnt = 0;
    uint16_t epoll_cnt = 0;

    while(true)
    {
        if (epoll_cnt == 3 || trans_cnt>0)
        {
            return trans_cnt;
        }
        epoll_cnt = epoll_cnt + 1;

        struct epoll_event epoll_events[BURST_SIZE];
        int n = epoll_wait(epollfd, epoll_events, BURST_SIZE, 1);

        if (n < 0)
        {
            //被信号中断
            if (errno == EINTR)
                continue;

            //出错，退出
            PRINTF("%s epoll_wait error\n", __FUNCTION__);
            exit(0);
        }
        else if (n == 0)
        {
            //超时，继续
            continue;
        }

        for (size_t i = 0; i < n; ++i)
        {
            //有读事件
            if (epoll_events[i].events & EPOLLIN)
            {
                if (epoll_events[i].data.fd == listenfd)
                {
                    int fd = deal_listenfd_epollin(listenfd, epollfd);
                    if (fd>0)
                    {
                        if (isEda2Dpdk)
                        {
                            connected_eda2dpdk++;
                            PRINTF("connected_eda2dpdk %d(+1), connected_dpdk2eda %d\n", connected_eda2dpdk, connected_dpdk2eda);
                        }
                        else
                        {
                            connected_dpdk2eda++;
                            PRINTF("connected_eda2dpdk %d, connected_dpdk2eda %d(+1)\n", connected_eda2dpdk, connected_dpdk2eda);
                        }
                    }
                }
                else
                {
                    char recvbuf[sock_buf_sz] = { 0 };
                    int m = deal_clientfd_epollin(epoll_events[i].data.fd, epollfd, recvbuf);

                    if (m > 0)
                    {
                        if (isEda2Dpdk)
                        {
                            // st_flag
                            mbufs[trans_cnt] = rte_pktmbuf_alloc(mbuf_pool);
                            if(mbufs[trans_cnt] == NULL)
                            {
                                rte_exit(EXIT_FAILURE, "rte_pktmbuf_alloc failed\n");
                            }
                            edamsg2pkt(mbufs[trans_cnt], recvbuf);
                            trans_cnt = trans_cnt + 1;
                        }
                        else
                        {
                            //PRINTF("%s EPOLLIN triggered, clientfd: %d, ignore\n", __FUNCTION__, epoll_events[i].data.fd);
                        }
                    }
                    else
                    {
                        if (isEda2Dpdk)
                        {
                            connected_eda2dpdk--;
                            PRINTF("connected_eda2dpdk %d(-1), connected_dpdk2eda %d\n", connected_eda2dpdk, connected_dpdk2eda);
                        }
                        else
                        {
                            connected_dpdk2eda--;
                            PRINTF("connected_eda2dpdk %d, connected_dpdk2eda %d(-1)\n", connected_eda2dpdk, connected_dpdk2eda);
                        }
                    }
                }//普通clientfd
            } // EPOLLIN
            else if (epoll_events[i].events & EPOLLOUT)
            {
                //只处理客户端fd的写事件
                if (epoll_events[i].data.fd != listenfd)
                {
                    //打印结果
                    if (isEda2Dpdk)
                    {
                        //PRINTF("%s EPOLLOUT triggered,clientfd: %d, ignore\n", __FUNCTION__, epoll_events[i].data.fd);
                    }
                    else
                    {
                        if (mbufs[0] != NULL)
                        {
                            char sendbuf[sock_buf_sz] = { 0 };
                            pkt2edamsg(sendbuf, mbufs[0]);
                            int m = deal_clientfd_epollout(epoll_events[i].data.fd, epollfd, sendbuf);
                            if (m<0)
                            {
                                connected_dpdk2eda--;
                                PRINTF("connected_eda2dpdk %d, connected_dpdk2eda %d(-1)\n", connected_eda2dpdk, connected_dpdk2eda);
                            }
                        }
                        else
                        {
                            struct epoll_event client_fd_event;
                            client_fd_event.data.fd = epoll_events[i].data.fd;
                            //同时侦听新来连接socket的读写
                            client_fd_event.events = EPOLLIN | EPOLLOUT;
                            //若取消注释这一行，则使用ET模式
                            client_fd_event.events |= EPOLLET;
                            if (epoll_ctl(epollfd, EPOLL_CTL_MOD, epoll_events[i].data.fd, &client_fd_event) != -1)
                            {
                                //PRINTF("%s reset event successfully, mode: EPOLL_CTL_MOD, clientfd: %d\n", __FUNCTION__, clientfd);
                            }
                        }
                        trans_cnt = trans_cnt + 1;
                    }
                }
            }
            else if (epoll_events[i].events & EPOLLERR)
            {
                //TODO 暂不处理
            }
        }
    }
}

static inline uint16_t
deal_eda2dpdk_msg(int listenfd, int epollfd, struct rte_mbuf **mbufs, struct rte_mempool *mbuf_pool)
{
    return deal_eda_dpdk_msg(listenfd, epollfd, mbufs, mbuf_pool, 1);
}

static inline uint16_t
deal_dpdk2eda_msg(int listenfd, int epollfd, struct rte_mbuf *mbuf)
{
    return deal_eda_dpdk_msg(listenfd, epollfd, &(mbuf), NULL, 0);
}

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */

/* Main functional part of port initialization. 8< */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
    struct rte_eth_conf port_conf;
    const uint16_t rx_rings = 1, tx_rings = 1;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    uint16_t q;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;

    if (!rte_eth_dev_is_valid_port(port))
        return -1;

    memset(&port_conf, 0, sizeof(struct rte_eth_conf));

    retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0) {
        PRINTF("Error during getting device (port %u) info: %s\n",
                port, strerror(-retval));
        return retval;
    }

    if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
        port_conf.txmode.offloads |=
            RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

    /* Configure the Ethernet device. */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0)
        return retval;

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0)
        return retval;

    /* Allocate and set up 1 RX queue per Ethernet port. */
    for (q = 0; q < rx_rings; q++) {
        retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
                rte_eth_dev_socket_id(port), NULL, mbuf_pool);
        if (retval < 0)
            return retval;
    }

    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    /* Allocate and set up 1 TX queue per Ethernet port. */
    for (q = 0; q < tx_rings; q++) {
        retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                rte_eth_dev_socket_id(port), &txconf);
        if (retval < 0)
            return retval;
    }

    /* Starting Ethernet port. 8< */
    retval = rte_eth_dev_start(port);
    /* >8 End of starting of ethernet port. */
    if (retval < 0)
        return retval;

    /* Display the port MAC address. */
    struct rte_ether_addr addr;
    retval = rte_eth_macaddr_get(port, &addr);
    if (retval != 0)
        return retval;

    PRINTF("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
               " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
            port, RTE_ETHER_ADDR_BYTES(&addr));

    /* Enable RX in promiscuous mode for the Ethernet device. */
    retval = rte_eth_promiscuous_enable(port);
    /* End of setting RX port in promiscuous mode. */
    if (retval != 0)
        return retval;

    return 0;
}
/* >8 End of main functional part of port initialization. */

/*
 * The lcore eda2dpdk. recv pkt from eda
 */

 /* Basic forwarding application lcore. 8< */
static __rte_noreturn void
lcore_eda2dpdk_tx(struct rte_mempool *mbuf_pool)
{
    uint16_t port;

    /*
     * Check that the port is on the same NUMA node as the polling thread
     * for best performance.
     */
    RTE_ETH_FOREACH_DEV(port)
    {
        if (rte_eth_dev_socket_id(port) >= 0 &&
                rte_eth_dev_socket_id(port) !=
                        (int)rte_socket_id())
            PRINTF("WARNING, port %u is on remote NUMA node to "
                    "polling thread.\n\tPerformance will "
                    "not be optimal.\n", port);
    }

    PRINTF("\nCore %u forwarding eda2dpdk packets. [Ctrl+C to quit]\n",
            rte_lcore_id());

    int listenfd, epollfd;
    init_server(&listenfd, &epollfd, dut2dpi_addr);

      /* Main work of application loop. 8< */
    for (;;) {
        RTE_ETH_FOREACH_DEV(port)
        {
            struct rte_mbuf *bufs[BURST_SIZE];
            const uint16_t nb_rx = deal_eda2dpdk_msg(listenfd, epollfd, bufs, mbuf_pool);

            if (unlikely(nb_rx == 0))
                continue;

            const uint16_t nb_tx = rte_eth_tx_burst(port, 0,
                    bufs, nb_rx);

            PRINTF("l%d p%d: dpdk recv %d pkt from eda, and send %d pkt to net\n", rte_lcore_id(), port, nb_rx, nb_tx);
            /* Free any unsent packets. */
            if (unlikely(nb_tx < nb_rx)) {
                uint16_t buf;
                for (buf = nb_tx; buf < nb_rx; buf++)
                    rte_pktmbuf_free(bufs[buf]);
            }
        }
    }
    /* >8 End of loop. */

    close(listenfd);
}
/* >8 End Basic forwarding application lcore. */

/*
 * The lcore dpdk2eda. send pkt to eda
 */


 /* Basic forwarding application lcore. 8< */
static int
lcore_dpdk2eda_rx(void *arg)
{
    uint16_t port;

    /*
     * Check that the port is on the same NUMA node as the polling thread
     * for best performance.
     */
    RTE_ETH_FOREACH_DEV(port)
    {
        if (rte_eth_dev_socket_id(port) >= 0 &&
                rte_eth_dev_socket_id(port) !=
                        (int)rte_socket_id())
            PRINTF("WARNING, port %u is on remote NUMA node to "
                    "polling thread.\n\tPerformance will "
                    "not be optimal.\n", port);
    }

    PRINTF("\nCore %u forwarding dpdk2eda packets. [Ctrl+C to quit]\n",
            rte_lcore_id());

    int listenfd, epollfd;
    init_server(&listenfd, &epollfd, dpi2dut_addr);

    /* Main work of application loop. 8< */
    for (;;) {
        RTE_ETH_FOREACH_DEV(port)
        {
            struct rte_mbuf *bufs[BURST_SIZE];
            struct rte_ether_hdr *hdr;
            const uint16_t nb_rx = rte_eth_rx_burst(port, 0,
                    bufs, BURST_SIZE);

            if (unlikely(nb_rx == 0))
            {
                continue;
            }

            PRINTF("l%d p%d: dpdk recv %d pkt from net\n", rte_lcore_id(), port, nb_rx);
            uint16_t buf;
            for (buf = 0; buf < nb_rx; buf++)
            {
#ifdef IPV4_DST_IP_FILTER
                struct rte_ipv4_hdr *ipv4_hdr;
                int qemu_pf_id;
                hdr = rte_pktmbuf_mtod(bufs[buf], struct rte_ether_hdr *);
                if(hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
                {
                    printf("this is IPV4\n");
                    ipv4_hdr = rte_pktmbuf_mtod_offset(bufs[buf], struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
                    for (qemu_pf_id = 0; qemu_pf_id < qemu_pf_number; qemu_pf_id++)
                    {
                        if (ipv4_hdr->dst_addr == qemu_pf_ip[qemu_pf_id])
                        {
                            printf("this is right dst addr\n");
                            deal_dpdk2eda_msg(listenfd, epollfd, bufs[buf]);
                            rte_pktmbuf_free(bufs[buf]);
                        }
                    }
                }
#else
                printf("\tno filter mode\n");
                deal_dpdk2eda_msg(listenfd, epollfd, bufs[buf]);
                rte_pktmbuf_free(bufs[buf]);
#endif
            }
        }
    }
    /* >8 End of loop. */
}
/* >8 End Basic forwarding application lcore. */

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int
main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    unsigned nb_ports;
    uint16_t portid;
    unsigned lcore_id;

    struct in_addr addr;

    int i = 0;
    /* Initializion the Environment Abstraction Layer (EAL). 8< */
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    /* >8 End of initialization the Environment Abstraction Layer (EAL). */

    argc -= ret;
    argv += ret;

    if(argc>1)
    {
        sprintf(dut2dpi_addr, "/home/common/st_socket/%s/dut2dpi", argv[1]);
        sprintf(dpi2dut_addr, "/home/common/st_socket/%s/dpi2dut", argv[1]);
    }

    for (i = 2; i < argc; i++)
    {
        //is ipv4 addr?
        if (inet_pton(AF_INET, argv[i], &addr) > 0)
        {
            qemu_pf_number++;
            if (qemu_pf_number > 4)
            {
                rte_exit(EXIT_FAILURE, "Too many ipv4 addr\n");
            }
            qemu_pf_ip[i-2] = inet_addr(argv[i]);
            printf("qemu_pf_ip[%d] is %s inet ip is %x\n", i, argv[i], qemu_pf_ip[i-2]);
        }
    }

    /* Check that there is an even number of ports to send/receive on. */
    nb_ports = rte_eth_dev_count_avail();
    if (nb_ports < 1)
        rte_exit(EXIT_FAILURE, "Error: need at least 1 port\n");

    /* Creates a new mempool in memory to hold the mbufs. */

    /* Allocates mempool to hold the mbufs. 8< */
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    /* >8 End of allocating mempool to hold mbuf. */

    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    /* Initializing all ports. 8< */
    RTE_ETH_FOREACH_DEV(portid)
    {
        if (port_init(portid, mbuf_pool) != 0)
            rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n",
                    portid);
    }
    /* >8 End of initializing all ports. */

    if (rte_lcore_count() < 2)
        rte_exit(EXIT_FAILURE, "need 2 lcore\n");

    /* Launches the function on each lcore. 8< */
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        /* Simpler equivalent. 8< */
        rte_eal_remote_launch(lcore_dpdk2eda_rx, NULL, lcore_id);
        /* >8 End of simpler equivalent. */
        break; // only need 1 another core
    }

    /* call it on main lcore too */
    lcore_eda2dpdk_tx(mbuf_pool);
    /* >8 End of launching the function on each lcore. */

    rte_eal_mp_wait_lcore();

    /* clean up the EAL */
    rte_eal_cleanup();

    return 0;
}
