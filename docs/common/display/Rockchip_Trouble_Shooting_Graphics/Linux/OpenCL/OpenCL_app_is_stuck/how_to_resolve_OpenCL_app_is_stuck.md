# 如何处理"OpenCL应用卡死"

文件标识：RK-PC-YF-0002

发布版本：V1.2.0

日期：2020-06-08

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

本文说明如何解决 "在 rk3399 Linux SDK 某些版本上出现的 客户的某些 OpenCL 应用执行一段时间后卡住" 的异常.

**读者对象**

本文档主要适用一下工程师：  
技术支持工程师
软件开发工程师

**修订记录**

| 日期       | 版本   | 作者 | 修订说明           |
| ---------- | ------ | ---- | ------------------ |
| 2020-06-08 | V1.0.0 | 陈真 | 初始版本           |

**目录**

------

[TOC]

------

**关键词：OpenCL 应用卡死**

## 平台版本：

适用平台 : RK3399 Linux  
适用 SDK 版本 : v2.0.8_20180929, v2.3.0_20191203, v2.3.1_20191223

## 问题描述：

使用 Midgard DDK r14 的 rk3399 Linux SDK 上, 客户的某些 OpenCL 应用执行一段时间后卡住, API 没有显式报错, 也没有 crash.

## 解决方案：

将设备中的 mali\_driver 升级到 r18 可以解决本异常.  
要升级 kernel 中的 mali device driver, 以及在 userspace 中使用基于 r18 的 libmali.

### 升级 mali device driver 到 r18

对于升级 mali device driver, 对不同 SDK 版本中的 kernel base, 需要使用不同的 patches.

对 SDK v2.0.8\_20180929(kernel/ 状态的 commit id 是 067752d), 需要使用目录 [on_SDK_v2.0.8_20180929/](./patch/kernel/on_SDK_v2.0.8_20180929) 下的 .patch 文件.  
对 SDK v2.3.0\_20191203 或 v2.3.1\_20191223(kernel/ 状态的 commit id 是 7379533), 需要使用目录 [on_SDK_v2.3.0_20191203/](./patch/kernel/on_SDK_v2.3.0_20191203) 下的 .patch 文件.

因为修改涉及 "arch/arm64/configs/rockchip\_linux\_defconfig", 所以具体编译前, 必须 先执行 "make ARCH=arm64 rockchip\_linux\_defconfig".  
然后, 编译 kernel, 烧写.

### 获取 r18 的 libmali

基于 DDK r18 的不同显示后端的 libmali 实例, 可以从如下 git 仓库找到 :

**仓库** : ssh://10.10.10.29:29418/linux/libmali  
**分支** : master  
**commit** : a39bc45  
**arm32  的 libmalis 所在的目录** : lib/arm-linux-gnueabihf/  
**arm64** : lib/aarch64-linux-gnu/

不同显示后端的 libmali 实例, 使用不同的文件名互相区别 :

| libmali 实例文件名                      | 显示后端     |
| --------------------------------------- | ------------ |
| libmali-midgard-t86x-r18p0.so	          | x11\_gbm     |
| libmali-midgard-t86x-r18p0-fbdev.so     | fbdev        |
| libmali-midgard-t86x-r18p0-x11-fbdev.so | x11\_fbdev   |
| libmali-midgard-t86x-r18p0-gbm.so       | gbm          |
| libmali-midgard-t86x-r18p0-wayland.so   | wayland\_gbm |

根据当前系统的显示后端, 选用对应的 libmali 实例.  
在 Defect #255026 中, 客户的显示后端是 x11\_gbm, 所以需要使用 libmali-midgard-t86x-r18p0.so .

### push r18 libmali 并使其生效

将待使用的 r18 的 libmali 实例 push 到 设备上 libMali.so 和 libmali.so 所在的目录, 并让 libMali.so 和 libmali.so 最终都指向这个 r18 libmali 实例.  
在 Defect #255026 中, 这样的目录是 /usr/lib/aarch64-linux-gnu/, 且有 "libmali.so -> libMali.so", 客户的 base 状态下还有 "libMali.so -> libmali-midgard-t86x-r14p0.so".  
此时, 要将 "libmali-midgard-t86x-r18p0.so" push 到设备的 /usr/lib/aarch64-linux-gnu/ 下, 然后删除符号链接 libMali.so, 再重新创建符号链接 libMali.so 并让它指向 libmali-midgard-t86x-r18p0.so .  
之后, 重启设备.

## 相关Redmine：

Defect #189171: https://redmine.rock-chips.com/issues/189171  
Defect #254482: https://redmine.rock-chips.com/issues/254482  
Defect #255026: https://redmine.rock-chips.com/issues/255026


