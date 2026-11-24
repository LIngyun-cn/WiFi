# 云端编译 R3A 固件（含 rkp-ipid 内核模块 + UA2F）

目标设备：**小米路由器 3A** / `ramips/mt76x8` / OpenWrt **24.10.8** / Linux **6.6.144**

## 为什么不是 ImageBuilder 了

旧版流程用 **ImageBuilder + 预编译 ipk**，8 分钟出镜像，很省事。但它做不了这一次的需求：

- **ImageBuilder / SDK 都不能编内核模块**。OpenWrt 官方明确不随 SDK 发布内核源码
  （`sdk: don't try to build in-kernel kmods — We don't ship the kernel sources`），
  SDK 包里根本没有 `target/linux`、没有 `$(LINUX_DIR)`、没有 `kernel.mk`。
- `rkp-ipid` 是 `kmod-rkp-ipid`，必须和内核在**同一次构建**里生成，否则 vermagic
  对不上（本机要求 `6.6.144-1-4ca73159125334b180ac4bba6dc45839`），`insmod` 会直接
  `invalid module format`。

所以改成**完整 buildroot 源码构建**。代价是首次 50–70 分钟（有 `dl/` 缓存后快一些），
好处是内核、模块、镜像一个源，vermagic 天生对齐。

## 仓库结构

```
.github/workflows/build-firmware.yml   全源码构建流程
packages/rkp-ipid/                     kmod-rkp-ipid 源码（上游 CHN-beta/rkp-ipid @ 073e389）
  Makefile  src/{Makefile,Kconfig,rkp-ipid.c}
packages/tcpts-strip/                  kmod-tcpts-strip（自写：抹掉出网 SYN 的 TCP 时间戳选项）
  Makefile  src/{Makefile,Kconfig,tcpts-strip.c}
package-src/ua2f/{Makefile,src.tar.gz}    UA2F（C 实现）打包元数据 + vendored 源码包
local-ipk/ua_3.7.0-r1_mipsel_24kc.ipk   UA3F 官方预编译包（已停用，仅留档，不参与构建）
files/                                 根文件系统覆盖层
  etc/init.d/rkp-ipid-mark             iptables mangle 打 mark + 关 flow offloading
  etc/init.d/ua2f                      上游 v5.2.0 的 init（nft / iptables 双路径）
  etc/init.d/ganyu-guard               防检测：DNS 劫持 / NTP 归位 / TTL 统一 / DoT 封锁 / IPv6 兜底
  etc/init.d/ybu-login                 校园网锐捷门户自动登录
  etc/config/{ua2f,ganyu,ybu}          上面三者的配置
  etc/uci-defaults/{90-rkp-ipid,93-ua2f,94-bbr,95-ybu,96-macclone,97-ganyu,98-wan-sta}
  usr/bin/ybu-login                    门户登录脚本本体
  usr/bin/ganyu-status                 反检测链路状态采集（纯文本，供 LuCI 页面调用）
  usr/share/luci/menu.d/luci-app-ganyu.json     状态页菜单项（状态 → 反检测状态）
  usr/share/rpcd/acl.d/luci-app-ganyu.json      ACL：放行 /usr/bin/ganyu-status 的 exec
  www/luci-static/resources/view/ganyu/status.js  LuCI 视图（5 秒轮询刷新）
  etc/uci-defaults/99-lan-ap           语言 / 无线 / 区域（SSID 只在还是 OpenWrt 默认名时才写）
```

## 用法

1. 仓库页面 → **Actions** → 左侧 `build-firmware` → **Run workflow**。
   两个输入：

   | 输入 | 说明 | 默认 |
   |---|---|---|
   | `extra_packages` | 额外装进固件的包，空格分隔 | 中文三件套 + tcpdump/iperf3/mtr |
   | `remove_packages` | 要剔除的包，空格分隔（写 `# CONFIG_PACKAGE_x is not set`） | 空 |

2. 等 **35 分钟**左右（编译缓存命中时；冷启动首次约 60–70 分钟）。构建会自己校验：
   - `rkp-ipid.ko` 确实生成，并打印它的 `vermagic=`
   - rootfs 里有 `/usr/bin/ua2f`、`/etc/init.d/ua2f`、`/etc/config/ua2f`、`/etc/init.d/rkp-ipid-mark`，
     且 `/usr/bin/ua3f` **不在**（还有人往里塞 Go 版就红）
   - 镜像 ≤ **15,204,352 字节**（mtd7 firmware 分区真实大小，不是 profile 里那个 16064k）
   - `files/` 覆盖层 15 个文件全部落进 rootfs，可执行位正确，状态页菜单/ACL/视图三件套齐全
   - `BUILTIN_PKGS` 里 31 个包逐个出现在 `.config` 与 manifest 里

3. Artifacts 下载 `openwrt-24.10.8-miwifi-3a-<n>`，里面有
   `*.squashfs-sysupgrade.bin` / `*.manifest` / `sha256sums` / `.config` / `build.log`。

4. LuCI → 系统 → 备份/刷写固件 → 上传 → 首次换固件建议**不勾**"保留配置"。

## 为什么是 UA2F 而不是 UA3F

UA3F（`SunBK201/UA3F`）是一个 Go 写的 17.6MB 静态二进制。在这台机器上装得住，但**跑不起来**：

- 起机自动拉起后 `State: D`、`VmRSS 7.8MB`、`CPU 25%usr/75%sys`，`logread` 一片空白，
  `nft list table ip mangle` 里连它自己的链都没有 —— 卡在 Go runtime 初始化，连建规则都没走到。
- A/B 三组实测（`SOCKS5` / `TPROXY` 不带 header-rewrite / `TPROXY` 带 header-rewrite）**全部 100%**，
  全杀掉立刻回到 3%。`SOCKS5` 完全不碰 iptables 也 100%，所以与 ipset 重名、与 TPROXY 都无关，
  是 Go runtime 在这颗单核 575MHz + musl + 6.6 上的问题。
- 顺带修过它的 ipset 自杀循环（退出不销毁 `UA3F_LAN`，procd 反复重启到 SIGKILL），但那只是表象。

换成 `Zxilly/UA2F`（C 实现，v5.2.0）：

- 源码 vendored 在 `package-src/ua2f/src.tar.gz`（sha256 `025fa6b7…609ca7`）。不走 OpenWrt 的
  `Download/` 机制，是因为上游没有 release 附件，GitHub 归档走 `github_archive` 方法时
  `MIRROR_HASH` 只有先下过一次才知道。vendored 进仓库 = 构建期不联网、完全可复现。
- 编译期关掉 `UA2F_ENABLE_BACKTRACE`（省掉 libbacktrace 那套 autoconf）；UA 由运行时
  `/etc/config/ua2f` 的 `main.custom_ua` 决定（`src/config.c` 读 uci），不写死在二进制里。
- 设备有 `nft`，init 走 nft 路径：`table inet ua2f`，postrouting `priority mangle -5` 里
  `tcp dport 80 counter ct mark set 44` + `ct mark 43 return` + `queue num 10010 bypass`。
  所以必须带 `kmod-nft-queue`，否则 NFQUEUE 规则根本建不出来。
- `handle_intranet=0`：内网/校园网地址（10/8、172.16/12、192.168/16…）直接 RETURN，只改真正
  出公网的 HTTP；`handle_tls=0`：443 是密文，改写只会把连接搞坏。

旧的 `package-src/ua3f-bin/` 已删除、`local-ipk/*.ipk` 留在仓库里，要回退只需把 `.config` 里
`CONFIG_PACKAGE_ua2f=y` 换回 `CONFIG_PACKAGE_ua3f-bin=y` 并恢复 91-ua3f（git 历史里有），
但在这颗 CPU 上仍然跑不动。

## rkp-ipid 的使用注意

- 模块默认 `mark_capture=0x10`，只有 `skb->mark & 0x10` 的包才会被改 IPID。
  `files/etc/init.d/rkp-ipid-mark` 建的 `IPID_MOD` 链负责打这个 mark，挂在
  `mangle OUTPUT` 和 `mangle FORWARD` 上。
- **故意没有** `-d 10.0.0.0/8 -j RETURN`。上游 README 有这一条，但本机校园网就是
  `10.126.93.98/20`（A 类私网），RETURN 掉等于把真正需要改的包全跳过。
- **flow offloading 必须关**（脚本会自动 `uci set firewall.@defaults[0].flow_offloading=0`），
  否则软件/硬件 NAT 加速让包绕过 `NF_INET_POST_ROUTING`，模块等于没装。
- 每翻倍打印一次统计：`Successfully modified %u packages ...`，`logread | grep rkp-ipid` 看。

## TCP 时间戳：为什么必须自己写内核模块

TTL 和 IPID 能改，是因为它们都在 **IP 头**里，路由器本来就在重写。TCP 时间戳在
**TCP 选项区**（`kind=8, len=10`），由客户端协议栈生成，路由器只转发。

能想到的现成方案全是死的：

| 候选 | 结论 |
|---|---|
| `iptables -j TCPOPTSTRIP --strip-options timestamp` | OpenWrt **不构建** xt_TCPOPTSTRIP：`v24.10.8` 的 `include/netfilter.mk` 全文没有它，库里也没有这个包 |
| `kmod-ipt-extra` | 只含 xt_TCPMSS / xt_HL / xt_TPROXY / xt_string 等 30 个 |
| nftables | 语法层面无法改 TCP 选项 |
| `sysctl net.ipv4.tcp_timestamps=0` | 只关路由器自己的包，**转发的流量一个字节都不碰** |
| 统一 NTP 授时 | **没有用**。TSval 是单调时钟（Linux 走 jiffies、Windows 每 10ms 一跳），跟墙钟无关，NTP 对齐不了它 |

所以自写 `packages/tcpts-strip`：`POST_ROUTING`（`NF_IP_PRI_NAT_SRC`，与 rkp-ipid
同位置）上解析选项，对 `SYN && !ACK` 的包把 kind=8 那 10 字节就地写成 10 个
`NOP(0x01)`——**等长替换**，`doff` 不变、payload 不用挪，只需重算 TCP 校验和。

只处理 SYN 的原因：SYN 里没有 ts，对端 SYN-ACK 也不会回 ts，这条连接两个方向
从此都不带时间戳；反过来在已协商 ts 的连接上单方向剥离，会让 TSecr 对不上、
打乱对端 PAWS 与 RTT 采样。

代价：失去 PAWS 与时间戳 RTT 采样（宿舍场景无感）。参数
`/sys/module/tcpts_strip/parameters/strip_syn`（默认 1）。

验证方法：抓 WAN 侧出网 SYN，选项里的 `TS val ...` 应该消失——
`tcpdump -i phy1-sta0 -c 5 -nn -vv 'tcp[tcpflags] & tcp-syn != 0'`，再看
`dmesg | grep tcpts-strip` 的 `stripped N SYNs`。

## IPv6：为什么直接封转发，而不是"再做一套 v6 规则"

实测这台路由器在 YBU 校园网上**没有 IPv6 出口**（`phy1-sta0` 只有 `fe80::`、
无默认路由、`wan6` `up=false`、`ping -6 2400:3200::1` 报 `Network unreachable`），
所以当下泄漏量为 0。

但 IPv6 的泄漏性质和 v4 完全不同：SLAAC 下每台设备一个全局地址、不经 NAT，
网关按源地址（EUI-64 里还带 MAC）就能逐台识别，等于"免检通道"——而
`rkp-ipid` / TTL 统一 / `ua2f` 全是 v4-only，一条都管不到 v6。

所以 `ganyu.main.block_ipv6=1`（默认开）会在 `table ip6 ganyu` 里建一条
`chain drop6 { type filter hook forward priority filter; }` + `counter drop`，
**v6 转发全封**：要么全走 v4（受我们控制），要么别走。目前校园网无 v6，代价为零；
哪天学校开了 v6，这条规则保证不会静默变成"三台机器各自上网"。

> 踩坑：链名不能叫 `fwd`——nft ≥ 1.0.9 把 `fwd` 当语句关键字，会报
> `syntax error, unexpected fwd, expecting string or last`。

## 反检测状态页

LuCI → **状态 → 反检测状态**（`/cgi-bin/luci/admin/status/ganyu`），5 秒自动刷新，显示：

- IPID：模块是否加载、`IPID_MOD` 是否挂在 OUTPUT/FORWARD、已改写包数
- nft `table ip ganyu`：DNS 劫持（UDP/TCP）、NTP 归位、TTL 统一、DoT 封锁各自的命中包数
- IPv6 转发是否已封（`table ip6 ganyu` 的 drop 计数）
- flow offloading 当前值（必须是 `0`，否则 rkp-ipid 收不到包）
- UA2F 进程状态与队列放行包数、ybu-login 的 `enabled`
- 无线 STA 的 SSID / 信号 / 信道 / 信号质量，WAN 拿到的地址
- 最近 5 条相关日志

实现是「`/usr/bin/ganyu-status` 出纯文本 + LuCI 视图原样显示」，所以命令行随时可以
`ssh root@192.168.1.1 ganyu-status` 看同一份数据。数据全取自 `iptables -L -v` / `nft list`，
**全是计数器，看一眼就是证据**，不需要信任任何中间层。

## 内置附加包

`BUILTIN_PKGS`（在 workflow「生成 .config」步骤里）会把下表这批包直接编进镜像，
合计约 **1.1 MB**，比事后 opkg 装进 overlay 省 2–3 倍空间（squashfs 有压缩）。
包名全部按 24.10.8 官方 `Packages` 索引核实过是**真实包名**（不是 `PROVIDES` 别名），
任何一个写错都会被「额外包自检」拦下并 `::error::` 中断，不会静默丢包。

| 用途 | 包 | 说明 |
|---|---|---|
| 传文件 | `openssh-sftp-server` | 设备原本没有 sftp 子系统，只能 `cat >` 硬灌 |
| 命令行 | `nano` `curl` `htop` `lsof` `conntrack` `iftop` | `curl` 是 apk/脚本通用刚需 |
| 网页终端 | `ttyd` + `luci-app-ttyd` | 302 KB，手机也能开 shell |
| 看门狗 | `watchcat` + `luci-app-watchcat` | WAN 掉线自动重启接口（**默认不启用**） |
| 流量统计 | `nlbwmon` + `luci-app-nlbwmon`、`vnstat` + `luci-app-vnstat2` | 按客户端 / 按接口 |
| 限速 | `sqm-scripts` + `kmod-sched-cake` + `luci-app-sqm` | CAKE 整形（**默认不启用**，无线 WAN 上要手动调） |
| 多拨容灾 | `mwan3` + `luci-app-mwan3` | 多 WAN 健康检查（由 `92-packfix` 强制停用，见下） |
| VPN | `wireguard-tools` + `kmod-wireguard` + `luci-proto-wireguard` | 只有工具和协议，不建隧道 |
| 端口映射 | `miniupnpd-nftables` + `luci-app-upnp` | nft 后端（**默认不启用**） |
| DNS | `https-dns-proxy` + `luci-app-https-dns-proxy` | DoH 客户端（由 `92-packfix` 强制停用，见下） |
| 远程唤醒 | `etherwake` + `luci-app-wol` | 局域网唤醒 |
| 拥塞控制 | `kmod-tcp-bbr` + `files/etc/uci-defaults/94-bbr` | 首启把 `tcp_congestion_control` 设成 `bbr`，写进 `/etc/sysctl.conf` |
| 命令页 | `luci-app-commands` | LuCI 里存常用命令 |

### `92-packfix`：为什么有两个包必须默认关掉

这两个包**装上就接管网络**，在没通过校园网门户认证的环境里会把上网能力搞死，
所以 `files/etc/uci-defaults/92-packfix` 在首启时无条件停用它们（包仍装在固件里，
`/etc/init.d/xxx enable` 随时能开回来）：

| 包 | 它干了什么 | 现场症状 |
|---|---|---|
| `https-dns-proxy` | 默认 `force_dns=1` + `dnsmasq_config_update='*'`，首启就把 dnsmasq 上游换成 `127.0.0.1#5053`/`#5054`（Cloudflare / Google 的 DoH）。未认证时 443 出不去 → DoH 连不上 → **所有解析超时** | `nslookup www.baidu.com` → `;; connection timed out; no servers could be reached`，但 `ping 10.126.112.1` 正常 |
| `mwan3` | 默认拿 `1.0.0.1`/`1.1.1.1`/`208.67.222.222` 做 track 目标，校园网未认证时不可达 → wan 被判 offline，五条策略全 unreachable | `mwan3 status` → `interface wan is offline and tracking is active`，日志每 4 秒刷 `Check (ping) failed` |

`92-packfix` 除了停服务，还会 `uci -q delete dhcp.@dnsmasq[0].server / noresolv / doh_server / ...`，
因为光停服务不够——被改过的 dnsmasq 配置会留在 `/etc/config/dhcp` 里继续生效。


刻意**没**装的：`wpad-mbedtls`（换掉 basic 净增 264 KB，收益不明显）、
`sing-box`/`v2ray`/`xray`（十几 MB 的 Go 二进制，正是 UA3F 翻车的那个坑）、
`zerotier`、`aria2`（3A 没有 USB）、`samba4`。

## 指纹补丁：九条"实测过才知道"的漏洞

这些不是拍脑袋加的，都是把 `nft list table inet ua2f` / `ps w | grep udhcpc` /
`netstat -lntp` 摊开看之后发现的真漏点。

| 漏点 | 现场证据 | 修法 |
|---|---|---|
| **校内 HTTP 的 UA 根本没改** | `nft list table inet ua2f` 里 `ip daddr @localaddr_v4 counter packets 251132 bytes 216422578 return`；`localaddr_v4` 硬编码了 `10.0.0.0/8`，而校园网就是 `10.126.0.0/20`（`ip route` 可见）。访问教务/门户等校内 http 站点时 UA 原样出网——而校方最方便查多设备的地方恰恰是自己网关 | `files/etc/init.d/ua2f` 把 `10.0.0.0/8` 从 `localaddr_v4` 摘掉（保留 `192.168.0.0/16` 让自家 LAN 不被改写） |
| **DHCP 自曝 `OpenWrt`** | `ps w \| grep udhcpc` → `-x hostname:OpenW`。`/lib/netifd/proto/dhcp.sh:48` 在没配 hostname 时拿 `/proc/sys/kernel/hostname` 兜底，于是 option 12 = `OpenWrt`，全校唯一一台这么自报的设备 | `files/etc/uci-defaults/89-wan-hostname` 把 `system.hostname` + `network.wan.hostname` 都改成普通机器名（默认 `DESKTOP-5G4QQFQ`），并顺手 `echo > /proc/sys/kernel/hostname` |
| **管理面监听 `0.0.0.0`** | `netstat -lntp` → uhttpd `0.0.0.0:80`/`[::]:443`、dropbear `0.0.0.0:22`。靠 fw4 的 wan `input=REJECT` 挡着，但 fw4 reload 一出错就整排摆在校园网里 | `files/etc/uci-defaults/88-bindlan`：uhttpd 绑 `192.168.1.1`、dnsmasq `interface=lan`+`notinterface=wan`、dropbear `Interface=lan` |
| **固定 60s 心跳** | ybu-login 每 60 秒准点探测 `gstatic/generate_204`，全网唯一节奏 | `files/usr/bin/ybu-login` 守护循环加 ±15% 抖动（`awk` 生成，不依赖 `$RANDOM`） |
| **WAN MAC 是小米 OUI** | `cat /sys/class/net/eth0/address` → `50:64:2b:c1:00:55`。网关瞄一眼前缀就知道链路上挂的是小米路由器，比任何协议指纹都直接 | `files/etc/uci-defaults/96-macclone`：没指定 `ybu.main.wan_mac` 时自动生成一个 Intel OUI（`3c:a9:f4`）的 MAC 并写进 uci（重启后固定），同时写 `network.wan.macaddr` 与 `wireless.wan_sta.macaddr` |
| **DHCP 不发 vendor-class** | 真 Windows 的 DISCOVER 带 option 60 = `MSFT 5.0`、option 55 是固定顺序的一串；busybox udhcpc 默认一个都不发 | `files/etc/uci-defaults/86-wan-dhcp`：`vendorid=MSFT 5.0`、`defaultreqopts=0`、`reqopts` 换成 Windows 10/11 的清单（1 3 6 15 31 33 43 44 46 47 119 121 249 252） |
| **DoH 绕过 53 劫持** | 客户端用 `dns.google`/`cloudflare-dns.com` 走 443，网关看不到查询内容，等于绕过 DNS 收敛 | `files/etc/init.d/ganyu-guard` 的 `block_doh`：nft `post` 链里封已知公共 DoH IP 的 443（tcp+udp）；另加 dnsmasq canary `use-application-dns.net` → NXDOMAIN，让 Firefox 自动关 DoH |
| **路由器的 DNS/NTP 服务对外可达** | `netstat -lnup` → `:::123`（ntpd `-l` 全接口）、`10.126.118.213:53`（dnsmasq 绑到 WAN 地址）。现在只靠 fw4 的 wan `input=REJECT` 挡着 | ganyu 表里加 `gin`（input 钩子）：`iifname { "lo", "br-lan" } accept`，其余来源的 udp 53/123 直接 drop。**必须放行 lo**，否则路由器自己也解析不了 |
| **只在 80 上改 UA** | 校内站点/代理也常见 8080 | `files/etc/init.d/ua2f` 的 nft 规则 `tcp dport { 80, 8080 } ct mark set 44` |

还有两条**明确不做**：TLS 的 JA3/JA3S（逐设备不同，除非 MITM 给每台设备装根证书，App pinning 必炸）、
TCP 窗口/wscale/ISN（改了会破坏流控与序列号，收益远小于风险）。

还有一条**改配置解决不了、只能改拓扑**的：现在 WAN 走的是 `phy1-sta0`（无线 STA 连校园网 `YBU`），
校园的 AP 因此能直接看到我们 802.11 关联层的全部能力集 —— 支持速率集、HT/VHT capabilities、
扩展能力位，一眼就是 Linux `mac80211`，和手机/笔记本完全不同。**插网线走
`ybu-login mode wired` 就能整块抹掉**，这也是为什么有线模式比无线模式干净得多。

## 已知限制

- 首次构建要下 ~1.5 GB 源码包，`dl/` 会走 Actions 缓存（key `ow-dl-24.10.8-<run_id>`）
- 镜像上限按 **15,204,352 B** 卡（设备实测 mtd7），留了安全余量；超了要减包或用
  `CONFIG_STRIP_KERNEL_EXPORTS`
- UA2F 依赖 `kmod-nft-queue`（NFQUEUE）与 `libnetfilter-queue`（来自 feeds/packages/libs）；
  改 `.config` 时这两条不能删，否则 ua2f 起来了但规则建不出来、HTTP 不生效
