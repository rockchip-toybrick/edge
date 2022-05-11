#!/bin/bash
TEMP_MODULES_PATH=$1
HEAD_KO=(rockchip-iommu.ko pinctrl-rockchip.ko pinctrl-rk805.ko pm_domains.ko io-domain.ko clk-pwm.ko clk-scmi.ko clk-rk808.ko clk-rk3568.ko clk-rk3399.ko
)
RET_FILE=./modules_scan_result.load
RET_KO=`find $TEMP_MODULES_PATH -type f -name *.ko | xargs basename -a|sort|uniq`

rm -rf $RET_FILE
for ko in ${HEAD_KO[@]}
do
    echo $ko >> $RET_FILE
done

for ko in ${RET_KO[@]}
do
    found=0
    for head in ${HEAD_KO[@]}
    do
        if [ "$head" == "$ko" ]
        then
            #echo "dup"
            found=1
            break
        fi
    done
    if [ "$found" != 1 ]
    then
        echo $ko >> $RET_FILE
    fi
done

echo -e "\033[32mSave modules scan result as $RET_FILE \033[0m"
