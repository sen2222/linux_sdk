# i.MX6ULL 精简版 U-Boot

本源码树仅保留正点原子 i.MX6ULL 当前板级配置：

- 配置：`configs/mx6ull_sen_emmc_defconfig`
- 板级源码：`board/freescale/mx6ull_sen_emmc/`
- 板级头文件：`include/configs/mx6ull_sen_emmc.h`

其他 CPU 架构、SoC 平台目录、开发板及其 defconfig 和配置头文件已移除，
相关 Kconfig 和 Makefile 入口同步精简。

保留 ARMv7、i.MX6、i.MX 公共代码，以及当前板级代码使用的 Freescale
MMC 和电源公共文件。`MX6ULL` 会选择 `MX6UL`；i.MX6UL 的配置符号和
共用代码也是 i.MX6ULL 的依赖。通用驱动、命令、库和主机工具继续保留。

## 编译和清理

工具链位于 SDK 的 `tool/compile-chain/`，无需手动配置 PATH。

```bash
./build.sh
JOBS=4 ./build.sh
./clean.sh
```

构建产物为 `u-boot.imx`。清理执行 `make distclean`，同时删除 `.config`。
两个脚本均可通过路径从任意工作目录调用。

精简后已验证：完整构建通过，生效配置和 `include/autoconf.mk` 与精简前
一致；使用相同 `SOURCE_DATE_EPOCH` 时，`u-boot.imx` 与精简前逐字节一致。
