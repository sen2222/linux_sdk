# i.MX6ULL SDK 内核

本源码树基于 NXP Linux 4.1.15，仅保留当前 i.MX6ULL 板卡的配置和平台源码：

- 内核配置：`arch/arm/configs/imx_sen_emmc_defconfig`
- 设备树：`arch/arm/boot/dts/imx6ull-sen-emmc.dts`
- SoC 支持：`SOC_IMX6ULL`、其依赖 `SOC_IMX6UL` 和公共 `SOC_IMX6`

原配置中的 ARMv6、其他 i.MX 型号及它们派生的专用驱动已停用。
其他 CPU 架构、ARM 平台目录、板卡 defconfig 和无关设备树已移除，
Kconfig 和 Makefile 入口同步精简。现有外设、网络、文件系统等配置沿用原配置，
通用驱动和公共内核代码继续保留。

设备树的本地依赖为 `imx6ull.dtsi`、`imx6ul-pinfunc.h`、
`imx6ull-pinfunc.h`、`imx6ull-pinfunc-snvs.h` 和 `skeleton.dtsi`。
公共设备树绑定头文件位于 `include/dt-bindings/`。
`arch/arm/boot/dts/include/dt-bindings` 的软链接作为预处理入口继续保留。

部分 DDR 调频汇编文件名包含 `imx6sx`，但也是 i.MX6UL/ULL 的实际依赖，
因此保留。`mach-imx/hardware.h` 引用的其他型号寄存器头文件也继续保留。
`arch/x86/` 仅保留 `syscalls/syscall_32.tbl`：ARM 构建使用它检查系统调用，
该目录不再包含 x86 平台源码或配置。禁用 UML 时，清理规则不再加载 UML 专用规则。

## 编译、清理和导出

使用 SDK 的 `tool/compile-chain/` 中的 64 位主机版 Linaro GCC 4.9.4：

```bash
./build.sh
JOBS=4 ./build.sh
./clean.sh
./cp_bin.sh
```

`build.sh` 从 defconfig 重新生成配置，并构建内核、模块和当前板卡设备树。
`clean.sh` 执行 `make distclean`，同时删除 `.config`。
`cp_bin.sh` 将已有内核和设备树复制到 SDK 外的 `../../share/`。
三个脚本均支持通过路径从任意工作目录调用。

主要产物：

- `arch/arm/boot/zImage`
- `arch/arm/boot/dts/imx6ull-sen-emmc.dtb`
- 各模块目录中的 `.ko`、顶层 `modules.order` 和 `Module.symvers`

主机需要 `make`、GCC、`bc`、Perl 和 `lzop`。缺少系统 `lzop` 时，
构建脚本使用 `tool/host-tools/bin/lzop`，无需安装系统软件包。
旧版 DTC 的 `yylloc` 声明已修复，可使用当前主机 GCC 编译。

其他架构专用的工具和构建目标不属于此精简源码树的支持范围。

## 整理验证

已删除 15,193 个无关文件，原源码备份位于
`/home/sent/linux/backups/kernel-before-prune-20261007.tar.gz`。

精简后的源码已完成清理和完整构建。固定内核构建时间、用户、主机和版本号后，
与精简前的 i.MX6ULL 单目标基线对照，启用配置、解压后的 `Image`、设备树、
16 个模块、`modules.order` 和 `Module.symvers` 均一致。
`zImage` 的差异仅为 LZO 压缩头的时间戳及头校验和。
设备树也与原多型号配置生成的文件一致；导出脚本已验证。
尚未进行上板启动验证。
