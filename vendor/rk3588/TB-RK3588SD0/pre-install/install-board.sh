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

add_toybrick_apt_source
#factory_test
