/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>

#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>

/* Launch a function on lcore. 8< */
static int
lcore_hello(__rte_unused void *arg)
{
	unsigned lcore_id;
	lcore_id = rte_lcore_id();
	printf("hello from core %u\n", lcore_id);
	return 0;
}
/* >8 End of launching function on lcore. */

/* Initialization of Environment Abstraction Layer (EAL). 8< */

// ./<build_dir>/examples/dpdk-helloworld -l 0-3 -n 4
int
main(int argc, char **argv)
{
	int ret;
	unsigned lcore_id;

	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_panic("Cannot init EAL\n");
	/* >8 End of initialization of Environment Abstraction Layer */

	/* Launches the function on each lcore. 8< */
	RTE_LCORE_FOREACH_WORKER(lcore_id) {
		/* Simpler equivalent. 8< */
    /*在另一个 lcore 上启动一个函数。 仅在 MAIN lcore 上执行。 向处于 WAIT 状态的 worker lcore（由 worker_id 标识）发送消息（在第一次调用 rte_eal_init() 后为真）。 这可以通过首先调用 rte_eal_wait_lcore(worker_id) 来检查。 当远程 lcore 收到消息时，它会切换到 RUNNING 状态，然后使用参数 arg 调用函数 f。 执行完成后，远程 lcore 切换到 WAIT 状态，f 的返回值存储在本地变量中，可使用 rte_eal_wait_lcore() 读取。 MAIN lcore 消息一发送就返回，并且对 f 的完成一无所知。 注意：此功能并非旨在提供最佳性能。 这只是在初始化时在另一个 lcore 上启动函数的实用方法。*/
		rte_eal_remote_launch(lcore_hello, NULL, lcore_id);
		/* >8 End of simpler equivalent. */
	}

	/* call it on main lcore too */
	lcore_hello(NULL);
	/* >8 End of launching the function on each lcore. */

	rte_eal_mp_wait_lcore();

	/* clean up the EAL */
	rte_eal_cleanup();

	return 0;
}
