# SDK 总构建入口

在 SDK 顶层使用 Bash 执行：

```bash
source build/config.sh
./build/build_all.sh
```

清理时执行 `./build/clean_all.sh`。
也可以在 `build/` 内执行 `source config.sh`、`./build_all.sh`、`./clean_all.sh`。
脚本通过自身路径定位 SDK，支持从其他目录调用。

## 目录结构

- `build/`：SDK 总构建、环境配置和清理脚本。
- `system/`：`kernel/`、`uboot/`、`rootfs/` 和 `tool/`，包含系统源码与工具链。
- `share/`：`code/` 用户态示例、`dev_node/` 驱动示例及 `docs/` 资料。
- `out/`：SDK 生成固件和校验文件。

内核、U-Boot 可分别运行 `./system/kernel/build.sh` 和 `./system/uboot/build.sh`。
`share/dev_node/` 中的 Makefile 默认使用 `system/kernel`，也可通过 `KERNELDIR=...` 指定其他已配置的内核目录；交叉编译前先加载 SDK 环境。

## 环境配置

`config.sh` 导出 `SDK_ROOT`、`TOOLCHAIN_DIR`、`ARCH=arm`、`CROSS_COMPILE`、
`OUT_DIR`、`JOBS`，并将 SDK 工具链及主机工具加入 `PATH`。
重复 source 不会重复添加路径，不改变工作目录或 shell 选项。
用 `export JOBS=4` 设置并行任务数。

## 构建流程

`build_all.sh` 的 `main()` 按顺序调用：

1. `check_environment()`：检查环境变量和主机工具。
2. `prepare_output()`：创建暂存输出目录。
3. `build_uboot()`：调用 U-Boot 构建脚本，收集 `u-boot.imx`。
4. `build_kernel()`：调用内核构建脚本，收集内核和设备树。
5. `pack_rootfs()`：直接打包现有 `system/rootfs/rootfs/`。
6. `publish_output()`：生成校验文件，将整组固件更新到 `out/`。

每个步骤先检查 `out/` 中对应的固件：已有非空文件就复用，缺少或为空时才生成。
内核分别检查 `zImage` 和 DTB；只缺 DTB 时仅编译设备树，只缺 `zImage` 时编译内核及模块，保留已有的另一份固件。
即使全部固件都已存在，也会重新生成 `SHA256SUMS`。
该判断只检查文件是否存在且非空，不比较源码修改时间。修改源码后，删除 `out/` 中对应固件再运行；需要全部重建时先执行 `./build/clean_all.sh`。

rootfs 仅打包现有内容，不编译或安装 BusyBox，不安装内核模块，也不改写原目录。
已有程序、库、启动配置和模块按原样保留。
归档保留隐藏文件、软链接和权限，属主统一为 root，排除旧 rootfs 归档。
修改文件系统内容应直接编辑 `system/rootfs/rootfs/`。
启动脚本通过 `udhcpc` 自动获取 `eth0` 的 IP、默认网关和 DNS。
`usr/share/udhcpc/default.script` 处理租约和续租，并更新 `/etc/resolv.conf`。
网络中需要提供 DHCP 服务；开发板地址以实际租约为准。
必须保留 `/dev`、`/proc`、`/sys`、`/tmp` 挂载目录；打包前会检查这些目录。
其中 `/dev` 由内核自动挂载 devtmpfs 并生成设备节点，其他目录由 `rcS` 挂载。
源目录中的 `.gitkeep` 用于让 Git 保留空挂载目录，请勿删除挂载目录。

SDK 顶层 `out/` 中的产物为：

- `u-boot-imx6ull14x14evk_emmc.imx`
- `zImage`
- `zImage-imx6ull-14x14-evk-emmc.dtb`
- `rootfs_nogpu.tar.bz2`
- `SHA256SUMS`（在 `out/` 内执行 `sha256sum -c SHA256SUMS` 验证）

编译或打包失败时停止构建并清理暂存目录，保留旧输出。
内部辅助文件 `publish.sh` 负责整组更新及失败恢复。
`out/` 专用于生成固件。

## 清理和独立打包

`clean_all.sh` 的 `main()` 依次调用 `clean_uboot()`、`clean_kernel()`、
`clean_output()`，清理 U-Boot、kernel 编译产物、生成配置、暂存目录及整个 `out/`。
清理不需要先 source，保留原始 rootfs、BusyBox 目录、defconfig 和工具链。

只打包 rootfs：`./system/rootfs/packet.sh`，默认输出 `out/rootfs_nogpu.tar.bz2`。
也可指定输入和输出：`./system/rootfs/packet.sh <rootfs目录> <归档路径>`。
