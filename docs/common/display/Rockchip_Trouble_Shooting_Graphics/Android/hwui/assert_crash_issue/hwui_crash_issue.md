---
typora-copy-images-to: image
---

# HWUI crash 相关问题

文件标识：RK-PC-YF-0002

发布版本：V1.0.0

日期：2020-06-010

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

本文主要对 **HWUI crash 相关问题** 进行说明，整理调试流程，整理相关客户Redmine，提供相关工程师调试参考。

**读者对象**

本文档主要适用一下工程师：

技术支持工程师

软件开发工程师

**修订记录**

| 日期       | 版本 | 作者       | 修订说明 |
| ---------- | ---- | ---------- | -------- |
| 2020-06-10 | V1.0 | 李煌，陈真 | 初始版本 |

**目录**

------

[TOC]

------

**关键词：HWUI, Toast，fd泄漏**

## 平台版本

​	适用平台：

| 芯片平台      | Android 版本         |
| ------------- | -------------------- |
| RK3399        | Android 7.1 or above |
| RK3368        | Android 7.1 or above |
| RK3288        | Android 6.0 or above |
| RK3326 / PX30 | Android 8.1 or above |
| RK3328        | Android 8.1 or above |



## 问题描述

APK 存在crash、闪退等现象。查看logcat ，能发现crash log，最终堆栈指向libhwui。

## 问题分析

HWUI crash 有很多原因，这里按异常类型分类描述。

### 因 "BufferQueue has been abandoned" 导致的 RenderThread crash

####Toast 异常

最直接看log 可以看到crash log在libhwui 中

```
 05-08 20:23:21.734 25963 25963 F DEBUG   : backtrace:
05-08 20:23:21.734 25963 25963 F DEBUG   :     #00 pc 000000000001d748  /system/lib64/libc.so (abort+120)
05-08 20:23:21.734 25963 25963 F DEBUG   :     #01 pc 0000000000007f08  /system/lib64/liblog.so (__android_log_assert+296)
05-08 20:23:21.734 25963 25963 F DEBUG   :     #02 pc 000000000004650c  /system/lib64/libhwui.so (android::uirenderer::debug::GlesErrorCheckWrapper::assertNoErrors(char const*)+384)
05-08 20:23:21.735 25963 25963 F DEBUG   :     #03 pc 000000000007f4dc  /system/lib64/libhwui.so (android::uirenderer::BakedOpRenderer::clearColorBuffer(android::uirenderer::Rect const&)+140)
05-08 20:23:21.735 25963 25963 F DEBUG   :     #04 pc 000000000007f774  /system/lib64/libhwui.so (android::uirenderer::BakedOpRenderer::startFrame(unsigned int, unsigned int, android::uirenderer::Rect const&)+132)
05-08 20:23:21.735 25963 25963 F DEBUG   :     #05 pc 0000000000069a48  /system/lib64/libhwui.so (_ZN7android10uirenderer12FrameBuilder14replayBakedOpsINS0_17BakedOpDispatcherENS0_15BakedOpRendererEEEvRT0_+800)
05-08 20:23:21.736 25963 25963 F DEBUG   :     #06 pc 0000000000069668  /system/lib64/libhwui.so (android::uirenderer::renderthread::OpenGLPipeline::draw(android::uirenderer::renderthread::Frame const&, SkRect const&, SkRect const&, android::uirenderer::FrameBuilder::LightGeometry const&, android::uirenderer::LayerUpdateQueue*, android::uirenderer::Rect const&, bool, bool, android::uirenderer::BakedOpRenderer::LightInfo const&, std::__1::vector<android::sp<android::uirenderer::RenderNode>, std::__1::allocator<android::sp<android::uirenderer::Render
05-08 20:23:21.736 25963 25963 F DEBUG   :     #07 pc 000000000006780c  /system/lib64/libhwui.so (android::uirenderer::renderthread::CanvasContext::draw()+184)
05-08 20:23:21.737 25963 25963 F DEBUG   :     #08 pc 000000000006b47c  /system/lib64/libhwui.so (android::uirenderer::renderthread::DrawFrameTask::run()+180)
05-08 20:23:21.737 25963 25963 F DEBUG   :     #09 pc 0000000000072a48  /system/lib64/libhwui.so (android::uirenderer::renderthread::RenderThread::threadLoop()+336)
05-08 20:23:21.737 25963 25963 F DEBUG   :     #10 pc 0000000000011478  /system/lib64/libutils.so (android::Thread::_threadLoop(void*)+280)
05-08 20:23:21.737 25963 25963 F DEBUG   :     #11 pc 00000000000a986c  /system/lib64/libandroid_runtime.so (android::AndroidRuntime::javaThreadShell(void*)+140)
05-08 20:23:21.737 25963 25963 F DEBUG   :     #12 pc 0000000000067d40  /system/lib64/libc.so (__pthread_start(void*)+36)
05-08 20:23:21.737 25963 25963 F DEBUG   :     #13 pc 000000000001ebd8  /system/lib64/libc.so (__start_thread+68)
```

能看到是由于某个GL API 报错被HWUI assert判断crash的，这里往log 前面看，能找到以下log，指向Toast 组件

```
05-08 20:23:15.926   239   445 E BufferQueueProducer: [Toast#0] dequeueBuffer: BufferQueue has been abandoned
```

该问题由于Toast 组件销毁时，没有和renderthread 渲染进程做一个同步，导致Surface 销毁时，渲染的进程还在dequeue。

#### 其他

典型log 如下

```
F libc    : Fatal signal 6 (SIGABRT), code -6 in tid 1915 (RenderThread), pid 1898 (d.soundrecorder)
F DEBUG   : *** *** *** *** *** *** *** *** *** *** *** *** *** *** *** ***
...
F DEBUG   : pid: 1898, tid: 1915, name: RenderThread  >>> com.android.soundrecorder <<<
F DEBUG   : signal 6 (SIGABRT), code -6 (SI_TKILL), fault addr --------
F DEBUG   : Abort message: 'Failed to set damage region on surface 0xeb990de8, error=EGL_BAD_ACCESS'
...
F DEBUG   : backtrace:
F DEBUG   :     #00 pc 0001a3be  /system/lib/libc.so (abort+63)
F DEBUG   :     #01 pc 0000655d  /system/lib/liblog.so (__android_log_assert+156)
F DEBUG   :     #02 pc 0004d12b  /system/lib/libhwui.so (android::uirenderer::renderthread::EglManager::damageFrame(android::uirenderer::renderthread::Frame const&, SkRect const&)+98)
F DEBUG   :     #03 pc 0004abd1  /system/lib/libhwui.so (android::uirenderer::renderthread::OpenGLPipeline::draw(android::uirenderer::renderthread::Frame const&, SkRect const&, SkRect const&, android::uirenderer::FrameBuilder::LightGeometry const&, android::uirenderer::LayerUpdateQueue*, android::uirenderer::Rect const&, bool, bool, android::uirenderer::BakedOpRenderer::LightInfo const&, std::__1::vector<android::sp<android::uirenderer::RenderNode>, std::__1::allocator<android::sp<android::uirenderer::RenderNode>>> co
F DEBUG   :     #04 pc 0004965d  /system/lib/libhwui.so (android::uirenderer::renderthread::CanvasContext::draw()+148)
F DEBUG   :     #05 pc 0004c217  /system/lib/libhwui.so (android::uirenderer::renderthread::DrawFrameTask::run()+134)
F DEBUG   :     #06 pc 0005101b  /system/lib/libhwui.so (android::uirenderer::renderthread::RenderThread::threadLoop()+178)
F DEBUG   :     #07 pc 0000d1c9  /system/lib/libutils.so (android::Thread::_threadLoop(void*)+144)
F DEBUG   :     #08 pc 0006dcd9  /system/lib/libandroid_runtime.so (android::AndroidRuntime::javaThreadShell(void*)+80)
F DEBUG   :     #09 pc 0004751f  /system/lib/libc.so (__pthread_start(void*)+22)
F DEBUG   :     #10 pc 0001af8d  /system/lib/libc.so (__start_thread+32)
```

app 进程的 RenderThread 遇到某个 GLES/EGL 调用失败, （可能出现在 libhwui 对多个 OpenGLES/EGL APIs 的各处调用中.场景不定）认为是 fatal, crash;而 crash 之前有 "BufferQueue has been abandoned" 的 log.该异常多出现在 Android 7.1 和 8.1 。



### fd 泄漏

#### log 分析

最直接能够看到crash 堆栈指向libhwui：

```
06-06 00:55:48.521 F/DEBUG   ( 5852): backtrace:
06-06 00:55:48.521 F/DEBUG   ( 5852):     #00 pc 000000000001d788  /system/lib64/libc.so (abort+120)
06-06 00:55:48.521 F/DEBUG   ( 5852):     #01 pc 0000000000007f08  /system/lib64/liblog.so (__android_log_assert+296)
06-06 00:55:48.521 F/DEBUG   ( 5852):     #02 pc 000000000006c5e4  /system/lib64/libhwui.so (android::uirenderer::renderthread::EglManager::beginFrame(void*)+324)
06-06 00:55:48.521 F/DEBUG   ( 5852):     #03 pc 00000000000677a4  /system/lib64/libhwui.so (android::uirenderer::renderthread::CanvasContext::draw()+80)
06-06 00:55:48.521 F/DEBUG   ( 5852):     #04 pc 0000000000067e6c  /system/lib64/libhwui.so (android::uirenderer::renderthread::CanvasContext::prepareAndDraw(android::uirenderer::RenderNode*)+192)
06-06 00:55:48.521 F/DEBUG   ( 5852):     #05 pc 00000000000727e4  /system/lib64/libhwui.so (android::uirenderer::renderthread::RenderThread::dispatchFrameCallbacks()+200)
06-06 00:55:48.521 F/DEBUG   ( 5852):     #06 pc 0000000000072a68  /system/lib64/libhwui.so (android::uirenderer::renderthread::RenderThread::threadLoop()+336)
06-06 00:55:48.521 F/DEBUG   ( 5852):     #07 pc 0000000000011478  /system/lib64/libutils.so (android::Thread::_threadLoop(void*)+280)
06-06 00:55:48.521 F/DEBUG   ( 5852):     #08 pc 00000000000a986c  /system/lib64/libandroid_runtime.so (android::AndroidRuntime::javaThreadShell(void*)+140)
06-06 00:55:48.521 F/DEBUG   ( 5852):     #09 pc 0000000000067d80  /system/lib64/libc.so (__pthread_start(void*)+36)
06-06 00:55:48.521 F/DEBUG   ( 5852):     #10 pc 000000000001ec18  /system/lib64/libc.so (__start_thread+68)
```

 往前分析log，能看到以下异常log ：

```
06-06 00:55:47.780 E/Parcel  (21148): fcntl(F_DUPFD_CLOEXEC) failed in Parcel::read, i is 0, fds[i] is 170, fd_count is 1, error: Unknown error 2147483647
```

最常见的是APP存在fd 泄漏，耗尽了所有fd资源，这里会导致dequeue 等操作失败，接着导致GLES/EGL api失败，运行在 hwui 中的 RenderThread 认为是 fatal, 最终导致crash。

某些情况下，存在fd 泄漏情况下还会有以下显式的异常log 

```
06-06 00:54:04.371 W/HwcComposer(  279): failed to dup fence 1010
```

```
12468 13459E IMemory : cannot dup fd=1015, size=1384448,err=0 (Too many open files) 
```



## 解决方案

### 因 "BufferQueue has been abandoned" 导致的 RenderThread crash

#### Toast 异常

在 framworks/base 目录打以下补丁，仅试用于Android 8.1， 9.0上已经存在该修改。

补丁文件 0001-Fix-toast-lifetime.patch

位于以下目录：[补丁目录](./patch/frameworks/base)

#### 其他

对于大多数此类异常, 对应的补丁 0001-hwui-Workaround-a-fatal-case-reported-in-61-of-https.patch 来 workaround.

位于以下目录：[补丁目录](./patch/frameworks/base)

 

若 hwui 仍旧有出现在其他位置的 fatal, 可以尝试在

以下目录：[补丁目录](./patch/frameworks/base) 

使用如下 patch : 

​	0001-WORKAROUND-hwui-EglManager-swapBuffers-treat-EGL_BAD.patch 

​	0001-workaround-hwui-force-to-disable-GL_CHECKPOINT.patch



补充：Android 6.0也发现 该问题，补丁在以下目录：[补丁目录](./patch/frameworks/base)

​	0001-hwui-Workaround-a-fatal-case-in-Defect-261433.patch



### fd 泄漏

要求客户先使用 /proc/<pid>/fd 等接口, 排查 app 进程中是否存在 fd 泄漏的问题。

如果fd 指向 surfaceflinger 进程。则参照以下以下方法排查。

使用 monitor_fd.sh 脚本（抓取/proc/<pid>/fd 以及 dumpsys SurfaceFlinger 信息）运行在后台，进行复现烤机。需要根据复现时间调整脚本内抓取信息间隔。

脚本文件位于以下目录：[脚本目录](./script/monitor_fd.sh)

复现时，请检查sf_dump.txt 文件，主要关心 dumpsys Surfaceflinger 中 “Allocated buffers” 条目，是否有随着烤机存在不断增长的情况。通常是由于应用进程Surface 泄漏导致，这会导致surfaceflinger 进程下fence 以及dma_buf 的fd 不断增长。

后续需要有客户或者产品部同事自行跟进解决。

## SDK commit

### 因 "BufferQueue has been abandoned" 导致的 RenderThread crash

####Toast 异常

目前该patch 仅在9.0分支上存在。8.1 暂未合并。

```
仓库：frameworks/base
分支：aosp/rk33/mid/9.0/develop
commit message
commit 99742734e3b67cc48941e37db982c43aa843596c
Author: Robert Carr <racarr@google.com>
Date:   Wed Feb 28 18:06:10 2018 -0800

    Fix toast lifetime

    In NotificationManagerService#cancelToast we have been calling
    WindowManagerService#removeWindowToken with 'removeWindows'=true. This
    is allowing for Surface destruction without any sort of synchronization
    from the client. Before the call to removeWindowToken we are emitting
    a one-way hide call to the Toast client. As a solution to the lifetime
    issue we have the client callback to let us know it has processed the
    hide call (and thus stopped the ViewRoot). On the server side we also instate
    a timeout. This mirrors the app stop timeout. All codepaths I could find
    leading to this sort of situation where a client is still rendering
    in to a toast following the total duration expiring seem to indicate a hung
    client UI thread.

    Bug: 62536731
    Bug: 70530552
    Test: Manual. go/wm-smoke
    Change-Id: I89643b3c3a9fa42423b498c1bd3a422a7959aaaf
```

#### 其他

Android 7.1

```
仓库：frameworks/base
分支：aosp/rk33/mid/7.1/develop
commit 8b044c4f31ef60cd7d2c22c4048429f45d8c9f53
Author: Li Huang <putin.li@rock-chips.com>
Date:   Wed Oct 30 15:13:12 2019 +0800

    Remove eglSetDamageRegionKHR assert fatal.

    Change-Id: Iaf0ffd152a861d91b3c1c2cd22fc168e9120f2f9
    Signed-off-by: Li Huang <putin.li@rock-chips.com>
```

Android 8.1

```
仓库：frameworks/base
分支：aosp/rk33/mid/8.1/develop
commit message
commit 15de9c1a104c2f87f689e9a75c38a71adfbc5d10
Author: Zhen Chen <chenzhen@rock-chips.com>
Date:   Wed Mar 27 09:54:42 2019 +0800

    hwui: Workaround a fatal case reported in #61 of https://redmine.rockchip.com.cn/issues/189373

    Change-Id: Ie7b3eb3c0292985e814000cc125df893dbaf2c38
    Signed-off-by: Zhen Chen <chenzhen@rock-chips.com>
```

Android 9.0

```
仓库：frameworks/base
分支：aosp/rk33/mid/9.0/develop
commit 158888124ed8a0417f184649f002185bfdf6c0e9
Author: Li Huang <putin.li@rock-chips.com>
Date:   Wed Oct 30 15:15:20 2019 +0800

    Remove eglSetDamageRegionKHR assert fatal.

    Change-Id: I7dd7176bfe57fa6ebb81d2de1790934c67e727e6
    Signed-off-by: Li Huang <putin.li@rock-chips.com>
```



##相关Redmine

### 因 "BufferQueue has been abandoned" 导致的 RenderThread crash

Defect #227645：https://redmine.rock-chips.com/issues/227645 

Defect #223628：https://redmine.rock-chips.com/issues/223628 

Defect #225784：https://redmine.rock-chips.com/issues/225784 

Defect #220904：https://redmine.rock-chips.com/issues/220904 

Defect #209970：https://redmine.rock-chips.com/issues/209970 

Defect #207382：https://redmine.rock-chips.com/issues/207382 

Defect #189373：https://redmine.rock-chips.com/issues/189373 

Defect #261433：https://redmine.rock-chips.com/issues/261433

### fd 泄漏

Defect #201307：https://redmine.rock-chips.com/issues/201307 

Defect #236492：https://redmine.rock-chips.com/issues/236492 

Defect #255308:  https://redmine.rock-chips.com/issues/255308




