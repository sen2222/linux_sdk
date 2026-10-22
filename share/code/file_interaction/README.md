# 文件操作服务端与客户端

提供 C/TCP 文件下载命令，电脑运行服务端、i.MX6ULL 开发板运行客户端。也可将 ARM 服务端运行在开发板，使用电脑端客户端下载文件。默认端口为 `9000`，目前支持 `cp` 下载单个普通文件。

## 编译与清理

在 SDK 根目录执行：

```sh
source build/config.sh
./share/code/file_interaction/build.sh
```

| 输出 | 运行位置 |
|---|---|
| `out/file_server`、`out/file_client` | i.MX6ULL ARM Linux |
| `out/host/file_server`、`out/host/file_client` | 当前 Linux 电脑 |
| `out/file-interaction-imx6ull.tar.gz` | ARM 程序及说明文档的打包文件 |
| `out/SHA256SUMS` | ARM 程序和压缩包校验值 |

这里的 `out` 位于 `share/code/file_interaction` 下。ARM 程序使用 SDK glibc，只依赖 libc，不需要额外安装网络库或编译 BusyBox。电脑版本也可单独执行 `make -C share/code/file_interaction host` 构建，无须 source 环境。

清理当前项目所有生成文件：

```sh
./share/code/file_interaction/clean.sh
```

## 服务端

```sh
./file_server DIRECTORY [PORT]
```

在电脑上共享 SDK 的固件目录：

```sh
./share/code/file_interaction/out/host/file_server ./out
```

指定端口：

```sh
./share/code/file_interaction/out/host/file_server ./out 9001
```

服务端在前台运行，监听 IPv4 `0.0.0.0`。Ctrl+C 退出；每个连接独立处理，允许并发下载。程序提供局域网文件共享，连接到该端口的设备都能下载共享目录中的文件，因此应选择需要共享的目录。

## 客户端

```sh
./file_client IP [PORT] cp REMOTE_PATH DESTINATION
```

你的示例可以直接使用：

```sh
./file_client 192.168.31.182 cp SERVER-DIR/txt ./
```

`SERVER-DIR/` 是共享目录标识，不是服务器上的绝对路径。服务端若启动为 `./file_server /home/sent/shared`，上面的命令下载 `/home/sent/shared/txt`，保存到当前目录的 `txt`。也可以直接写相对路径：

```sh
./file_client 192.168.31.182 cp txt ./
./file_client 192.168.31.182 9001 cp subdir/txt ./
./file_client 192.168.31.182 cp txt ./renamed.txt
./file_client 192.168.31.182 cp "subdir/a b.bin" ./
```

目标是已有目录时，保存为远端文件的文件名；否则按目标文件名保存，其父目录必须存在。已有普通文件会被覆盖，已有文件权限保留；新文件权限来自源文件并受客户端 umask 限制。文件完整接收并刷新后才重命名到目标；下载期间发生断线、超时、Ctrl+C 或错误请求时保留原文件并清理临时文件。最终重命名成功即表示复制完成，此时到达的取消信号不改变成功结果。不可捕获的 SIGKILL 或断电可能留下 `.file-client-*` 临时文件。

服务端只允许共享目录中的普通文件，包括子目录里的文件；拒绝绝对路径、`..`、符号链接、目录和 FIFO。客户端也拒绝将文件目标符号链接作为覆盖目标。源文件应在传输期间保持稳定；此命令不提供源文件快照。当前不支持目录递归、上传、shell 命令或加密连接。数字 IPv4 地址直接连接，不需要 DNS。

## 开发板部署

首次安装客户端时，电脑在 SDK 根目录提供 HTTP 下载：

```sh
python3 -m http.server 8000 --directory share/code/file_interaction/out
```

开发板执行，按实际电脑 IP 修改地址：

```sh
mkdir -p /usr/local/bin
cd /tmp
wget -O file_client http://192.168.31.182:8000/file_client
chmod +x file_client
cp file_client /usr/local/bin/file_client
```

电脑另一个终端启动文件服务端共享 SDK `out` 后，开发板下载固件：

```sh
export LD_LIBRARY_PATH=/lib:/usr/lib
file_client 192.168.31.182 cp SERVER-DIR/zImage /tmp/
file_client 192.168.31.182 cp SERVER-DIR/zImage-imx6ull-14x14-evk-emmc.dtb /tmp/
```

以上命令只下载文件，不会烧写 eMMC 或自动重启。服务端没有启动、IP 不可达、端口被防火墙拦截时，客户端会返回非零退出码并打印错误；应确保开发板可以访问电脑的 TCP 服务端口。

## 测试

```sh
./share/code/file_interaction/test.sh
```

使用真实本机 TCP 连接验证默认/显式端口、文件内容及权限、空文件、二进制文件、长文件名、并发、共享目录限制、畸形协议、截断响应及中断时的目标文件保护。测试需要本机 TCP 端口 `9000` 空闲。

## 协议与源码

| 文件 | 用途 |
|---|---|
| `src/common.[ch]` | 网络完整读写、端口检查、协议编码解码 |
| `src/server.c` | 共享目录解析、监听、子进程连接处理和文件发送 |
| `src/client.c` | 命令解析、连接、目标路径及文件接收 |
| `tests/test_transfer.py` | TCP 集成测试 |

请求：8 字节 `FICP0001`、32 位命令号（cp 为 1）、32 位路径字节数，随后为不含结尾 NUL 的路径。响应：8 字节 `FIRS0001`、32 位状态、64 位正文长度、32 位文件权限，随后为文件内容或英文错误信息。所有整数采用网络字节序。路径最多 4096 字节，错误信息最多 1024 字节，网络读写单次等待超时为 30 秒。
