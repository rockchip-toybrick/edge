# Overscan-功能说明

**关键词：** **overscan、缩放、过扫描**

发布版本：1.0

作者邮箱：bin.li@rock-chips.com

日期：2020.02

文件密级：公开

----

**前言**

本文主要对 overscan 功能 与 操作方法进行说明，并提供相关补丁，整理相关Redmine.

**读者对象**

本文档主要适用一下工程师：

技术支持工程师

软件开发工程师

**修订记录**

| 日期       | 版本 | 作者 | 修订说明 |
| ---------- | ---- | ---- | -------- |
| 2020-02-18 | V1.0 | 李斌 | 初始版本 |

---

[TOC]


## 功能描述：

Overscan功能是通过 **Vop后级缩放模块** 做过扫描处理，通俗的将就是通过Vop后端硬件对输出的显示内容进行一定比例的缩放处理。

缩放支持 Left \ Top \ Right \ Bottom 四个维度，硬件支持的缩放范围为 100% - 51%，而系统默认可设置的范围为 100 - 80%（如需要修改默认范围，需要额外打补丁）



## 平台支持情况：

| 芯片平台         | 支持硬件overscan |
| ---------------- | ---------------- |
| RK3399           | Y                |
| RK3288           | Y                |
| RK3368           | Y                |
| RK3328 / RK3228H | Y                |
| RK322x           | Y                |
| RK3326 / PX30    | N                |
| RK312x / RK3126c | N                |
| RK3188           | N                |
| RK3066           | N                |
| RK3036           | N                |




## 应用场景：

- **HDMI 过扫描问题处理：** 部分HDMI电视可能存在显示不全，或者显示内容延伸到屏幕之外，此时就可以通过调整overscan，将显示内容拉回屏幕显示区域内
- **显示屏幕被磨具遮挡：**显示屏幕被遮挡，导致部分内容显示不全，所以需要调整显示区域使所有屏幕内容可见
- **缩放屏幕需求**：客户希望可以任意调整屏幕缩放比例



## 使用方法：

#### 相关属性介绍：

```c++
//Android 8.1 及以下版本：
persist.sys.overscan.main //主屏
persist.sys.overscan.aux //副屏，通常为热插拔屏幕

//Android 9.0 及以上版本：
persist.vendor.overscan.main //主屏
persist.vendor.overscan.aux //副屏，通常为热插拔屏幕

//属性设置命令示例：
adb shell setprop persist.sys.overscan.main "overscan 80,80,80,80"
adb shell setprop persist.sys.overscan.aux "overscan 80,80,80,80"
    
//属性格式介绍：
"overscan 80,80,80,80"
//最后数值依次为 left,top,right,bottom,即屏幕左上右下缩放比例，缩放方向为向屏幕中心，上述设置情况
//解释为 left,top,right,bottom 向屏幕中心缩放 80%
    
//属性没有设置的话，默认值为100，即无overscan：
"overscan 100,100,100,100"
```

#### 注意事项：

1. 若设置的值超过限定范围，则取最接近目标值的限定值，即限定范围为100-80，则最高设置为100，最低设置为80.
2. 设置完属性值后，还需要触发系统界面刷新才能生效，可以在设置完属性值后，滑动窗口触发刷新，因为显示内容需要生效需要系统帧刷新驱动.
3. 若需要修改限定值的范围则需要额外打补丁.



#### 修改限定值范围补丁：

以下补丁适用平台包括：

​		RK3399 / RK3288 / RK3368 / RK3328   Android 7.1 及以上平台

```C++
// 请在 hardware/rockchip/hwcomposer 目录打上以下补丁：
diff --git a/drmdisplaycompositor.h b/drmdisplaycompositor.h
index 7037328..013131b 100755
--- a/drmdisplaycompositor.h
+++ b/drmdisplaycompositor.h
@@ -63,7 +63,7 @@
 #define RGA_MAX_WIDTH                   (4096)
 #define RGA_MAX_HEIGHT                  (2304)
 #define VOP_BW_PATH                    "/sys/class/devfreq/dmc/vop_bandwidth"
-#define OVERSCAN_MIN_VALUE              (80)
+#define OVERSCAN_MIN_VALUE              (51)
 #define OVERSCAN_MAX_VALUE              (100)
```



## 相关Redmine:

- [x] ​	Defect #241930：<https://redmine.rockchip.com.cn/issues/241930#change-2212972>：

  ​		需要自定义输出屏幕缩放比例功能，可以使用overscan满足需求





