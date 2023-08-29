本补丁适用于rk3399 7.1 sdk，通过gpu对输出进行梯形变换来达到投影设备梯形校正的效果，本补丁已对校正后内部的锯齿进行优化。

1、补丁说明
分别打上以下补丁
patchs/
├── device
│   └── rockchip
│       └── rk3399
│       │   └── 0001-Keystone-close-HWC-prop-for-keystone.patch
│       └── common
│           └── 0001-add-properties-for-keystone-correction.patch
└── frameworks
    └── native
        └── 0001-PATCH-Support-keystone-correction-function-for-10.0-.patch

/src/ 目录提供了源文件方便比对

2、使用说明

1）通过设置如下的属性来调整需要变换后的四边形的四个顶点
	例如
	persist.sys.keystone.lt=50,0		#代表四边形距离屏幕左上角的x,y距离
	persist.sys.keystone.lb=0,0			#代表四边形距离屏幕左下角的x,y距离
	persist.sys.keystone.rt=-50,0		#代表四边形距离屏幕右上角的x,y距离
	persist.sys.keystone.rb=0,0			#代表四边形距离屏幕右下角的x,y距离

	注意需按照笛卡尔直角坐标系来设置坐标的值，如persist.sys.keystone.lt=50,-50将会在屏幕内显示，而persist.sys.keystone.lt=50,50将会超出屏幕上方。

2）设置完成后再将persist.sys.keystone.update的属性值设置为1，将会使上述形状生效。

3）当属性设置梯形无变化时。可以先做如下两步检查：
	1、getprop vendor.hwc.compose_policy  查看属性值是否为0，需要将hwc关闭。
	2、getprop persist.sys.keystone.display.id  查看display id的值是否为当前需要做梯形的设备的id。若是不匹配，需setprop设置为对应的id。

4）针对某些角度过大，导致的画面不均匀现象，新开发了两个属性接口：
	1、setprop persist.sys.keystone.offset.x 0.0      范围为:-1.0~1.0
	2、setprop persist.sys.keystone.offset.y 0.0      范围为:-1.0~1.0

5）RockKeystone应用
	该应用提供了手动进行梯形校正的功能，并且后台service会根据gsensor来自动进行梯形校正。
	应用源代码见src/vendor/rockchip/common/apps/，客户可以自行进行二次开发。

3、版本更新
