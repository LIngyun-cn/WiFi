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
package-src/ua2f/{Makefile,src.tar.gz}    UA2F（C 实现）打包元数据 + vendored 源码包
local-ipk/ua_3.7.0-r1_mipsel_24kc.ipk   UA3F 官方预编译包（已停用，仅留档，不参与构建）
files/                                 根文件系统覆盖层
  etc/init.d/rkp-ipid-mark             iptables mangle 打 mark + 关 flow offloading
  etc/init.d/ua2f                      上游 v5.2.0 的 init（nft / iptables 双路径）
  etc/init.d/ganyu-guard               防检测：DNS 劫持 / NTP 归位 / TTL 统一 / DoT 封锁
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

## 反检测状态页

LuCI → **状态 → 反检测状态**（`/cgi-bin/luci/admin/status/ganyu`），5 秒自动刷新，显示：

- IPID：模块是否加载、`IPID_MOD` 是否挂在 OUTPUT/FORWARD、已改写包数
- nft `table ip ganyu`：DNS 劫持（UDP/TCP）、NTP 归位、TTL 统一、DoT 封锁各自的命中包数
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

## 已知限制

- 首次构建要下 ~1.5 GB 源码包，`dl/` 会走 Actions 缓存（key `ow-dl-24.10.8-<run_id>`）
- 镜像上限按 **15,204,352 B** 卡（设备实测 mtd7），留了安全余量；超了要减包或用
  `CONFIG_STRIP_KERNEL_EXPORTS`
- UA2F 依赖 `kmod-nft-queue`（NFQUEUE）与 `libnetfilter-queue`（来自 feeds/packages/libs）；
  改 `.config` 时这两条不能删，否则 ua2f 起来了但规则建不出来、HTTP 不生效
