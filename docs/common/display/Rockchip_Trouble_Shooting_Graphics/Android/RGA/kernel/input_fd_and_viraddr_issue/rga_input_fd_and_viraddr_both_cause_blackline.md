---
typora-copy-images-to: image
---

# 显示黑线-RGA驱动BUG导致RGA合成输出图像随机黑线

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

本文主要对 **RAG合成导致画面随机出现黑色直线问题** 进行说明，整理调试流程，整理相关客户Redmine，提供相关工程师调试参考。

**读者对象**

本文档主要适用一下工程师：

技术支持工程师

软件开发工程师

**修订记录**

| 日期       | 版本 | 作者   | 修订说明 |
| ---------- | ---- | ------ | -------- |
| 2020-07-09 | V1.0 | 林志雄 | 初始版本 |

**目录**

------

[TOC]

------

**关键词：RGA，黑线**

## 平台版本

​	适用平台：

| 芯片平台 | Android 版本 |
| -------- | ------------ |
| RK3128   | Android 7.1  |



## 问题描述

使用hwc合成时，进入某些APK场景，会有黑色直线问题，如下图：

![black_line](./image/blackline.png)



## 问题分析



### 分析现象

1. 该现象只在开启HWC合成时出现，关闭HWC使用GPU合成时没问题。因此怀疑是rga合成时引入的问题。

2. 从HWC处dump出来的layer，可以看到，源数据就带有黑线。 此时有点怀疑是应用draw出来的buffer有问题，但是GPU合成没问题，因此该猜测被推翻。 进而猜测 会不会是rga合成的时候，冲到了源数据的buffer。

3. 在HWC申请buffer的地方 ，将alloc的buffer 扩大至 1.2倍，补丁如下：

   [hwc申请buffer扩大至1.2倍](./patch/hardware/rochchip/hwcomposer/0001-Workaround-hwcClear-use-rgaClear-data-err.patch)

   该现象消失。 此补丁发给客户应急用，具体原因接着调查。

4. 跟进HWC代码后发现，HWC主要在hwcBlit 与 hwcClear的时候有使用ioctl调用RGA，其中，注释掉hwcClear的ioctl时发现 画面正常了。

5. 检查 hwcClear 调用RGA 的配置信息，发现其在配置源数据的时候，将fd与viraddr一同配置下来了。代码片段如下：

```
{    
    #if defined(TARGET_BOARD_PLATFORM_RK312X) || defined(TARGET_BOARD_PLATFORM_RK3328)
    #if defined(__arm64__) || defined(__aarch64__)
    RGA_set_src_vir_info(&Rga_Request, srchnd->share_fd, reinterpret_cast<long>(srchnd->base),  0, srcStride, srcHeight, srcFormat, 0);         //此处fd与 viraddr同时传入
    RGA_set_dst_vir_info(&Rga_Request, dstFd, reinterpret_cast<long>(DstHandle->base),  0, DstHandle->stride, dstHeight, &clip, dstFormat, 0);
    #else
...
    #endif
    rga_set_fds_offsets(&Rga_Request, 0, 0, 0, 0);
    #else
...
    #endif
}
```

6. 理论上RGA的 devicedriver应该检测到，如果有fd和viraddr同时传下来，优先使用fd 而忽略viraddr。
7. 因此这个属于RGA驱动BUG，最终在devicedriver处将该逻辑加上即可。



## 解决方案

在 kernel 目录打以下补丁。

补丁文件 0001-video-rockchip-rga-Fix-rgaColorFill-issue-when-both-.patch

位于以下目录：[修复RGA驱动同时传入fd与虚地址黑线问题](./patch/kernel/0001-video-rockchip-rga-Fix-rgaColorFill-issue-when-both-.patch)



## SDK commit

```
仓库：kernel
分支：rk29/develop-4.4
commit message
commit 4852ecf21c02db7602f1e63f8bde88c5b5de2eb9
Author: Zhixiong Lin <zhixiong.lin@rock-chips.com>
Date:   Wed Apr 15 15:08:53 2020 +0800

    video/rockchip/rga: Fix rgaColorFill issue when both input fd and vir_addr.
    
    Change-Id: Idee53715ded6941033f7cab8e6de70a0a6d27364
    Signed-off-by: Zhixiong Lin <zhixiong.lin@rock-chips.com>
```

```
仓库：kernel
分支：rk29/develop-4.19
commit message
commit 6e32dce0213faf87576ef4e8db55a0780b3cd426
Author: Zhixiong Lin <zhixiong.lin@rock-chips.com>
Date:   Wed Apr 15 15:08:53 2020 +0800

    video/rockchip/rga: Fix rgaColorFill issue when both input fd and vir_addr.
    
    Change-Id: Idee53715ded6941033f7cab8e6de70a0a6d27364
    Signed-off-by: Zhixiong Lin <zhixiong.lin@rock-chips.com>
```

## 相关Redmine

https://redmine.rock-chips.com/issues/239256


