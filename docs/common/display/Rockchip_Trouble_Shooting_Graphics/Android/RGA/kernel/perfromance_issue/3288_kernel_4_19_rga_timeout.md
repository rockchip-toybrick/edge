---
typora-copy-images-to: image
---

# 3288-4.19内核-RGA出现TimeOut问题

文件标识：RK-PC-YF-0002

发布版本：V1.0.0

日期：2020-06-18

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

本文主要对 **3288 4.19内核 RGA出现TimeOut问题** 进行说明，整理调试流程，整理相关客户Redmine，提供相关工程师调试参考。

**读者对象**

本文档主要适用一下工程师：

技术支持工程师

软件开发工程师

**修订记录**

| 日期       | 版本 | 作者 | 修订说明 |
| ---------- | ---- | ---- | -------- |
| 2020-06-18 | V1.0 | 李煌 | 初始版本 |

**目录**

------

[TOC]

------

**关键词：RGA，3288，TimeOut，内核4.19**

## 平台版本

​	适用平台：

| 芯片平台 | Android 版本 |
| -------- | ------------ |
| RK3288   | Android 10.0 |



## 问题描述

3288上调用RGA的所有场景卡顿，查看dmesg有RGA TimeOut相关打印。尝试使用RGA Demo进行测试，同样存在问题。且与DDR 无关，定频后仍有问题。

相关log：

```
[ 1315.728115] rga: Rga sync pid 1527 wait 1 task done timeout
```

## 问题分析

###分析现象

只有3288存在该问题，由于RGA device driver和其他平台一致，主要怀疑DDR 或者CLK 相关。

DDR 定频后使用Demo 测试仍存在TimeOut问题。

需要检查下RGA CLK相关

```
cat /d/clk/clk_summary｜grep rga
```

发现RGA clk 异常偏高，RGA 通常频率在200M ～ 400M 直接，再高极易不稳定。

![](./image/RGA_unormal_clk.png)

尝试手动修改RGA 频率：

```
echo 300000000 > sys/kerenl/debug/clk/aclk_rga/clk_rate
```

修改后，问题消失，确认为RGA CLK相关问题。

询问底层平台同事，3288 的cru 节点未设置默认频率，并且RGA 频率在dts中未声明，导致了异常的CLK。3288 平台需要在dts中声明RGA 的CLK。



## 解决方案

在 kernel 目录打以下补丁。

补丁文件 0001-ARM-dts-rockchip-rk3288-android-add-assigned-clocks-.patch

位于以下目录：[补丁目录](./patch/kernel/0001-ARM-dts-rockchip-rk3288-android-add-assigned-clocks-.patch)



###注意

如果打上patch 后发现开机出现VOP 异常（显示异色，断层）。实际原因为：

因为内核设置了RGA的时钟，设置时钟的时候，发现gpll更合适，会从cpll切到gpll，然后cpll下面子时钟的引用计数为0，cpll会被关闭一会，等VOP驱动加载后，cpll才会被开启，短暂下dclk是没有时钟的，所以显示出现了异常

请打上以下patch。

位于以下目录：[补丁目录](./patch/kernel/0001-drm-rockchip-add-more-clock-protect-for-loader-logo.patch)



## SDK commit

```
仓库：kernel
分支：rk29/develop-4.19
commit message
commit 2a2bd1e413e5a51bc77cc4cf580f54c7279d008a
Author: Putin Lee <putin.li@rock-chips.com>
Date:   Tue Feb 19 09:06:38 2019 +0800

    ARM: dts: rockchip: rk3288-android: add assigned clocks for rga

    Change-Id: Ice88b6a58b2f7c766b5cd42291bddbdbfb50cb2d
    Signed-off-by: Putin Lee <putin.li@rock-chips.com>
```



##相关Redmine

无




