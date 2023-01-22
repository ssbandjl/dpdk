..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation.

.. _Ring_Library:

Ring Library
============

The ring allows the management of queues.
Instead of having a linked list of infinite size, the rte_ring has the following properties:

*   FIFO

*   Maximum size is fixed, the objects are stored in a table

*   Objects can be pointers or elements of multiple of 4 byte size

*   Lockless implementation

*   Multi-consumer or single-consumer dequeue

*   Multi-producer or single-producer enqueue

*   Bulk dequeue - Dequeues the specified count of objects if successful; otherwise fails

*   Bulk enqueue - Enqueues the specified count of objects if successful; otherwise fails

*   Burst dequeue - Dequeue the maximum available objects if the specified count cannot be fulfilled

*   Burst enqueue - Enqueue the maximum available objects if the specified count cannot be fulfilled

The advantages of this data structure over a linked list queue are as follows:

*   Faster; only requires a single 32 bit Compare-And-Swap instruction instead of several pointer size Compare-And-Swap instructions.

*   Simpler than a full lockless queue.

*   Adapted to bulk enqueue/dequeue operations.
    As objects are stored in a table, a dequeue of several objects will not produce as many cache misses as in a linked queue.
    Also, a bulk dequeue of many objects does not cost more than a dequeue of a simple object.

The disadvantages:

*   Size is fixed

*   Having many rings costs more in terms of memory than a linked list queue. An empty ring contains at least N objects. 与链表队列相比，拥有多个环在内存方面的成本更高。 一个空环至少包含 N 个对象

A simplified representation of a Ring is shown in with consumer and producer head and tail pointers to objects stored in the data structure.
环的简化表示如图所示，消费者和生产者的头部和尾部指针指向存储在数据结构中的对象。

.. _figure_ring1:

.. figure:: img/ring1.*

   Ring Structure


References for Ring Implementation in FreeBSD*
----------------------------------------------

The following code was added in FreeBSD 8.0, and is used in some network device drivers (at least in Intel drivers):

    * `bufring.h in FreeBSD <http://svn.freebsd.org/viewvc/base/release/8.0.0/sys/sys/buf_ring.h?revision=199625&amp;view=markup>`_

    * `bufring.c in FreeBSD <http://svn.freebsd.org/viewvc/base/release/8.0.0/sys/kern/subr_bufring.c?revision=199625&amp;view=markup>`_

Lockless Ring Buffer in Linux*
------------------------------

The following is a link describing the `Linux Lockless Ring Buffer Design <http://lwn.net/Articles/340400/>`_.

Additional Features
-------------------

Name
~~~~

A ring is identified by a unique name.
It is not possible to create two rings with the same name (rte_ring_create() returns NULL if this is attempted).

Use Cases
---------

Use cases for the Ring library include:

    *  Communication between applications in the DPDK

    *  Used by memory pool allocator

Anatomy of a Ring Buffer 剖析环形缓冲区
------------------------

This section explains how a ring buffer operates.
The ring structure is composed of two head and tail couples; one is used by producers and one is used by the consumers.
The figures of the following sections refer to them as prod_head, prod_tail, cons_head and cons_tail.

Each figure represents a simplified state of the ring, which is a circular buffer.
The content of the function local variables is represented on the top of the figure,
and the content of ring structure is represented on the bottom of the figure.
本节介绍环形缓冲区的工作原理。 环形结构由两个头尾对组成； 一种供生产者使用，一种供消费者使用。 以下部分的数字将它们称为 prod_head、prod_tail、cons_head 和 cons_tail。

每个图代表环的一个简化状态，它是一个循环缓冲区。 函数局部变量的内容表示在图的顶部，环形结构的内容表示在图的底部。

Single Producer Enqueue
~~~~~~~~~~~~~~~~~~~~~~~

This section explains what occurs when a producer adds an object to the ring.
In this example, only the producer head and tail (prod_head and prod_tail) are modified,
and there is only one producer. 本节解释当生产者将对象添加到环时会发生什么。 本例中只修改了生产者头和尾（prod_head和prod_tail），生产者只有一个。

The initial state is to have a prod_head and prod_tail pointing at the same location. 初始状态是让 prod_head 和 prod_tail 指向同一位置。

Enqueue First Step 入队第一步
^^^^^^^^^^^^^^^^^^

First, *ring->prod_head* and ring->cons_tail are copied in local variables.
The prod_next local variable points to the next element of the table, or several elements after in case of bulk enqueue. 首先，将 ring->prod_head 和 ring->cons_tail 复制到局部变量中。 prod_next 局部变量指向表的下一个元素，或者在批量入队的情况下指向后面的几个元素

If there is not enough room in the ring (this is detected by checking cons_tail), it returns an error.


.. _figure_ring-enqueue1:

.. figure:: img/ring-enqueue1.*

   Enqueue first step


Enqueue Second Step
^^^^^^^^^^^^^^^^^^^

The second step is to modify *ring->prod_head* in ring structure to point to the same location as prod_next.

The added object is copied in the ring (obj4).


.. _figure_ring-enqueue2:

.. figure:: img/ring-enqueue2.*

   Enqueue second step


Enqueue Last Step
^^^^^^^^^^^^^^^^^

Once the object is added in the ring, ring->prod_tail in the ring structure is modified to point to the same location as *ring->prod_head*.
The enqueue operation is finished.


.. _figure_ring-enqueue3:

.. figure:: img/ring-enqueue3.*

   Enqueue last step


Single Consumer Dequeue
~~~~~~~~~~~~~~~~~~~~~~~

This section explains what occurs when a consumer dequeues an object from the ring.
In this example, only the consumer head and tail (cons_head and cons_tail) are modified and there is only one consumer.

The initial state is to have a cons_head and cons_tail pointing at the same location.

Dequeue First Step
^^^^^^^^^^^^^^^^^^

First, ring->cons_head and ring->prod_tail are copied in local variables.
The cons_next local variable points to the next element of the table, or several elements after in the case of bulk dequeue. 首先，将 ring->cons_head 和 ring->prod_tail 复制到局部变量中。 cons_next 局部变量指向表的下一个元素，或者在批量出队的情况下指向后面的几个元素。

If there are not enough objects in the ring (this is detected by checking prod_tail), it returns an error.


.. _figure_ring-dequeue1:

.. figure:: img/ring-dequeue1.*

   Dequeue first step


Dequeue Second Step
^^^^^^^^^^^^^^^^^^^

The second step is to modify ring->cons_head in the ring structure to point to the same location as cons_next.

The dequeued object (obj1) is copied in the pointer given by the user.


.. _figure_ring-dequeue2:

.. figure:: img/ring-dequeue2.*

   Dequeue second step


Dequeue Last Step
^^^^^^^^^^^^^^^^^

Finally, ring->cons_tail in the ring structure is modified to point to the same location as ring->cons_head.
The dequeue operation is finished.


.. _figure_ring-dequeue3:

.. figure:: img/ring-dequeue3.*

   Dequeue last step


Multiple Producers Enqueue
~~~~~~~~~~~~~~~~~~~~~~~~~~

This section explains what occurs when two producers concurrently add an object to the ring.
In this example, only the producer head and tail (prod_head and prod_tail) are modified.

The initial state is to have a prod_head and prod_tail pointing at the same location.

Multiple Producers Enqueue First Step
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

On both cores, *ring->prod_head* and ring->cons_tail are copied in local variables.
The prod_next local variable points to the next element of the table,
or several elements after in the case of bulk enqueue.

If there is not enough room in the ring (this is detected by checking cons_tail), it returns an error.


.. _figure_ring-mp-enqueue1:

.. figure:: img/ring-mp-enqueue1.*

   Multiple producer enqueue first step


Multiple Producers Enqueue Second Step
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The second step is to modify ring->prod_head in the ring structure to point to the same location as prod_next.
This operation is done using a Compare And Swap (CAS) instruction, which does the following operations atomically: 
第二步，修改ring结构中的ring->prod_head指向和prod_next相同的位置。 此操作是使用比较和交换 (CAS) 指令完成的，该指令以原子方式执行以下操作

*   If ring->prod_head is different to local variable prod_head,
    the CAS operation fails, and the code restarts at first step.

*   Otherwise, ring->prod_head is set to local prod_next,
    the CAS operation is successful, and processing continues.

In the figure, the operation succeeded on core 1, and step one restarted on core 2.


.. _figure_ring-mp-enqueue2:

.. figure:: img/ring-mp-enqueue2.*

   Multiple producer enqueue second step


Multiple Producers Enqueue Third Step
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The CAS operation is retried on core 2 with success.

The core 1 updates one element of the ring(obj4), and the core 2 updates another one (obj5).


.. _figure_ring-mp-enqueue3:

.. figure:: img/ring-mp-enqueue3.*

   Multiple producer enqueue third step


Multiple Producers Enqueue Fourth Step
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Each core now wants to update ring->prod_tail.
A core can only update it if ring->prod_tail is equal to the prod_head local variable.
This is only true on core 1. The operation is finished on core 1.


.. _figure_ring-mp-enqueue4:

.. figure:: img/ring-mp-enqueue4.*

   Multiple producer enqueue fourth step


Multiple Producers Enqueue Last Step
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Once ring->prod_tail is updated by core 1, core 2 is allowed to update it too.
The operation is also finished on core 2.


.. _figure_ring-mp-enqueue5:

.. figure:: img/ring-mp-enqueue5.*

   Multiple producer enqueue last step


Modulo 32-bit Indexes 取模
~~~~~~~~~~~~~~~~~~~~~

In the preceding figures, the prod_head, prod_tail, cons_head and cons_tail indexes are represented by arrows.
In the actual implementation, these values are not between 0 and size(ring)-1 as would be assumed.
The indexes are between 0 and 2^32 -1, and we mask their value when we access the object table (the ring itself).
32-bit modulo also implies that operations on indexes (such as, add/subtract) will automatically do 2^32 modulo
if the result overflows the 32-bit number range.
在上图中，prod_head、prod_tail、cons_head 和 cons_tail 索引用箭头表示。 在实际实现中，这些值并不像假设的那样介于 0 和 size(ring)-1 之间。 索引介于 0 和 2^32 -1 之间，我们在访问对象表（环本身）时屏蔽它们的值。 32 位模还意味着如果结果溢出 32 位数字范围，对索引的操作（例如，加/减）将自动执行 2^32 模。

The following are two examples that help to explain how indexes are used in a ring.

.. note::

    To simplify the explanation, operations with modulo 16-bit are used instead of modulo 32-bit.
    In addition, the four indexes are defined as unsigned 16-bit integers,
    as opposed to unsigned 32-bit integers in the more realistic case.
    为了简化说明，使用模 16 位运算而不是模 32 位运算。 此外，四个索引被定义为无符号 16 位整数，而不是更实际情况下的无符号 32 位整数。


.. _figure_ring-modulo1:

.. figure:: img/ring-modulo1.*

   Modulo 32-bit indexes - Example 1


This ring contains 11000 entries.


.. _figure_ring-modulo2:

.. figure:: img/ring-modulo2.*

      Modulo 32-bit indexes - Example 2


This ring contains 12536 entries.

.. note::

    For ease of understanding, we use modulo 65536 operations in the above examples.
    In real execution cases, this is redundant for low efficiency, but is done automatically when the result overflows.
    为了便于理解，我们在上面的例子中使用了模65536运算。 在实际执行情况下，这是多余的，效率低下，但在结果溢出时自动完成。

The code always maintains a distance between producer and consumer between 0 and size(ring)-1.
Thanks to this property, we can do subtractions between 2 index values in a modulo-32bit base:
that's why the overflow of the indexes is not a problem.

At any time, entries and free_entries are between 0 and size(ring)-1,
even if only the first term of subtraction has overflowed:


.. code-block:: c

    uint32_t entries = (prod_tail - cons_head);
    uint32_t free_entries = (mask + cons_tail -prod_head);

Producer/consumer synchronization modes
---------------------------------------

rte_ring supports different synchronization modes for producers and consumers.
These modes can be specified at ring creation/init time via ``flags``
parameter.
That should help users to configure ring in the most suitable way for his
specific usage scenarios.
Currently supported modes:
rte_ring 支持生产者和消费者的不同同步模式。 这些模式可以在环创建/初始化时通过标志参数指定。 那应该可以帮助用户以最适合其特定使用场景的方式配置环。 目前支持的模式：

.. _Ring_Library_MPMC_Mode:

MP/MC (default one)
~~~~~~~~~~~~~~~~~~~

Multi-producer (/multi-consumer) mode. This is a default enqueue (/dequeue)
mode for the ring. In this mode multiple threads can enqueue (/dequeue)
objects to (/from) the ring. For 'classic' DPDK deployments (with one thread
per core) this is usually the most suitable and fastest synchronization mode.
As a well known limitation - it can perform quite pure on some overcommitted
scenarios.
多生产者（/多消费者）模式。 这是环的默认入队（/出队）模式。 在这种模式下，多个线程可以将对象入队（/出队）到（/从）环中。 对于“经典”DPDK 部署（每个内核一个线程），这通常是最合适和最快的同步模式。 作为一个众所周知的限制 - 它可以在一些过度使用的场景中表现得非常纯粹。

.. _Ring_Library_SPSC_Mode:

SP/SC
~~~~~
Single-producer (/single-consumer) mode. In this mode only one thread at a time
is allowed to enqueue (/dequeue) objects to (/from) the ring.

.. _Ring_Library_MT_RTS_Mode:

MP_RTS/MC_RTS
~~~~~~~~~~~~~

Multi-producer (/multi-consumer) with Relaxed Tail Sync (RTS) mode.
The main difference from the original MP/MC algorithm is that
tail value is increased not by every thread that finished enqueue/dequeue,
but only by the last one.
That allows threads to avoid spinning on ring tail value,
leaving actual tail value change to the last thread at a given instance.
That technique helps to avoid the Lock-Waiter-Preemption (LWP) problem on tail
update and improves average enqueue/dequeue times on overcommitted systems.
To achieve that RTS requires 2 64-bit CAS for each enqueue(/dequeue) operation:
one for head update, second for tail update.
In comparison the original MP/MC algorithm requires one 32-bit CAS
for head update and waiting/spinning on tail value.
具有 Relaxed Tail Sync (RTS) 模式的多生产者（/多消费者）。 与原始 MP/MC 算法的主要区别在于，tail 值不是由每个完成入队/出队的线程增加的，而是由最后一个线程增加的。 这允许线程避免在环尾值上自旋，将实际尾值更改留给给定实例的最后一个线程。 该技术有助于避免尾部更新时的 Lock-Waiter-Preemption (LWP) 问题，并改善过度使用系统的平均入队/出队时间。 为实现这一点，RTS 需要 2 个 64 位 CAS 用于每个入队（/出队）操作：一个用于头部更新，第二个用于尾部更新。 相比之下，原始 MP/MC 算法需要一个 32 位 CAS 来更新头部和等待/自旋尾部值。

.. _Ring_Library_MT_HTS_Mode:

MP_HTS/MC_HTS
~~~~~~~~~~~~~

Multi-producer (/multi-consumer) with Head/Tail Sync (HTS) mode.
In that mode enqueue/dequeue operation is fully serialized:
at any given moment only one enqueue/dequeue operation can proceed.
This is achieved by allowing a thread to proceed with changing ``head.value``
only when ``head.value == tail.value``.
Both head and tail values are updated atomically (as one 64-bit value).
To achieve that 64-bit CAS is used by head update routine.
That technique also avoids the Lock-Waiter-Preemption (LWP) problem on tail
update and helps to improve ring enqueue/dequeue behavior in overcommitted
scenarios. Another advantage of fully serialized producer/consumer -
it provides the ability to implement MT safe peek API for rte_ring.
具有头/尾同步 (HTS) 模式的多生产者（/多消费者）。 在那种模式下，入队/出队操作是完全序列化的：在任何给定时刻，只有一个入队/出队操作可以进行。 这是通过仅当 head.value == tail.value 时允许线程继续更改 head.value 来实现的。 head 和 tail 值都自动更新（作为一个 64 位值）。 为了实现 64 位 CAS，head 更新例程使用了它。 该技术还避免了尾部更新时的 Lock-Waiter-Preemption (LWP) 问题，并有助于改善过度使用场景中的环入队/出队行为。 完全序列化的生产者/消费者的另一个优势 - 它提供了为 rte_ring 实现 MT 安全查看 API 的能力。

Ring Peek API
-------------

For ring with serialized producer/consumer (HTS sync mode) it is possible
to split public enqueue/dequeue API into two phases:  对于序列化生产者/消费者（HTS 同步模式）的环，可以将公共入队/出队 API 分为两个阶段：

*   enqueue/dequeue start

*   enqueue/dequeue finish

That allows user to inspect objects in the ring without removing them
from it (aka MT safe peek) and reserve space for the objects in the ring
before actual enqueue.
Note that this API is available only for two sync modes:
这允许用户检查环中的对象而无需将它们从环中移除（也称为 MT 安全窥视），并在实际排队之前为环中的对象预留空间。 请注意，此 API 仅适用于两种同步模式：

*   Single Producer/Single Consumer (SP/SC)

*   Multi-producer/Multi-consumer with Head/Tail Sync (HTS)

It is a user responsibility to create/init ring with appropriate sync modes
selected. As an example of usage:

.. code-block:: c

    /* read 1 elem from the ring: */
    uint32_t n = rte_ring_dequeue_bulk_start(ring, &obj, 1, NULL);
    if (n != 0) {
        /* examine object */
        if (object_examine(obj) == KEEP)
            /* decided to keep it in the ring. */
            rte_ring_dequeue_finish(ring, 0);
        else
            /* decided to remove it from the ring. */
            rte_ring_dequeue_finish(ring, n);
    }

Note that between ``_start_`` and ``_finish_`` none other thread can proceed
with enqueue(/dequeue) operation till ``_finish_`` completes.

Ring Peek Zero Copy API
-----------------------

Along with the advantages of the peek APIs, zero copy APIs provide the ability
to copy the data to the ring memory directly without the need for temporary
storage (for ex: array of mbufs on the stack).
除了 peek API 的优势外，零复制 API 还提供了将数据直接复制到环形内存的能力，而无需临时存储（例如：堆栈上的 mbuf 数组）。

These APIs make it possible to split public enqueue/dequeue API into 3 phases:

* enqueue/dequeue start

* copy data to/from the ring

* enqueue/dequeue finish

Note that this API is available only for two sync modes:

*   Single Producer/Single Consumer (SP/SC)

*   Multi-producer/Multi-consumer with Head/Tail Sync (HTS)

It is a user responsibility to create/init ring with appropriate sync modes.
Following is an example of usage:

.. code-block:: c

    /* Reserve space on the ring */
    n = rte_ring_enqueue_zc_burst_start(r, 32, &zcd, NULL);
    /* Pkt I/O core polls packets from the NIC */
    if (n != 0) {
        nb_rx = rte_eth_rx_burst(portid, queueid, zcd->ptr1, zcd->n1);
        if (nb_rx == zcd->n1 && n != zcd->n1)
            nb_rx += rte_eth_rx_burst(portid, queueid, zcd->ptr2,
							n - zcd->n1);
        /* Provide packets to the packet processing cores */
        rte_ring_enqueue_zc_finish(r, nb_rx);
    }

Note that between ``_start_`` and ``_finish_`` no other thread can proceed
with enqueue(/dequeue) operation till ``_finish_`` completes.

References
----------

    *   `bufring.h in FreeBSD <http://svn.freebsd.org/viewvc/base/release/8.0.0/sys/sys/buf_ring.h?revision=199625&amp;view=markup>`_ (version 8)

    *   `bufring.c in FreeBSD <http://svn.freebsd.org/viewvc/base/release/8.0.0/sys/kern/subr_bufring.c?revision=199625&amp;view=markup>`_ (version 8)

    *   `Linux Lockless Ring Buffer Design <http://lwn.net/Articles/340400/>`_
