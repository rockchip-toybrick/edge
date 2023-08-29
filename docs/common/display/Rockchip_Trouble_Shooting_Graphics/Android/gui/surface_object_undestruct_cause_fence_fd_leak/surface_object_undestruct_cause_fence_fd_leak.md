---
typora-copy-images-to: image
---

# GUI-fd泄露-Surface未析构导致FenceFd泄露

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

本文以 **Defect #256905** 为例，对 **由于Surface对象未析构导致的FenceFd问题** 进行说明，整理调试流程，整理相关客户Redmine，提供相关工程师调试参考。

**读者对象**

本文档主要适用一下工程师：

技术支持工程师

软件开发工程师

**修订记录**

| 日期       | 版本 | 作者 | 修订说明 |
| ---------- | ---- | ---- | -------- |
| 2020-06-18 | V1.0 | 李斌 | 初始版本 |

**目录**

------

[TOC]

------

**关键词：GUI、Surface、Fence、fd泄露**

## 平台版本

​	适用平台：

| 芯片平台 | Android 版本 |
| -------- | ------------ |
| All      | All          |



## 问题描述

客户Monkey拷机过程中，发现系统显示异常，卡死，奔溃等现象。

客户平台设备为 RK3368 Android 8.1

## 问题分析

1. **分析 Defect #256905 2#提供日志：**

```shell
# 出现 Too many open files 报错，是典型的fd泄露问题
E/Fence   (  279): merge: (..)returned an error: Too many open files (-24)

# Fence fd 索引达到1023,进一步说明fd存在泄露问题，因为fd的最大值为1024
W/HwcComposer(  279): failed to dup fence 1018
W/HwcComposer(  279): failed to dup fence 1019
W/HwcComposer(  279): failed to dup fence 1023

# dup fd 失败导致Pvr driver 报错
E/Parcel  (21148): fcntl(F_DUPFD_CLOEXEC) failed in Parcel::read, i is 0, fds[i] is 170, fd_count is 1, error: Unknown error 2147483647
E/IMGSRV  (21148): android_ws.c:374: DequeueBufferWrapper: Failed to de-queue buffer (-22)
E/IMGSRV  (21148): generic_ws.c:350: KEGLGetDrawableParameters: Native window is invalid
E/IMGSRV  (21148): eglglue.c:2538: GLES3MakeCurrentGC: Invalid write drawable - what do we do?
E/IMGSRV  (21148): khronos_egl.c:6155: IMGeglMakeCurrent: unexpected error code

# 最终导致 Fatal 报错，系统出现crash
F/OpenGLRenderer(21148): Failed to make current on surface 0x7e479262c0, error=EGL_SUCCESS
F/libc    (21148): Fatal signal 6 (SIGABRT), code -6 in tid 21266 (RenderThread), pid 21148 (oid.m1.launcher)

# 结论：由fd泄露导致的系统crash问题
```

2. **确定 fd 泄露类型：**

   利用 Defect #256905 5# 提供脚本，复测拷机环境，检测SurfaceFlinger / system_server 进程的fd管理情况，若复现fd泄露的问题，可在输出文件内查看到对应fd的泄露类型，如下：

   ```shell
   # 查看 14# 提供日志  fd_overflow.rar 中的 sf_fd.txt 文件：
   
   Fri Jun 12 10:30:17 EDT 2020:--------------------------------------------------:1
   anon_inode:sync_fence 数目为 13 个
   
   Fri Jun 12 16:45:38 EDT 2020:--------------------------------------------------:76
   anon_inode:sync_fence 数目为 621 个
   
   # 可以看到 4小时内，sync_fence fd数目激增，那么很快就会达到1024个，造成系统异常
   ```

   故确定泄露的Fd类型为 Fence fd。

   脚本可从以下目录获取：[monitor_fd.sh](script)

3. **进一步确认Fence fd 泄露类型为 AcquireFence 还是 ReleaseFence**

   AcquireFence 由生产者创建（一般为GPU等生产数据的组件创建）

   ReleaseFence由消费者创建（一般为显示后端vop使用数据的组件）

   可以通过单独关闭 AcquireFence 与 ReleseFence 的方法对Fence fd的类型进行进一步收敛，因为如果系统都不创建Fence fd，那自然不存在泄露的问题。

   关闭 AcquireFence 方法与补丁：[Android-8.1-close-AcquireFence.patch](patch/frameworks/native)

   关闭 ReleaseFence 方法与补丁：[Android-8.1-close-releaseFence.patch](patch/hardware/rockchip/hwcomposer)

   进一步确认泄露的 fd 类型为 AcquireFence。

4. **确认具体AcquireFence泄露代码位置或数据结构位置：**

   即强制关闭某结构的 AcquireFence fd，则不存在fd泄露问题：

   最终确认只要关闭以下补丁的  mLastQueueBufferFence 结构后，fd则不再泄露，补丁如下：

   [Android-8.1-close-mLastQueueBufferFence.patch](patch/frameworks/native)

   ```c++
   // frameworks/native/libs/gui/include/gui/BufferQueueProducer.h
   // This saves the fence from the last queueBuffer, such that the
   // next queueBuffer call can throttle buffer production. The prior
   // queueBuffer's fence is not nessessarily available elsewhere,
   // since the previous buffer might have already been acquired.
   sp<Fence> mLastQueueBufferFence;
   
   // mLastQueueBufferFence 结构内部包含一个AcquireFence fd,该 fd 被close目前只有两种途径：
   // 1.下一帧queue行为会将上一帧的 mLastQueueBufferFence  close.
   // 2.整个 class BufferQueueProducer 实例被析构时，mLastQueueBufferFence 也会被close
   ```

   故怀疑可能由于 BufferQueueProducer 实例没有被析构导致 AcquireFence fd 泄露。

5. **提供 BufferQueueProducer 实例创建与析构调用的打印：**

   相关调用的补丁位置为：[Print-bufferQueueProducer_create_and_destruct_class.patch](patch/frameworks/native/)

   反馈日志分析：

   ```shell
   # 如 Defect #255308 16# 提供日志：
   Surface创建相关打印：
   D/BufferQueueProducer(  279): DEBUG_lb BufferQueueProducer name = unnamed-279-146  
   D/BufferQueueConsumer(  279): DEBUG_lb setConsumerName 2 name = unnamed-279-146  
   D/BufferQueueConsumer(  279): DEBUG_lb setConsumerName 2 name = com.mm.android.m1.l
   
   Surface析构相关打印：
   D/BufferQueueProducer(  279): DEBUG_lb ~BufferQueueProducer name = com.mm.android
   
   # 通过分析日志，可以查看到完整的日志中存在以下现象：
   对于 SurfaceView - com.mm.android.m1.launcher/com.mm.a Surface，只能找到创建的打印，未发现析构打印，则说明 SurfaceView 未调用析构函数，导致AcqureFence fd泄露。
   ```

   故问题定位为：**SurfaceView 未调用析构函数，导致AcqureFence fd泄露**

## 解决方案

此问题确定为客户播放库相关代码未按正常的析构程序将申请的相关实例析构，导致系统异常，fd泄露等问题，故此问题需要客户检查自己的代码库进行修改解决问题。

## SDK commit

此问题为客户代码逻辑的BUG，非RK sdk代码问题，故没有相关的SDK commit.

##相关Redmine

Defect #256905：https://redmine.rock-chips.com/issues/255308#change-2386256
