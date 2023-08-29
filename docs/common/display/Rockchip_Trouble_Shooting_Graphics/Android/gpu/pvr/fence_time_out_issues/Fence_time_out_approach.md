# fence time out 相关问题处理方法

## 问题现象

通常表现为卡死，花屏等问题。内核可以在复现时kernel看到以下log：

3000ms 的Fence timeout：

```
fence timeout on [ffffffc02cfb0200] after 3010ms
```

## 处理办法

1、优先要求定频，GPU、DDR、CPU都定频进行测量，优先定中档频率进行实验测量，如果条件允许，可以针对其他频点进行测量。定频后测量板级逻辑电压和目前rk sdk对外release所使用的dts中定义的电压值是否一致，如差异较大或者测量的电压值存在异常波动，请优先建议客户调整dts，或者排查硬件电路是否存在异常。

2、定频后电压如无异常，则进行复现的实验操作，如果定频后无法复现，则可以判断为是变频策略或者部分频点电压等存在问题。这部分由产品部跟进确认。

3、如仍会复现，请尝试以下修改：

 PVR apphint：

   将”powervr.ini”文件push 到设备system/etc 以及vendor/etc 目录，重启尝试复现。

补丁目录：

```
Android/vendor/rockchip/common/PVR/powervr.ini
```

 Power always on：

   在kerenl 打上pvr_power_always_on.patch ,重新烧写固件验证。

补丁目录：

```
kernel/driver/staging/imgtec/patch/pvr_power_always_on.patch
```

4、如果客户Android 版本为6.0 及以下，请确认下客户DDK 版本

```
getprop | grep pvr
```

如果版本信息为1.35 及以下，请将以下目录的更新包丢给客户，直接使用压缩包内的push的脚本进行验证。

```
Android/vendor/rockchip/common/PVR/DDK_1.8_on_rk3368_6.0_v11.tar.gz
```

如果push 后可以成功解决客户问题。让客户按照压缩包内readme 进行集成。

如果push 后导致系统起不来，无法进入Android，请让客户打包上传所使用的kernel，并提供编译命令给gpu 组的人进行处理。

