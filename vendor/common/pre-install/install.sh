#!/bin/bash

set -e

ostype=$1
user=$2
password=$3
model=$4
osname=$5
relver=$6
apturl=$7
key=$8
alias=$(cat /etc/apt/sources.list | grep main | awk -F' ' 'NR==1 {print $3}')
hostname=${osname}.${user}
if [ -f /etc/gdm3/daemon.conf ]; then
	gdm3_conf=/etc/gdm3/daemon.conf
elif [ -f /etc/gdm3/custom.conf ]; then
	gdm3_conf=/etc/gdm3/custom.conf
else
	gdm3_conf=none
fi

apt update --fix-missing
apt -y upgrade
apt -y install vim expect sudo gnupg network-manager net-tools rfkill bluez usbutils ssh pciutils gcc make cmake git python3-pip
touch /etc/NetworkManager/conf.d/10-globally-managed-devices.conf

if [ -z "$(echo ${ostype} | grep origin)" ]; then
	# Add user
	if [ ! -d /home/${user} ]; then
		echo "Add user ${user}, password ${password} ..."
		useradd -s '/bin/bash' -m -G adm,sudo ${user}
		pre-install/epasswd ${user} ${password}
	else
		echo "User ${user} already exists, ignore it"
	fi
	sudo -u ${user} echo "syntax on" | tee /home/${user}/.vimrc
	sudo -u ${user} echo "set mouse=v" | tee -a /home/${user}/.vimrc
fi

# Install desktop/server package
case ${ostype} in
gnome*)
	apt -y install task-gnome-desktop || apt -y install ${osname}-desktop || echo "Ignore install gnome desktop failure"
	;;
lxde*)
	apt -y install task-lxde-desktop || echo "Ignore install lxde desktop failure"
	;;
server*)
	apt -y install task-ssh-desktop || echo "Ignore install ssh sever failure"
	;;
*)
	;;
esac

if [ ! -z "$(echo ${ostype} | grep origin)" ]; then
	exit 0
fi

# Add edge apt source
apt-key add pre-install/key/${key}
echo "deb ${apturl}/edge/${osname}-release-v${relver} ${alias} main" | tee /etc/apt/sources.list.d/edge.list
echo "deb file:/usr/share/packages-local ${alias} main" | tee /etc/apt/sources.list.d/local.list

# Add edge pip source
echo "[global]" | tee /etc/pip.conf
echo "extra-index-url = http://repo.rock-chips.com/edge/pypi/simple http://pypi.douban.com/simple" | tee -a /etc/pip.conf
echo "trusted-host = repo.rock-chips.com pypi.douban.com" | tee -a /etc/pip.conf

mkdir -p /data
if [ ! -h /userdata ]; then
	ln -s /data/ /userdata
fi
# Mount disk
echo "#" | tee /etc/fstab
echo "# /etc/fstab" | tee -a /etc/fstab
echo "# See man pages fstab(5), findfs(8), mount(8) and/or blkid(8) for more info" | tee -a /etc/fstab
echo "#" | tee -a /etc/fstab
echo | tee -a /etc/fstab
echo "/dev/disk/by-partlabel/boot_linux /boot                      ext2     defaults,nofail        1 1" | tee -a /etc/fstab
echo "/dev/disk/by-partlabel/userdata /data                      ext4     defaults,nofail        1 1" | tee -a /etc/fstab

# Set hostmame
echo ${hostname} | tee /etc/hostname
echo "127.0.0.1	localhost ${hostname}" | tee /etc/hosts
echo "127.0.1.1	${hostname}" | tee -a /etc/hosts
echo
echo "# The following lines are desirable for IPv6 capable hosts" | tee -a /etc/hosts
echo "::1     localhost ${hostname} ip6-localhost ip6-loopback" | tee -a /etc/hosts
echo "ff02::1 ip6-allnodes" | tee -a /etc/hosts
echo "ff02::2 ip6-allrouters" | tee -a /etc/hosts

# Add more privilege for user
usermod -a -G adm ${user}
usermod -a -G sudo ${user}
usermod -a -G input ${user}
usermod -a -G video ${user}
usermod -a -G render ${user}
usermod -a -G netdev ${user}

# Disable suspend
sudo -u ${user} mkdir -p /home/${user}/.config/dconf
cp pre-install/config/dconf/user /home/${user}/.config/dconf/

if [ "${gdm3_conf}" != "none" ]; then
	# Enable autologin with $user
	sed -i "s/#  AutomaticLoginEnable = true/AutomaticLoginEnable = true/g" ${gdm3_conf}
	sed -i "s/#  AutomaticLogin = user1/AutomaticLogin = ${user}/g" ${gdm3_conf}
	# Enable wayland
	sed -i "s/^WaylandEnable=false/#WaylandEnable=false/g" ${gdm3_conf}
fi

# Add alias: ll, la and l
sed -i "s/#alias ll=/alias ll=/g" /home/${user}/.bashrc
sed -i "s/#alias la=/alias la=/g" /home/${user}/.bashrc
sed -i "s/#alias l=/alias l=/g" /home/${user}/.bashrc

if [ ${ostype} == "lxde" ]; then
	sed -i "s/#autologin-user=/autologin-user=${user}/g" /etc/lightdm/lightdm.conf
elif [ ${osname} == "debian" ]; then
	# set GDK_BACKEND wayland
	echo "export GDK_BACKEND=wayland" | tee /etc/environment
	# Firefox: enable wayland ...
	echo "export MOZ_ENABLE_WAYLAND=1" | tee -a /etc/environment
else
	# Firefox: enable wayland ...
	echo "export MOZ_ENABLE_WAYLAND=1" | tee -a /etc/environment
fi

echo "export VK_DRIVER_FILES=/etc/vulkan/icd.d/edge_vk.json" | tee -a /etc/environment

mkdir -p /etc/prop
echo "{" | tee /etc/prop/dev.json
echo "  \"model\": \"${model}\"" | tee -a /etc/prop/dev.json
echo "}" | tee -a /etc/prop/dev.json

apt update
apt -y install rockchip-mali recovery gstreamer1.0-rockchip1 vendor-firmware edge-utils toybrick-server toybrick-usbd toybrick-prop-bin rockchip-isp toybrick-vendor-bin
apt -y upgrade

systemctl enable toybrick.service
systemctl enable toybrick-prop.service
systemctl enable toybrick-usb.service
systemctl enable rockchip-isp.service
systemctl mask plymouth-quit-wait.service

if [ -f /usr/sbin/iptables-legacy ]; then
	update-alternatives --set iptables /usr/sbin/iptables-legacy
	update-alternatives --set ip6tables /usr/sbin/ip6tables-legacy
fi

if [ ! -z "$(echo ${ostype} | grep docker)" ]; then
	toybrick-install.sh docker
fi
if [ ! -z "$(echo ${ostype} | grep ros1)" ]; then
	toybrick-install.sh ros1
fi
if [ ! -z "$(echo ${ostype} | grep ros2)" ]; then
	toybrick-install.sh ros2
fi

sudoer.sh ${user}
sudo -u ${user} toybrick-install.sh rknn

pre-install/install-board.sh $*

sudo apt -y reinstall qemu-system-arm

link_mali.sh down
sleep 1
link_mali.sh up

link_isp.sh down
sleep 1
link_isp.sh up

link_rknn.sh down
sleep 1
link_rknn.sh up

echo "Edge Release V${relver}" | tee /etc/edge-release
