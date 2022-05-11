#!/bin/bash

set -e

part_start=2048
kernel_size=204800  # 100M
gpt_last_size=33

function gen_part_table()
{
	disk=$1
	part_table=$2
	kernel_uuid=$3
	rootfs_uuid=$4
	disk_size=$5

	part1_start=${part_start}
	part1_size=${kernel_size}
	part2_start=$(expr ${kernel_size} + ${part_start})
	part2_size=$(expr ${disk_size} - ${part2_start} - ${gpt_last_size})
	echo "label: gpt" > ${part_table}
	echo "label-id: $(uuidgen)" >> ${part_table}
	echo "unit: sectors" >> ${part_table}
	echo "first-lba: 34" >> ${part_table}
	echo "sector-size: 512" >> ${part_table}
	echo >> ${part_table}
	echo "${disk}1: start=${part1_start}, size=${part1_size}, type=${kernel_uuid}, uuid=$(uuidgen)" >> ${part_table}
	echo "${disk}2: start=${part2_start}, size=${part2_size} , type=${rootfs_uuid}, uuid=$(uuidgen)" >> ${part_table}
}

function sdcard_new_parts()
{
	disk=$1
	part_table=$2
	kernel_uuid=$3
	rootfs_uuid=$4
	disk_size=$5
	
	echo -e "\033[32m[EDGE INFO] Start to create two partitions on ${disk} ...\033[0m"
	gen_part_table ${disk} ${part_table} ${kernel_uuid} ${rootfs_uuid} ${disk_size}
	sudo sfdisk --delete ${disk}
	sudo sfdisk ${disk} < ${part_table}
	rm -rf ${part_table}
}

function sdcard_flash_kernel()
{
	disk=$1
	kernel_image=$2
	echo -e "\033[32m[EDGE INFO] Start to flash boot_linux to ${disk} ...\033[0m"
	sudo dd if=${kernel_image} of=${disk}1
}

function sdcard_flash_rootfs()
{
	disk=$1
	rootfs_image=$2
	echo -e "\033[32m[EDGE INFO] Start to flash rootfs to ${disk} ...\033[0m"
	sudo dd if=${rootfs_image} of=${disk}2
}

image_path=$2
kernel_uuid=$3
rootfs_uuid=$4
part_table=${image_path}/part_table
kernel_image=${image_path}/boot_linux.img
rootfs_image=${image_path}/rootfs.img

while :
do
	i=0
	disks=$(ls /dev/sd*[a-z])
	echo -e "\033[32m[EDGE DEBUG] disk list:\033[0m"
	for d in ${disks}
	do
		disk_array[$i]=$d
		echo "$i: $d"
		i=$(expr $i + 1)
	done
	read -n 1 -p "[EDGE INFO] Enter the number of disk to flash images: " index
	printf "\n"

	if [ "${index}" -ge 0 ] 2>/dev/null; then
		if [ $i -le ${index} ]; then
			echo -e "\033[31m[EDGE_ERROR] the number of disk is invalid.\033[0m"
			exit 1
		fi
	else
		echo -e "\033[31m[EDGE_ERROR] the number of disk is invalid.\033[0m"
		exit 1
			
	fi

	echo -e "\033[32m[EDGE INFO] The disk to flash images: ${disk_array[${index}]} \033[0m"
	echo -e "\033[33m[EDGE WARNING] All data on this disk will be cleared!\033[0m"
	echo -e "\033[33m[EDGE WARNING] This may be dangerous if the disk is system disk!\033[0m"
	echo -e "\033[33m[EDGE WARNING] Please make sure the disk is right!\033[0m"
	read -n 1 -p "[EDEG DEBUG] Is this disk right?(Y/n)" str
	printf "\n"
	if [ "${str}]" == "Y" ] || [ "${str}" == "y" ]; then
		break;
	else
		exit 1
	fi
done

disk=${disk_array[${index}]}
disk_size=$(sudo fdisk -l ${disk} | grep "${disk}" | awk -F' ' '{print $7}')
for d in $(ls ${disk}*)
do
	sudo umount $d || true
done

case $1 in
new)
	sdcard_new_parts ${disk} ${part_table} ${kernel_uuid} ${rootfs_uuid} ${disk_size}
	;;
boot_linux)
	sdcard_flash_kernel ${disk} ${kernel_image}
	;;
rootfs)
	sdcard_flash_kernel ${disk} ${rootfs_image}
	;;
all)
	sdcard_new_parts ${disk} ${part_table} ${kernel_uuid} ${rootfs_uuid} ${disk_size}
	sdcard_flash_kernel ${disk} ${kernel_image}
	sdcard_flash_kernel ${disk} ${rootfs_image}
	;;
*)
	echo "[EDGE DEBUG] Usage: sddisk.sh [new|boot_linux|rootfs|all] IMAGE_PATH KERNEL_UUID ROOTFS_UUID"
	exit 1
	;;
esac

