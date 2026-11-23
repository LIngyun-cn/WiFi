# 云端 ImageBuilder —— 用 GitHub Actions 编 R3A 固件

仓库结构：

```
.github/workflows/build-firmware.yml   # 构建流程（已写好）
files/                                  # 要预置进固件的文件（可选）
  etc/uci-defaults/99-lan-ap            # 首次启动脚本示例
packages/                               # 你自己的包源码（可选，每包一个目录）
```

## 用法

1. 在 GitHub 建一个空仓库（私有也行，构建跑 8 分钟左右，免费额度够用）。
2. 把本目录内容推上去：

```bash
cd D:\dsh-webdapru\gh-build
git init
git add -A
git commit -m "cloud imagebuilder"
git branch -M main
git remote add origin https://github.com/<你的用户名>/<仓库名>.git
git push -u origin main
```

3. 仓库页面 → **Actions** → 左侧 `build-openwrt-firmware` → **Run workflow**。
4. 填两个输入：

| 输入 | 说明 | 默认 |
|---|---|---|
| `extra_packages` | 额外装进固件的包，空格分隔 | 中文三件套 + tcpdump/iperf3/mtr |
| `remove_packages` | 想从默认列表删掉的包（目前只是占位，未生效） | 空 |

5. 跑完在 build 页面底部 **Artifacts** 下载 `openwrt-24.10.8-xiaomi_miwifi-3a-<n>`，解压得到 `...squashfs-sysupgrade.bin`。
6. LuCI → 系统 → 备份/刷写固件 → 上传 → 不勾"保留配置"（首次换固件建议干净刷）。

## 带自己的本地包

把包源码目录放进 `packages/`，例如 `packages/ua3f/Makefile`。流程会自动：

1. 下 SDK → `feeds update/install` → 把你 `packages/` 下的目录拖进 `package/`
2. 按目录名追加 `CONFIG_PACKAGE_<目录名>=m` 然后编译
3. 把编出来的 `.ipk` 复制进 ImageBuilder 的 `packages/`，再打进镜像

**注意**：目录名必须等于包的 config 符号名（`PKG_NAME`），否则第 2 步勾不中，编出来是空。

## 已知限制

- 构建服务器上是 **官方 feeds**，你的包必须有完整的依赖声明
- 镜像上限 16 MiB，步骤日志里会打印 `image bytes` 和占比，超了直接失败
- 缓存键按 `24.10.8 + ramips/mt76x8` 固定，换版本要改 `OW_VER`
- 私有仓库 Actions 也消耗额度（2000 分钟/月，一次构建约 8 分钟）
