#
# UA2F —— C 实现的 User-Agent 改写器。
#
# 为什么不用 ua3f：ua3f 是 Go 写的 17.6MB 静态二进制，在这台 MT7628（单核 575MHz、
# 57MB 内存、musl）上启动就卡死在 runtime 层 —— A/B 实测（SOCKS5/TPROXY/带不带
# header-rewrite 三组）CPU 都是 100%、进程状态 D、连规则都没建出来，且 logread
# 一片空白。同样的功能用 C 实现的 UA2F 只占一百多 KB，NFQUEUE 改写 HTTP 头。
#
include $(TOPDIR)/rules.mk

PKG_NAME:=ua2f
PKG_VERSION:=5.2.0
PKG_RELEASE:=2

PKG_LICENSE:=GPL-3.0-only
PKG_LICENSE_FILES:=LICENSE
PKG_MAINTAINER:=Zxilly <zhouxinyu1001@gmail.com>

# 源码包 vendor 在本目录（src.tar.gz），不走 OpenWrt 的 Download 机制：
# 上游没有 release 附件，GitHub 归档走 github_archive 方法时 MIRROR_HASH
# 只有先下过一次才知道，且构建期间还得能连 GitHub。直接进仓库最可复现。
# src.tar.gz = UA2F v5.2.0 官方归档 + 我们一处补丁（src/handler.c 的 skip_short_ua）
# 官方原件 sha256 = 025fa6b7f8e1e0d03d005899790249fd5e10f45b05ec0eb65fd7622be2609ca7
# 打过补丁后 sha256 = 3bfeae4582c34fdf85cb4097b569fb430200285b705735122bd087fe4650ca5a
# 补丁内容：ua2f 只能原地等长写入，原 UA 比 custom_ua 短时会被截断成
# "Mozilla/5.0 (Windows" 这种没有结尾的畸形头（比不改更容易被 DPI 挑出来），
# 所以 ua_len < strlen(custom_ua) 时整条跳过、保留原样。原件另存为 src.orig.tar.gz。
# 解包后是 UA2F-5.2.0/ 一层，所以 Build/Prepare 里 --strip-components=1

PKG_BUILD_PARALLEL:=1
CMAKE_BINARY_SUBDIR:=build

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/ua2f
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Routing and Redirection
  TITLE:=Rewrite HTTP User-Agent on the fly (C implementation)
  URL:=https://github.com/Zxilly/UA2F
  DEPENDS:= \
	+libnetfilter-conntrack \
	+libnetfilter-queue \
	+libmnl \
	+libnfnetlink \
	+(mips||mipsel):libatomic \
	+libpthread \
	+libuci \
	+ip-full \
	+kmod-nft-queue \
	+kmod-nft-tproxy
endef

define Package/ua2f/description
  UA2F rewrites the HTTP User-Agent header of traffic passing through the
  router, so that every client behind it looks like the same machine.
  Implemented in C with libnetfilter_queue; NFQUEUE / REDIRECT / TPROXY modes.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	tar -xzf $(CURDIR)/src.tar.gz -C $(PKG_BUILD_DIR) --strip-components=1
endef

# UA 在运行时由 /etc/config/ua2f 的 main.custom_ua 决定（src/config.c 读 uci），
# 所以编译期不打开 UA2F_CUSTOM_USER_AGENT —— 那个选项要走带空格的字符串参数，
# 在 make/shell 里转义容易踩坑。backtrace 关掉，省掉 libbacktrace 的 autoconf 那一套。
CMAKE_OPTIONS += \
	-DUA2F_VERSION_STR=$(PKG_VERSION) \
	-DUA2F_ENABLE_BACKTRACE=OFF \
	-DUA2F_CUSTOM_USER_AGENT=OFF

# 只装二进制。/etc/config/ua2f 与 /etc/init.d/ua2f 由仓库根目录的 files/ 覆盖层提供，
# 这样改配置和 init 不用碰包本身。
define Package/ua2f/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/ua2f $(1)/usr/bin/
endef

$(eval $(call BuildPackage,ua2f))
