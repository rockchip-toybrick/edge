# GPU 报错导致Chromium-Wayland启动失败

文件标识：RK-PC-YF-0002

发布版本：V1.0.0

日期：2020-07-16

文件密级：□绝密   □秘密   □内部资料   ■公开

---

**免责声明**

本文档按“现状”提供，瑞芯微电子股份有限公司（“本公司”，下同）不对本文档的任何陈述、信息和内容的准确性、可靠性、完整性、适销性、特定目的性和非侵权性提供任何明示或暗示的声明或保证。本文档仅作为使用指导的参考。

由于产品版本升级或其他原因，本文档将可能在未经任何通知的情况下，不定期进行更新或修改。

**商标声明**

“Rockchip”、“瑞芯微”、“瑞芯”均为本公司的注册商标，归本公司所有。

本文档可能提及的其他所有注册商标或商标，由其各自拥有者所有。

**版权所有** **© 2019** **瑞芯微电子股份有限公司**

超越合理使用范畴，非经本公司书面许可，任何单位和个人不得擅自摘抄、复制本文档内容的部分或全部，并不得以任何形式传播。

瑞芯微电子股份有限公司

Rockchip Electronics Co., Ltd.

地址：     福建省福州市铜盘路软件园A区18号

网址：     [www.rock-chips.com](http://www.rock-chips.com)

客户服务电话： +86-4007-700-590

客户服务传真： +86-591-83951833

客户服务邮箱： [fae@rock-chips.com](mailto:fae@rock-chips.com)

----

**前言**

本文主要对 **GPU 报错导致Chromium-Wayland启动失败** 的问题进行说明，整理调试流程，整理相关客户Redmine，提供相关工程师调试参考。

**读者对象**

本文档主要适用一下工程师：

技术支持工程师

软件开发工程师

**修订记录**

| 日期       | 版本 | 作者 | 修订说明 |
| ---------- | ---- | ---- | -------- |
| 2020-07-17 | V1.0 | 李煌 | 初始版本 |

**目录**

------

[TOC]

------

**关键词：Mali，Chromium，Wayland，GL_INVALID_FRAMEBUFFER_OPERATION**

## 平台版本

​	适用平台：

| 芯片平台             | Linux 后端 |
| -------------------- | ---------- |
| RK3288/RK3399/RK3326 | Wayland    |



## 问题描述

启动Chromium 出现GPU报错，且无法正常启动。

相关log：

```
[573:573:0703/143353.020636:ERROR:gl_image_egl.cc(37)] Error creating EGLImage: EGL_BAD_PARAMETER
[573:573:0703/143353.020939:ERROR:gpu_memory_buffer_factory_native_pixmap.cc(43)] Failed to create GLImage 508x1260, BGRA_8888
[573:573:0703/143353.021267:ERROR:shared_image_backing_factory_gl_texture.cc(996)] Failed to create image.
[573:573:0703/143353.021486:ERROR:shared_image_factory.cc(422)] CreateSharedImage: could not create backing.
[573:573:0703/143353.026533:ERROR:shared_image_factory.cc(228)] DestroySharedImage: Could not find shared image mailbox
[573:573:0703/143353.047241:ERROR:buffer_manager.cc(488)] [.DisplayCompositor]GL ERROR :GL_INVALID_FRAMEBUFFER_OPERATION : glBufferData: <- error from previous GL command
```



## 问题分析

这里有两个问题。

(1)GL Error：GL_INVALID_FRAMEBUFFER_OPERATION

实际是驱动BUG，glPolygonOffset 函数调用报错GL_INVALID_FRAMEBUFFER_OPERATION导致。

修复代码详见内部提交：

Midgard r14:a79caef8f5df9db3a4dcede1f163d679bd864dff

Midgard r18:5b46be0e001609ff9ae6eaa215168cb805543d94

Bifrost:3464030602669e60f36a111fdd6b2a3377aaa7f6

(2)Failed to create GLImage

Mali so 未支持dmabuf import 扩展导致。部分应用或者框架需求eglGetDisplay使用dmabuf improt 扩展来创建display，创建display失败后进而导致创建Image 失败。该扩展在GPU内部已经修改并支持。

相关代码详见内部提交：

Midgard r14:00e323ea2752e3b6b6ffa317d3ddb8e7acb3d184

Midgard r18:07ffd4cf95ed511b4d131677e3bc298e6bbb6412

Bifrost:110fadf446f3c969a1ba634588674ea9ab875c9e

## 解决方案

###获取最新的libmali

在SDK中位于externel/libmali 目录。

仓库 : ssh://10.10.10.29:29418/linux/libmali
分支 : master
arm32  的 libmalis 所在的目录 : lib/arm-linux-gnueabihf/
arm64 : lib/aarch64-linux-gnu/

不同显示后端的 libmali 实例, 使用不同的文件名互相区别 :

| libmali 实例文件名                      | 显示后端     |
| --------------------------------------- | ------------ |
| libmali-midgard-t86x-r18p0.so           | x11\_gbm     |
| libmali-midgard-t86x-r18p0-fbdev.so     | fbdev        |
| libmali-midgard-t86x-r18p0-x11-fbdev.so | x11\_fbdev   |
| libmali-midgard-t86x-r18p0-gbm.so       | gbm          |
| libmali-midgard-t86x-r18p0-wayland.so   | wayland\_gbm |



### 手动集成

将待使用的 libmali 实例 push 到 设备上 libMali.so 和 libmali.so 所在的目录(usr/lib), 并让 libMali.so 和 libmali.so 软链接最终都指向这个libmali 实例.

之后, 重启设备验证.

## SDK commit

RK3326：

```
仓库：external/libmali
分支：master
commit message
commit 947e839a430b883ce1335af42823d43a9e6dde6e
Author: Li Huang <putin.li@rock-chips.com>
Date:   Fri Jul 10 15:05:48 2020 +0800

    rk3326: Update Wayland & Gbm
        1.Updata header file
        2.Fixup glPolygonOffset report error: GL_INVALID_FRAMEBUFFER_OPERATION
        3.and so on

    Signed-off-by: Li Huang <putin.li@rock-chips.com>
    Change-Id: I41a9a32ad333b53b213eba13742ac7be9a1c6b36
```



RK3399/RK3288 - R14:

```
仓库：external/libmali
分支：master
commit message
commit 5faaeca1847a886e377add86d48027e3f859f61b
Author: Li Huang <putin.li@rock-chips.com>
Date:   Mon Jul 15 20:22:47 2019 +0800

    libmali: update midgard

    1. Add EGL_EXT_image_dma_buf_import for x11 && wayland
    2. Fixup glPolygonOffset report error: GL_INVALID_FRAMEBUFFER_OPERATION

    Change-Id: I1747c0004ddd06218cbb6628f55d206c8ad9d85c
    Signed-off-by: Li Huang <putin.li@rock-chips.com>
```



RK3399/RK3288 - R18



## 相关Redmine

https://redmine.rock-chips.com/issues/259237#change-2410623




