# 本地 ipk 放这里
把这个目录里任意 *.ipk 交给 ImageBuilder，流程会自动：
1. 复制进 ImageBuilder 的 packages/
2. 从 ipk 的 control 里读出 Package 名，自动加入 PACKAGES 列表

硬性要求（不满足会在 make image 阶段报错）：
- 架构必须是 mipsel_24kc
- 与 24.10.8 同一 musl ABI
- 含 kmod 的包必须是 kernel vermagic 6.6.144-1-4ca73159125334b180ac4bba6dc45839
