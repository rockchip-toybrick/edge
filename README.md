
# Toybrik RK3588 Linux Debian11 快速入门

[TOC]

---

## 前言

为了方便文档描述，约定如下变量定义：

- BOARD：开发板/产品型号；如Toybrick RK3588开发板的型号是TB-RK3588X0。
- DTB：内核设备树；如Toybrick RK3588开发板的产品内核设备树是rk3588-toybrick-x0-linux。
- ROOT_DIR：边缘计算SDK的工作目录；文档中所示的目录为/home/toybrick/work/edge。
- CHIP：开发板的芯片型号；当前边缘计算SDK支持的芯片型号为rk3588。
- OUT_DIR：编译生成的镜像路径${ROOT_DIR}/out/${CHIP}/${BOARD}/images ，如RK3588 EVB1的路径为/home/toybrick/work/edge/out/rk3588/TB-RK3588X0/images。

## 搭建系统环境

### 编译主机系统要求

- Ubuntu18.04及以上和Debian11版本，内存推荐16GB及以上。
- 系统的用户名不能有中文字符。
- 只能使用普通用户搭建开发环境，不允许用root用户执行。

### 安装编译依赖基础软件

#### Debain系统安装依赖

```shell
sudo apt -y install python lz4 coreutils qemu qemu-user-static python3 \
devicetree-compiler clang bison flex lld libssl-dev bc genext2fs git make
```

#### Ubuntu系统安装依赖

```shell
sudo apt -y install python lz4 coreutils qemu qemu-user-static python3 \
device-tree-compiler clang bison flex lld libssl-dev bc genext2fs git make
```

### 获取源码

1. 注册github账号，并上传本地ssh公钥

2. 执行如下命令获取代码：

   ```shell
   git clone git@github.com:rockchip-toybrick/edge.git
   cd edge
   ./download_rootfs.sh  #获取ROOTFS二进制软件包和Debian Core
   ```
   

### 获取rootfs镜像

[Toybrick Wiki (rock-chips.com)](https://t.rock-chips.com/wiki.php?filename=资料下载/资料下载)

## 编译配置

### 设置配置信息

执行如下执行命令，输入产品型号的序号（如：RK3588 EVB1开发板所对应的序号为0）设置配置信息：

```shell
./edge set

[EDGE DEBUG] Board list:
> rk3588
  0. TB-RK3588X0
Enter the number of the board: 1
```

***注意：每次更新或修改边缘计算SDK相关代码后，请重新执行此命令，更新配置。***

### 查看配置信息

执行如下命令，查看当前配置信息：

```shell
./edge env

[EDGE DEBUG] root path: /home/toybrick/edge
[EDGE DEBUG] out path: /home/toybrick/edge/out/rk3588/TB-RK3588X0/images
[EDGE DEBUG] board: TB-RK3588X0
[EDGE DEBUG] chip: rk3588
[EDGE DEBUG] arch: arm64
[EDGE DEBUG] bootmode: extlinux
[EDGE DEBUG] > Partition:
[EDGE DEBUG]   uboot: ['0x00002000', '0x00004000']
[EDGE DEBUG]   misc: ['0x00006000', '0x00002000']
[EDGE DEBUG]   boot_linux:bootable: ['0x00008000', '0x00020000']
[EDGE DEBUG]   recovery: ['0x00028000', '0x00040000']
[EDGE DEBUG]   resource: ['0x00068000', '0x00010000']
[EDGE DEBUG]   rootfs:grow: ['0x00078000', '-']
[EDGE DEBUG] > Uboot:
[EDGE DEBUG]   config: rk3588-toybrick
[EDGE DEBUG] > Kernel:
[EDGE DEBUG]   version: 5.10
[EDGE DEBUG]   config: rk3588_edge.config rk3588_toybrick.config
[EDGE DEBUG]   dtbname: rk3588-toybrick-x0-linux
[EDGE DEBUG]   size: 64
[EDGE DEBUG]   docker: False
[EDGE DEBUG]   debug: 0xfeb50000
```

### 配置信息说明

SDK配置项包含系统配置、安全启动、分区配置、uboot配置、内核配置和文件系统配置。配置说明如下：

- 公共配置保存在vendor/common/config.json。
- 板级配置保存在vendor/${CHIP}/${BOARD}/config.json，其值会覆盖公共配置的同名配置项的值。
- 值为“not set”的配置项必须在《板级配置》中设置；其他配置项可根据实际需要在《板级配置》中修改。

#### 系统配置

系统配置项包含board、chip、arch和bootmode配置项：

| 配置项   | 描述             | 默认值   | 备注                                                  |
| -------- | ---------------- | -------- | ----------------------------------------------------- |
| board    | 开发板或产品型号 | not set  | board值必须和vendor/${CHIP}目录下的${BOARD}目录名一致 |
| chip     | 芯片型号         | not set  | RK3588和RK3588s的芯片型号都设置为rk3588               |
| arch     | 芯片架构         | not set  | arm或arm64                                            |
| bootmode | Uboot的启动方式  | extlinux | 支持extlinux和fit两种启动方式                         |

#### 分区配置

系统的分区信息，包括分区名，起始地址和分区大小（起始地址和分区大小的单位为block，每个block的大小为512字节）。编译脚本会根据bootmode设置的启动方式加载part-extlinux和part-fit分区表，分区表信息如下：

**part-extlinux：extlinux启动方式的系统分区**

| 配置项     | 描述           | 起始地址   | 分区大小   | 备注                       |
| ---------- | -------------- | ---------- | ---------- | -------------------------- |
| uboot      | uboot分区      | 0x00002000 | 0x00004000 | 必选                       |
| misc       | misc分区       | 0x00006000 | 0x00002000 | 烧写进入recovery模式，可选 |
| boot_linux | 内核分区       | 0x00008000 | 0x00020000 | 必选                       |
| recovery   | recovery分区   | 0x00028000 | 0x00040000 | 必选                       |
| resource   | resource分区   | 0x00068000 | 0x00010000 | 保存开机LOGO，必选         |
| rootfs     | 根文件系统分区 | 0x00078000 | -          | 所有剩余空间，必选         |

**part-fit: fit启动方式的系统分区**

| 配置项   | 描述           | 起始地址   | 分区大小   | 备注                       |
| -------- | -------------- | ---------- | ---------- | -------------------------- |
| uboot    | uboot分区      | 0x00002000 | 0x00004000 | 必选                       |
| misc     | misc分区       | 0x00006000 | 0x00002000 | 烧写进入recovery模式，可选 |
| boot     | 内核分区       | 0x00008000 | 0x00020000 | 必选                       |
| recovery | recovery分区   | 0x00028000 | 0x00040000 | 必选                       |
| backup   | backup分区     | 0x00068000 | 0x00010000 | 可选                       |
| rootfs   | 根文件系统分区 | 0x00078000 | 0x01c00000 | 必选                       |
| oem      | oem分区        | 0x01c78000 | 0x00040000 | 可选                       |
| userdata | userdata分区   | 0x01cb8000 | -          | 所有剩余空间，可选         |

*说明：* 

1. *如果起始地址和分区地址都为0，则脚本会忽略此分区。*
2. *OTG口不接USB线、长按recovery按键，系统会从recovery分区引导，并进入紧急修复模式。*
3. *烧写misc镜像，系统也会从recovery分区引导，并进入紧急修复模式。*

#### uboot配置

| 配置项 | 描述          | 默认值      | 备注                       |
| ------ | ------------- | ----------- | -------------------------- |
| config | uboot编译配置 | rk3588-edge | configs/rk3588-edge.config |

#### 内核配置

| 配置项  | 描述                       | 默认值             | 备注                                  |
| ------- | -------------------------- | ------------------ | ------------------------------------- |
| version | 内核版本号                 | 5.10               | 目前只支持5.10内核                    |
| config  | 内核编译配置               | rk3588_edge.config | arch/arm64/configs/rk3588_edge.config |
| size    | 内核镜像的大小，单位：M    | 64                 | auto表示自动调整大小                  |
| dtbname | 内核设备树文件名           | not set            | 不包含后缀dts                         |
| docker  | 内核配置是否需要支持docker | false              | 内核镜像会增大                        |
| debug   | 芯片调试口物理地址         | not set            | 根据芯片配置                          |

说明：内核编译配置默认会加载rockchip_linux_defconfig和config配置项指定的配置。

## 镜像编译

### 一键编译

执行如下命令编译所有镜像，并打包update.img，保存在OUT_DIR目录：

```shell
./edge build -a
```

### 生成分区文件

执行如下命令生成parameter.txt，保存在OUT_DIR目录：

```shell
./edge build -p
```

### 编译Uboot镜像

执行如下命令编译生成MiniLoaderAll.bin和uboot.img镜像，保存在OUT_DIR目录：

```shell
./edge build -u
```

### 编译kernel镜像

执行如下命令编译生成内核镜像，保存在OUT_DIR目录：

```shell
./edge build -k
```

说明：

fit启动方式：生成boot.img和recovery.img

extlinux启动方式：生成boot_linux.img、recovery.img和resource.img

### 编译update镜像

执行如下命令编译生成update镜像，保存在OUT_DIR目录：

```shell
./edge build -U
```

### 查看编译帮助

查看支持的编译参数：

```shell
./edge build -h
```

## 烧写镜像

### 进入烧写模式

#### 进入loader烧写模式

1. 连接Type-C口到电脑PC端，按住主板的V+/Recovery按键不放。
2. 开发板供电12V，若已经上电，按下复位按键。
3. 当开发板进入loader模式后，松开按键。
4. 参考《查询烧写状态》章节，确认开发板进入loader模式。

#### 进入maskrom烧写模式

1. 连接Type-C口到电脑PC端，按住主板的Maskrom按键不放。
2. 开发板供电12V，若已经上电，按下复位按键。
3. 当开发板进入loader模式后，松开按键。
4. 参考《查询烧写状态》章节，确认开发板进入maskrom模式。

### 查询烧写状态

#### Linux主机查询

执行如下命令查询烧写状态:

```shell
./edge flash -q
```

- none：表示开发板未进入烧写模式。
- loader：表示开发板进入loader烧写模式。
- maskrom：表示开发板进入maskrom烧写模式。

#### Windows主机查询

双击打开tools\RKDevTool_Release_v2.84目录下的RKDevTool.exe，界面显示：

- 没有发现设备（如图1-1所示）：表示开发板未进入烧写模式。

- 发现一个LOADER设备（如图1-2所示）：表示开发板进入loader烧写模式。

- 发现一个MASKROM设备（如图1-3所示）：表示开发板进入maskrom烧写模式。

![none](./resources/Flash_none.png)

<center> 图1-1：没有发现设备</center>

![loader](./resources/Flash_loader.png)

<center> 图1-2：发现一个LOADER设备</center>

![maskrom](./resources/Flash_maskrom.png)

<center>图1-3：发现一个MASKROM设备</center>

### LINUX主机烧写镜像

#### 烧写所有镜像

烧写所有镜像

```shell
./edge flash -a
```

#### 烧写uboot镜像

烧写镜像：MiniLoaderAll.bin，uboot.img和parameter.txt

```shell
./edge flash -u
```

#### 烧写内核镜像

烧写内核镜像：

1. extlinux启动模式会烧写boot_linux.img、recovery.img和resource.img
2. fit启动模式会烧写boot.img和recovery.img

```shell
./edge flash -k
```

#### 烧写文件系统镜像

烧写镜像：rootfs.img

```shell
./edge flash -r
```

#### 烧写misc镜像

烧写misc镜像： misc.img

```shell
./edge flash -m
```

#### 烧写oem镜像

烧写oem镜像： oem.img (仅支持fit启动方式)

```shell
./edge flash -o
```

#### 烧写userdata镜像

烧写userdata镜像： userdata.img (仅支持fit启动方式)

```shell
./edge flash -d
```

#### 查看烧写帮助

查看支持的烧写参数：

```shell
./edge flash -h
```

### Windows主机烧写镜像

1. 双击打开tools\RKDevTool_Release_v2.84目录下的RKDevTool.exe。

2. 在工具的空白处点击右键，选择弹出菜单的"导入配置"，如下图所示：

   ![flash](./resources/Flash_cfg.png)

<center>图2-1：导入配置</center>

3. 选择要烧写的分区配置

   - config-extlinx.cfg：extlinux启动方式

   - config-fit.cfg: fit启动方式

2. 确认开发板已经进入loader或者maskrom烧写模式。

3. 打勾选择需要烧写的镜像。

   ***注意：Loader和Parmeter选项建议打勾选择，其他选项根据需要打勾选择。***

4. 点击“执行”按钮，开始烧写固件（如图2-1所示）。

   ![flash](./resources/Flash_fit.png)

<center>图2-2：烧写固件</center>

## 串口调试

### 串口连接

用USB线连接主机的USB host口和开发板的调试口(通常开发板上的调试口边上有标有类似"DEBUG" 或 “UART TO USB”的丝印)。

### Windows主机调试

#### 获取端口号

打开设备管理器获取调试串口的端口号，如图3-1所示：

![Debug_Port](./resources/Debug_port.png)

<center> 图3-1：获取调试端口号</center>

#### 配置调试串口信息

打开串口工具SecureCRT, 点击"快速连接"按钮，打开调试串口配置界面如图3-2和图3-3所示：

1. Port: 选择设备管理器显示的端口号
2. 波特率： 1500000
3. 禁止流控：不勾选RTS/CTS

![Debug_SecureCRT](./resources/Debug_SecureCRT.png)

<center>图3-2：SecureCRT</center>

![Debug_Config](./resources/Debug_config.png)

<center>图3-3：配置调试串口信息</center>

### Linux主机调试

#### 安装minicom

```shell
sudo apt -y install minicom
```

#### 配置调试窗口信息

按如下步骤，配置保存串口信息（此步骤执行一次即可）：

1. 执行如下命令打开minicom

   ```shell
   sudo minicom -s
   ```

2. 进入串口设置界面：输入CTRL-A Z

   ```shell
   +-------------------------------+
   | Filenames and paths           |
   | File transfer protocols       |
   | Serial port setup             |
   | Modem and dialing             |
   | Screen and keyboard           |
   | Save setup as dfl             |
   | Save setup as                 |
   | Exit                          |
   +-------------------------------+
   ```

3. 端口设置：选择"Serial port setup"

4. 设置串口设备： 输入"A"，填入"/dev/ttyUSB0", 然后按回车确定

5. 禁止流控：输入“F”，按回车确定

6. 设置波特率：输入“E”，再输入“A”直到显示"Current 1500000 8N1", 然后按回车确认

7. 配置完成后，界面显示

   ```shell
   +--------------------------------------------------------+
   | A -        Serial Device          : /dev/ttyUSB0       |
   | B -        Lockfile Location      : /var/lock          |
   | C -        Callin Program         :                    |
   | D -        Callout Porgram        :                    |
   | E -        Bps/Par/Bits           : 1500000 8N1        |
   | F -        Hardware Flow Control  : No                 |
   | G -        Software Flow Control  : No                 |
   +--------------------------------------------------------+
   ```

8. 退出端口设置：按回车

9. 保存配置： 选择"Save setup as dfl"

10. 退出设置：选择"Exit"

#### 运行minicom

```shell
sudo minicom
```

## SD卡启动

### 制作SD启动卡

1. 准备一张SD卡（容量不小于16G），通过SD卡读卡器插到windows电脑下USB Host口。

2. 双机打开tools\SDDiskTool_v1.69\SD_Firmware_Tool.exe，如图4-1所示：

   按界面提示选择磁盘设备、功能模式和update.img的路径，然后点击“开始创建”按钮，开始制作SD卡。

   ![SDDisk](./resources/SDDisk.png)

<center>图4-1：SDDisk界面</center>

### 从SD卡启动

将SD卡从RK3588开发板的SD卡槽重启开机启动，即可从SD卡启动。

## 更多文档

1. Debian系统软件包： docs/edge/debian
2. docker文档：docs/edge/docker
3. mpp文档：docs/edge/mpp
4. rga文档：docs/edge/rga
5. rknn文档：docs/edge/rknn
6. python-sdk文档：docs/edge/python-sdk
7. ros2文档：docs/edge/ros2
