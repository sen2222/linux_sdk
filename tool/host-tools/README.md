# SDK 主机工具

内核配置使用 LZO 压缩。`kernel/build.sh` 优先使用系统中的 `lzop`，
系统未提供时使用 `bin/lzop`，无需安装系统软件包。

`bin/lzop` 是未经修改的 x86-64 主机程序，来自 Ubuntu 22.04 官方
`lzop` 软件包，版本 `1.04-2build2`。下载命令为 `apt-get download lzop`。
程序依赖主机的 glibc 和 `liblzo2.so.2`；版权及许可见 `lzop.COPYRIGHT`。
