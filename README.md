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
  etc/uci-defaults/{90-rkp-ipid,93-ua2f,95-ybu,96-macclone,97-ganyu,98-wan-sta}
  usr/bin/ybu-login                    门户登录脚本本体
  etc/uci-defaults/99-lan-ap           语言 / 无线 / 区域（SSID 只在还是 OpenWrt 默认名时才写）
```

## 用法

1. 仓库页面 → **Actions** → 左侧 `build-firmware` → **Run workflow**。
   两个输入：

   | 输入 | 说明 | 默认 |
   |---|---|---|
   | `extra_packages` | 额外装进固件的包，空格分隔 | 中文三件套 + tcpdump/iperf3/mtr |
   | `remove_packages` | 要剔除的包，空格分隔（写 `# CONFIG_PACKAGE_x is not set`） | 空 |

2. 等 50–70 分钟。构建会自己校验：
   - `rkp-ipid.ko` 确实生成，并打印它的 `vermagic=`
   - rootfs 里有 `/usr/bin/ua2f`、`/etc/init.d/ua2f`、`/etc/config/ua2f`、`/etc/init.d/rkp-ipid-mark`，
     且 `/usr/bin/ua3f` **不在**（还有人往里塞 Go 版就红）
   - 镜像 ≤ **15,204,352 字节**（mtd7 firmware 分区真实大小，不是 profile 里那个 16064k）

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

## 已知限制

- 首次构建要下 ~1.5 GB 源码包，`dl/` 会走 Actions 缓存（key `ow-dl-24.10.8-<run_id>`）
- 镜像上限按 **15,204,352 B** 卡（设备实测 mtd7），留了安全余量；超了要减包或用
  `CONFIG_STRIP_KERNEL_EXPORTS`
- UA2F 依赖 `kmod-nft-queue`（NFQUEUE）与 `libnetfilter-queue`（来自 feeds/packages/libs）；
  改 `.config` 时这两条不能删，否则 ua2f 起来了但规则建不出来、HTTP 不生效
