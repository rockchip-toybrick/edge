# RK3288 双屏拔插物理输入设备APP出现闪黑的问题

文件标识：RK-PC-YF-0002

发布版本：V1.0.0

日期：2020-06-30

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

本文主要对 **RK3288 双屏拔插USB 键盘部分APP出现闪黑的问题** 进行说明，整理调试流程，整理相关客户Redmine，提供相关工程师调试参考。

**读者对象**

本文档主要适用一下工程师：

技术支持工程师

软件开发工程师

**修订记录**

| 日期       | 版本 | 作者 | 修订说明 |
| ---------- | ---- | ---- | -------- |
| 2020-06-30 | V1.0 | 李煌 | 初始版本 |

**目录**

------

[TOC]

------

**关键词：RGA，3288，TimeOut，内核4.19**

## 平台版本

​	适用平台：

| 芯片平台 | Android 版本     |
| -------- | ---------------- |
| RK3288   | Android 8.1，9.0 |



## 问题描述

在双屏的情况，部分APP比如计算器，Setting等，拔插物理键盘或者蓝牙鼠标等物理输入设备，会出现闪黑的现象。

单屏经测试不存在该问题。

相关log：

```
06-28 04:13:26.945   418   490 I ActivityManager: Override config changes=30 {1.0 ?mcc?mnc [en_US] ldltr sw720dp w1280dp h648dp 240dpi lrg long land finger -keyb/v/h -nav/h winConfig={ mBounds=Rect(0, 0 - 1920, 1008) mAppBounds=Rect(0, 0 - 1920, 1008) mWindowingMode=fullscreen mActivityType=undefined} s.24} for displayId=0
```

bufferqueue 也能看到一些异常log：

```
06-28 04:13:16.453 1380 1400 W libEGL : EGLNativeWindowType 0x95f70008 disconnect failed
```



## 问题分析

#### 分析现象

询问董正勇后，跟SystemUI相关的应用有关，拔插物理输入设备时会调用应用的重绘，导致闪黑问题。



## 解决方案

在 framworks/base 目录打以下补丁，目前仅Android 8.1， 9.0已验证。

补丁文件 keyboard.patch

位于以下目录：[补丁目录](./patch/frameworks/base)



如果不能解决该问题，请找董正勇跟进处理。



## SDK commit

无



##相关Redmine

https://redmine.rock-chips.com/issues/258309#change-2401068

https://redmine.rock-chips.com/issues/244750




