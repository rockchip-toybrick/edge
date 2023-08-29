#!/bin/bash

ostype=$1
user=$2
password=$3
model=$4
osname=$5
relver=$6

echo "Parmeter: ostype ${ostype}, user ${user}, password ${password}, model ${model} osname ${osname} relver ${relver}"
echo "Start to run it now ..."
# This script will be call with root.
# you can add code to install your custom package below.

alias=bullseye
function add_toybrick_apt_source()
{
	
	echo "Add toybrick apt source ..."
	echo "deb http://repo.rock-chips.com/edge/debian-toybrick ${alias} main" | tee /etc/apt/sources.list.d/toybrick.list
	
	apt update
	apt -y upgrade
}

function factory_test()
{
	echo "deb http://10.10.10.117/edge/debian-toybrick ${alias} main" | tee /etc/apt/sources.list.d/factory.list
	apt update
	apt -y upgrade
	apt -y install factory-test
}

function autostart()
{
	mkdir -p /home/${user}/.config/autostart
	echo "[Desktop Entry]" | tee /home/${user}/.config/autostart/toybrick-autostart.desktop
	echo "Type=Application" | tee -a /home/${user}/.config/autostart/toybrick-autostart.desktop 
	echo "Name=Toybrick Autostart" | tee -a /home/${user}/.config/autostart/toybrick-autostart.desktop 
	echo "Comment=Start with toybrick HELP" | tee -a /home/${user}/.config/autostart/toybrick-autostart.desktop 
	echo "Exec=gnome-terminal -- /usr/local/bin/toybrick_startup.sh" | tee -a /home/${user}/.config/autostart/toybrick-autostart.desktop 

	chown -R ${user}:${user} /home/${user}/.config
}

function set_display_prop()
{
	# {
	#   "model": "TB-RK3588X0",
	#   "display": {
	#     "dsi": "off"
	#   }
	# }

	echo "{" | tee /etc/prop/dev.json
	echo "  \"model\": \"${model}\"," | tee -a /etc/prop/dev.json
	echo "  \"display\": {" | tee -a /etc/prop/dev.json
	echo "    \"dsi\": \"off\"" | tee -a /etc/prop/dev.json
	echo "  }" | tee -a /etc/prop/dev.json
	echo "}" | tee -a /etc/prop/dev.json
}

autostart
add_toybrick_apt_source
set_display_prop
#factory_test
