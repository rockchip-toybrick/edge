1）camera 分 dvp soc camera 和 mipi raw camera，
其中RK3066、RK3188、RK312x 只支持 dvp soc camera ；
RK3288 & RK3368 & RK3399 & RK3326 & RV110X 支持 dvp soc camera 和 mipi raw camera；

2）RK3066 & RK3188 camera支持情况，请参考相关 SDK 代码中已调试过的型号；
或者将RK312x or RK3288 中的相应型号代码自行移植过来；

3）RK312x camera支持情况，请参考《RK312x_Camera_User_Manual》；
或者将RK3066、RK3188 or RK3288 中的相应型号代码自行移植过来；

4）RK3288 & RK3368 & RK3399 & RK3326 & RV110X RAW camera支持情况，请参考Rockchip_Camera_AVL_V2.0_Package_20180515.7z, 内附Sensor驱动、效果参数文件、AVL列表。

5）Sofia 3GR camera支持情况，请参考《Rockchip SOFIA 3G-R_PMB8018(x3_C3230RK)_Camera_Module_AVL》。

6) Sensor驱动开发文档：
RK3288/RK3399/RK3368/RK3326 android isp driver配套说明相关文档：《RK_ISP10_Camera_User_Manual_V2.3》

7）RK3288/RK3399/RK3368/RK3326 android isp driver调试方法及常用问题解决方法文档：
RKISPV1_Camera_驱动调试方法V1.0.pdf
RKISPV1_Camera_常见问题解决方法V1.0.pdf
Rockchip_Trouble_Shooting_Android_CameraHAL1_CN&EN.pdf