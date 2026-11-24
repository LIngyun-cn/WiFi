include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=tcpts-strip
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define KernelPackage/tcpts-strip
	SUBMENU:=Netfilter Extensions
	TITLE:=Strip TCP timestamps from outgoing SYN packets
	FILES:=$(PKG_BUILD_DIR)/tcpts-strip.ko
	AUTOLOAD:=$(call AutoLoad, 98, tcpts-strip)
	KCONFIG:=
endef

define KernelPackage/tcpts-strip/description
 在 FORWARD/OUTPUT 的 POST_ROUTING 钩子上把 TCP 时间戳选项（kind=8）
 就地改写成等长 NOP，于是服务器不会协商 ts，整条连接两个方向都不带
 时间戳 —— 网关侧就失去了"按时间戳速率/主机开机时长区分多台设备"的依据。

 只处理"带 SYN 且不带 ACK"的建连包（含重传 SYN）。为什么不处理后续包：
 一旦 SYN 里没有 ts，对端 SYN-ACK 也不会带 ts，之后的包本来就没有；
 反过来若在已协商 ts 的连接上单方向剥离，会打乱对端的 PAWS/RTT 计算。
endef

EXTRA_KCONFIG:= \
	CONFIG_TCPTS_STRIP=m

EXTRA_CFLAGS:= \
	$(patsubst CONFIG_%, -DCONFIG_%=1, $(patsubst %=m,%,$(filter %=m,$(EXTRA_KCONFIG)))) \
	$(patsubst CONFIG_%, -DCONFIG_%=1, $(patsubst %=y,%,$(filter %=y,$(EXTRA_KCONFIG)))) \
	-DVERSION=$(PKG_RELEASE)

MAKE_OPTS:=$(KERNEL_MAKE_FLAGS) \
	M="$(PKG_BUILD_DIR)" \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	$(EXTRA_KCONFIG)

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" $(MAKE_OPTS) modules
endef

$(eval $(call KernelPackage,tcpts-strip))
