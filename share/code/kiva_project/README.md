# kiva_project

当前接入的是本地 LVGL **10.0.0-dev** 源码，使用 SDK 的 ARM hard-float 工具链编译。
LVGL 以静态库链接进应用，显示使用 Linux framebuffer，触摸直接读取 Linux input 事件。

## 编译

在本目录执行：

```bash
./lib/lvgl/build.sh
make -j8
```

首次接入已生成库和公开头文件，平时修改应用只需执行 `make`。
清理应用使用 `make clean`，不会删除 LVGL 库。

库构建脚本默认读取 SDK 同级目录的 `source_library/lvgl-master`；也可指定源码：

```bash
./lib/lvgl/build.sh /home/sent/linux/source_library/lvgl-master
```

使用的工具链是 SDK 中的 `system/tool/compile-chain/bin/arm-linux-gnueabihf-`。
库和应用均使用 Cortex-A7、VFPv3-D16、hard-float 参数。
构建不下载依赖；构建前会向外部 LVGL 源码应用 `lib/lvgl/patches/` 中的 JPG 解码结束修复，已应用时跳过。

## 文件位置

| 路径 | 用途 |
| --- | --- |
| `lib/lvgl/lv_conf.h` | 本项目的 LVGL 配置，修改后重新构建库及应用 |
| `lib/lvgl/build.sh` | 交叉编译、安装库与头文件的入口 |
| `lib/lvgl/lib/liblvgl.a` | ARM 静态库 |
| `lib/lvgl/include/lvgl/` | 配套公开头文件与实际构建配置 |
| `hw_drive/lvgl_port/` | framebuffer 初始化、Goodix 查找、触摸事件解析及状态恢复 |
| `application/ui/` | 最小界面与按钮计数回调 |
| `main.c` | 初始化、LVGL 事件循环、退出清理 |
| `tests/` | 触摸事件、无屏界面及错误路径测试 |

Makefile 沿用原有分段，通过模块列表和 LVGL 库配置接入。
`lib/lvgl/include/` 是构建脚本安装的文件，配置应修改 `lib/lvgl/lv_conf.h`。
公开头文件、配置和静态库应配套更新；版本标记见安装的 `lv_version.h`。
`SOURCE_VERSION.sha256` 记录来源版本文件的摘要，许可证在 `lib/lvgl/` 内。

## 开发板运行

将生成的 `kiva_project` 复制到开发板后执行：

```sh
chmod +x kiva_project
./kiva_project
```

默认打开 `/dev/fb0`，显示分辨率和像素格式从 framebuffer 查询；内核当前配置为 800×480、RGB565。
程序自动查找名为 Goodix 的触摸设备。找不到触摸时会提示并进入仅显示模式。
也可以明确指定设备节点：

```sh
cat /proc/bus/input/devices
./kiva_project /dev/fb0 /dev/input/event1
```

`event1` 只是示例，以开发板实际触摸节点为准。显式指定的设备不可用时程序返回错误。
运行账户需要有 framebuffer 的读写权限和 input 设备的读取权限。
初始界面包含标题、按钮和触摸计数；点击按钮应增加计数。Ctrl+C 退出。

LVGL 已静态链接，开发板部署应用时不需要部署 `liblvgl.a` 或 LVGL 头文件。
应用仍动态依赖 SDK 的标准 C/C++ 系统库，已核对当前 rootfs 包含这些库。
触摸通过内核报告的坐标范围缩放到屏幕坐标，将多点触摸转换成稳定的单个指针。
触摸设备需在程序启动前可用，当前适配不处理运行中的设备热插拔。

## 验证

在开发电脑执行：

```bash
./tests/run.sh
```

测试使用本机编译器和同一份 LVGL 源码构建测试库，不需要屏幕或触摸硬件。
覆盖非零触点槽、多点触摸主指针、抬起、坐标缩放和限幅、丢帧释放、排队快速点击，
以及真实 LVGL 按钮回调、RGB565 软件绘制、framebuffer 不存在和设备无效时的退出行为。

ARM 编译及主机测试通过不等于实物验收。开发板仍需确认 framebuffer 节点、实际显示、
触摸方向与坐标，以及退出后控制台的显示情况。
