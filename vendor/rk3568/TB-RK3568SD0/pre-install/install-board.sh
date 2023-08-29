#!/bin/bash

ostype=$1
user=$2
password=$3
model=$4
osname=$5
relver=$6

function set_led_flash()
{
	# {
	#   "model": "TB-RK3568SD0",
	#   "led": {
	#     "color": "flash-1"
	#   }
	# }

	echo "{" | tee /etc/prop/dev.json
	echo "  \"model\": \"${model}\"," | tee -a /etc/prop/dev.json
	echo "  \"led\": {" | tee -a /etc/prop/dev.json
	echo "    \"color\": \"flash-1\"" | tee -a /etc/prop/dev.json
	echo "  }" | tee -a /etc/prop/dev.json
	echo "}" | tee -a /etc/prop/dev.json
}

echo "Parmeter: ostype ${ostype}, user ${user}, password ${password}, model ${model} osname ${osname} relver ${relver}"
echo "Start to run it now ..."

# This script will be call with root.
# you can add code to install your custom package below.

set_led_flash
systemctl enable toybrick-led.service
