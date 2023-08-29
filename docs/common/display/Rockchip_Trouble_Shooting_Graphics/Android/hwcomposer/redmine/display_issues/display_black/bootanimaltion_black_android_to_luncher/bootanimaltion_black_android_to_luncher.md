# Android动画至Luncher存在黑屏

**关键词：** **开机动画、黑屏、闪屏**

发布版本：1.0

作者邮箱：bin.li@rock-chips.com

日期：2020.02

文件密级：公开

----

**前言**

本文主要以 Defect #230712 问题为例，对 Android动画至Luncher存在黑屏问题进行分析，并提供相关补丁，整理相关Redmine.

**读者对象**

本文档主要适用一下工程师：

技术支持工程师

软件开发工程师

**修订记录**

| 日期      | 版本 | 作者 | 修订说明 |
| --------- | ---- | ---- | -------- |
| 2020-04-2 | V1.0 | 李斌 | 初始版本 |

---

[TOC]


## 问题描述：

升级后第一次启动没有问题,reset后重启进入系统会出现闪开机动画的现象，现象出现在第二次第三次开机,再后面的开机不会在出现。

异常情况：

开机logo -> 开机动画 -> android正在启动 -> (闪开机动画) -> 进入安卓桌面

正常情况：

开机logo -> 开机动画 -> android正在启动 -> 进入安卓桌面



## 问题分析：

正常的开机动画流程：

kernel logo -> Android 开机动画 -> android正在启动 -> 进入安卓桌面



异常的开机动画流程：

kernel logo -> Android 开机动画 -> android正在启动 -> **闪 Android 开机动画 或 黑屏 （异常）** -> 进入安卓桌面



**闪 Android 开机动画 或 黑屏 （异常）** 这个现象其实是系统显示了之前的残留数据，残留数据为之前显示的开机动画画面，那么就显示开机动画，若无画面，则显示黑屏。



发生这样问题的原因是以为，按理论上我们的过渡动画是覆盖在屏幕最顶层，等底层的系统UI 图像送显之后，再关闭过渡动画，则顺滑的切换场景画面，而出现该问题的原因就是因为，底层系统UI画面还没有准备完成，就关闭了过渡动画，这时候系统无内容显示，也就是真正的系统UI图像还没有正确送显，就直接导致显示了之前的残留数据，进而导致问题。



而造成这个问题的原因，可能是因为在该场景，系统负荷比较大，导致相关的处理没有及时完成，相关的显示内容没有及时渲染导致问题。



## 解决方法：

延迟开机动画关闭的时间，当然若问题仅在恢复出厂设置或者重烧固件的条件下才出现，可以加入persit 属性去区分场景:

```diff
diff --git a/services/core/java/com/android/server/wm/WindowManagerService.java b/services/core/java/com/android/server/wm/WindowManagerService.java
old mode 100644
new mode 100755
index 5ac25522ad5..532c9a643e9
--- a/services/core/java/com/android/server/wm/WindowManagerService.java
+++ b/services/core/java/com/android/server/wm/WindowManagerService.java
@@ -3523,33 +3523,7 @@ public class WindowManagerService extends IWindowManager.Stub
                 return;
             }
 
-            if (!mBootAnimationStopped) {
-                Trace.asyncTraceBegin(TRACE_TAG_WINDOW_MANAGER, "Stop bootanim", 0);
-                // stop boot animation
-                // formerly we would just kill the process, but we now ask it to exit so it
-                // can choose where to stop the animation.
-                SystemProperties.set("service.bootanim.exit", "1");
-                mBootAnimationStopped = true;
-            }
-
-            if (!mForceDisplayEnabled && !checkBootAnimationCompleteLocked()) {
-                if (DEBUG_BOOT) Slog.i(TAG_WM, "performEnableScreen: Waiting for anim complete");
-                return;
-            }
 
-            try {
-                IBinder surfaceFlinger = ServiceManager.getService("SurfaceFlinger");
-                if (surfaceFlinger != null) {
-                    Slog.i(TAG_WM, "******* TELLING SURFACE FLINGER WE ARE BOOTED!");
-                    Parcel data = Parcel.obtain();
-                    data.writeInterfaceToken("android.ui.ISurfaceComposer");
-                    surfaceFlinger.transact(IBinder.FIRST_CALL_TRANSACTION, // BOOT_FINISHED
-                            data, null, 0);
-                    data.recycle();
-                }
-            } catch (RemoteException ex) {
-                Slog.e(TAG_WM, "Boot completed: SurfaceFlinger is dead!");
-            }
 
             EventLog.writeEvent(EventLogTags.WM_BOOT_ANIMATION_DONE, SystemClock.uptimeMillis());
             Trace.asyncTraceEnd(TRACE_TAG_WINDOW_MANAGER, "Stop bootanim", 0);
@@ -3566,6 +3540,42 @@ public class WindowManagerService extends IWindowManager.Stub
         }
 
         mPolicy.enableScreenAfterBoot();
+        Slog.d(TAG_WM,"caijq delay boot animation exit ....");
+        try{
+            Thread.sleep(4000);
+            }catch(InterruptedException  e)
+        {
+            Slog.e(TAG_WM,"Sleep exception:",e);
+        }
+        if (!mBootAnimationStopped) {
+            Trace.asyncTraceBegin(TRACE_TAG_WINDOW_MANAGER, "Stop bootanim", 0);
+            // stop boot animation
+            // formerly we would just kill the process, but we now ask it to exit so it
+            // can choose where to stop the animation.
+            SystemProperties.set("service.bootanim.exit", "1");
+            mBootAnimationStopped = true;
+        }
+        
+        if (!mForceDisplayEnabled && !checkBootAnimationCompleteLocked()) {
+            if (DEBUG_BOOT) Slog.i(TAG_WM, "performEnableScreen: Waiting for anim complete");
+            return;
+        }
+        
+        try {
+            IBinder surfaceFlinger = ServiceManager.getService("SurfaceFlinger");
+            if (surfaceFlinger != null) {
+                Slog.i(TAG_WM, "******* TELLING SURFACE FLINGER WE ARE BOOTED!");
+                Parcel data = Parcel.obtain();
+                data.writeInterfaceToken("android.ui.ISurfaceComposer");
+                surfaceFlinger.transact(IBinder.FIRST_CALL_TRANSACTION, // BOOT_FINISHED
+                        data, null, 0);
+                data.recycle();
+            }
+        } catch (RemoteException ex) {
+            Slog.e(TAG_WM, "Boot completed: SurfaceFlinger is dead!");
+        }
+
+        
 
         // Make sure the last requested orientation has been applied.
         updateRotationUnchecked(false, false);
```



## 补丁说明：

附件补丁打在：frameworks/base/services
请测试效果，自行调试：
Thread.sleep(4000); 这句里面 4000 单位是毫秒，比如2000 就可以的话，使用2000，不够的话，再加大。

## 相关Redmine:

- Defect #230712:https://redmine.rockchip.com.cn/issues/230712

  "android正在启动"结束后闪过开机动画

- Support #246319: https://redmine.rockchip.com.cn/issues/246319#change-2268559

  好德芯 rk3399_Android_Pie_release_20190220 开机动画闪烁





