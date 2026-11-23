#
# ua3f-bin —— 把预编译的 ua_3.7.0-r1_mipsel_24kc.ipk 的 data 树做成一个文件包
#
# 思路：不在 CI 里引入 Go 工具链重新编译 UA3F，而是直接复用官方预编译产物。
# CI 会在 make 之前执行：
#   ar x local-ipk/ua_3.7.0-r1_mipsel_24kc.ipk && tar -xzf data.tar.gz -C package/ua3f-bin/files
# 于是本目录下的 files/ 就是完整的根文件树（usr/bin/ua3f、etc/config/ua3f、
# etc/init.d/ua3f、usr/lib/lua/luci/... 、www/luci-static/...）。
# 依赖关系在这里用 DEPENDS 声明，交给 opkg/构建系统自动拉齐（原来 ipk 的 control
# 里那份 Depends 逐条照抄，一个不落）。
#
include $(TOPDIR)/rules.mk

PKG_NAME:=ua3f-bin
PKG_VERSION:=3.7.0
PKG_RELEASE:=1

PKG_LICENSE:=MIT
PKG_MAINTAINER:=SunBK201

include $(INCLUDE_DIR)/package.mk

define Package/ua3f-bin
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Web Servers/Proxies
  TITLE:=UA3F - HTTP User-Agent rewriting proxy (prebuilt mipsel_24kc)
  URL:=https://github.com/SunBK201/UA3F
  DEPENDS:=+luci-compat +ipset +iptables +iptables-mod-tproxy +iptables-mod-extra \
	+iptables-mod-ipopt +iptables-mod-nfqueue +iptables-mod-conntrack-extra \
	+kmod-nf-conntrack-netlink
endef

define Package/ua3f-bin/description
  UA3F rewrites the User-Agent (and other headers) of HTTP requests going out
  through the router, so that tethered clients look like a phone/PC instead of
  a router. This package ships the official prebuilt ua_3.7.0-r1_mipsel_24kc
  payload unchanged; only the packaging metadata is ours.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Compile
endef

define Package/ua3f-bin/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/controller
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/model/cbi/ua3f
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/view/ua3f
	$(INSTALL_DIR) $(1)/www/luci-static/resources/ua3f
	$(CP) ./files/. $(1)/
endef

# ua3f 的 /etc/config/ua3f 是 conffile，sysupgrade 时要保留用户配置
define Package/ua3f-bin/conffiles
/etc/config/ua3f
endef

$(eval $(call BuildPackage,ua3f-bin))
