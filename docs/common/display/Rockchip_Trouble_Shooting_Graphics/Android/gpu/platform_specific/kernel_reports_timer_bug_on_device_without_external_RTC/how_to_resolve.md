# 如何处理"Mali400_GPU-设备上没有外置RTC-kernel提示BUG,死机"

文件标识：RK-PC-YF-0002

发布版本：V1.2.0

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

本文说明如何解决 "RK3188(RKPX3) Android 5.1 上, 若设备没有外置 RTC, 可能遇到的 'kernel 卡死, 显式报告 timer 相关的 BUG'" 的异常.

**读者对象**

本文档主要适用以下工程师：  
技术支持工程师
软件开发工程师

**修订记录**

| 日期       | 版本   | 作者 | 修订说明           |
| ---------- | ------ | ---- | ------------------ |
| 2020-06-18 | V1.0.0 | 陈真 | 初始版本           |

**目录**

------

[TOC]

------

**关键词：RTC, kerne 死机, timer BUG**

## 平台版本：

适用平台 : RK3188(RKPX3) Android 5.1

## 问题描述：

RK3188(RKPX3) Android 5.1, 设备上没有外置RTC, 设备运行一段时间后会死机, 并有如下 log : 

```
[192152.062540] kernel BUG at kernel/timer.c:853!
[192152.062665] Unable to handle kernel NULL pointer dereference at virtual address 00000000
...
[192152.967428] [<c05b422c>] (_bug+0x1c/0x28) from [<c05f673c>] (add_timer+0x28/0x2c)
[192152.975164] [<c05f673c>] (add_timer+0x28/0x2c) from [<bf019df8>] (mali_control_timer_callback*+0x60/0x70 [mali])
[192152.985412] [<bf019df8>] (mali_control_timer_callback+0x60/0x70 [mali]) from [<c05f6480>] (run_timer_softirq+0x12c/0x3c0)
[192152.996486] [<c05f6480>] (run_timer_softirq+0x12c/0x3c0) from [<c05ef07c>] (_do_softirq+0xc8/0x25c)
[192153.005746] [<c05ef07c>] (_do_softirq+0xc8/0x25c) from [<c05ef6c4>] (irq_exit+0x98/0xa0)
[192153.014049] [<c05ef6c4>] (irq_exit+0x98/0xa0) from [<c05ab050>] (asm_do_IRQ+0x50/0xac)
[192153.022098] [<c05ab050>] (asm_do_IRQ+0x50/0xac) from [<c05b0ac8>] (__irq_svc+0x48/0xe0)
```

"mali_control_timer_callback" 是 Mali400 device driver 中的函数.

## 解决方案：

在 mali.ko 中加入 workaround 的 patch, 可解决本异常.  
这样的 mali.ko 被存放在 3188_5.1\_\_mali_ko_of_r1_for_defect_252934\_\_on_r5p0-01rel0.tar.gz 中.  
上述 .gz 文件在 目录 [patch/](./patch) 下.  
将其拷贝到 SDK 源码工程的 根目录, 然后执行 "tar xzvf 3188_5.1\_\_mali_ko_of_r1_for_defect_252934\_\_on_r5p0-01rel0.tar.gz", 其中的 mali.ko 将替换工程中的原始版本.   
之后, 编译 Android, 烧写固件, ...

## mali.ko 的源码和相关修改.

上述 mali.ko 的源码在如下分支 :  
**仓库** : ssh://git@10.10.10.59:8001/graphics/utgard/src_of_mali_so/mali_so.git  
**分支** : build_mali_ko_on_r5p0-01rel0_for_3188_5.1

workaround patch 对应上述分支中的如下 commit :  
**93d72ef** mali_control_timer_callback(): call _mali_osk_timer_mod() instead of _mali_osk_timer_add()

## 相关Redmine：

Defect #252934: https://redmine.rock-chips.com/issues/252934
