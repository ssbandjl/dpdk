git clone https://github.com/Mellanox/dpdk.org.git
cd dpdk.org && git checkout origin/mlnx_dpdk_20.11
meson -Dexamples=vdpa -Dbuildtype=debug /tmp/build
ninja -C /tmp/build


#Use DPDK testpmd forward between SF representor port and PF
# 如果仅探测两个端口，DPDK testpmd 默认以 IO 转发模式启动。Representor 语法为“<PCI>,representor=[pfX]sfY”，如果不是链路聚合(LAG)模式, 则pf参数是可选项, 换言之, 链路聚合才需要PF参数
/tmp/build/app/dpdk-testpmd --file-prefix=xx -w 0000:03:00.0,representor=sf88  -- --stats-period=1


#Start DPDK vdpa example with SF devices
/tmp/build/examples/dpdk-vdpa -w auxiliary:mlx5_core.sf.4,class=vdpa -- --iface /tmp/vhost-user-