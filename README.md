# 云端编译 R3A 固件（含 rkp-ipid 内核模块 + UA3F）

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
package-src/ua3f-bin/Makefile          UA3F 的打包元数据（只声明依赖，不编译）
local-ipk/ua_3.7.0-r1_mipsel_24kc.ipk  UA3F 官方预编译包（CI 里解包成文件包）
files/                                 根文件系统覆盖层
  etc/init.d/rkp-ipid-mark             iptables mangle 打 mark + 关 flow offloading
  etc/uci-defaults/90-rkp-ipid         首次启动启用上面的 init 脚本
  etc/uci-defaults/91-ua3f             首次启动打开 ua3f 并注册自启
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
   - rootfs 里 `/usr/bin/ua3f`、`/etc/init.d/ua3f`、`/etc/init.d/rkp-ipid-mark` 都在
   - 镜像 ≤ **15,204,352 字节**（mtd7 firmware 分区真实大小，不是 profile 里那个 16064k）

3. Artifacts 下载 `openwrt-24.10.8-miwifi-3a-<n>`，里面有
   `*.squashfs-sysupgrade.bin` / `*.manifest` / `sha256sums` / `.config` / `build.log`。

4. LuCI → 系统 → 备份/刷写固件 → 上传 → 首次换固件建议**不勾**"保留配置"。

## UA3F 是怎么进固件的

`local-ipk/` 里那个 ipk **不是标准 ar 归档**，而是 `tar.gz` 套
`debian-binary / control.tar.gz / data.tar.gz`（`ar p ...` 那句在老流程里从来就没成功过）。
CI 里两种格式都兼容，解出来以后 `data` 树直接塞进 `package/ua3f-bin/files/`，
依赖用 `DEPENDS` 声明（照抄原 ipk 的 `Depends`，一条不落）：

```
+luci-compat +ipset +iptables +iptables-mod-tproxy +iptables-mod-extra
+iptables-mod-ipopt +iptables-mod-nfqueue +iptables-mod-conntrack-extra
+kmod-nf-conntrack-netlink
```

好处是不用往 CI 里塞 Go 工具链，编译时间不变；坏处是 UA3F 升级要重新换 ipk。

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
- 只整合了 `local-ipk/ua_*.ipk` 这一个预编译包；再放别的 ipk 进去不会自动装
