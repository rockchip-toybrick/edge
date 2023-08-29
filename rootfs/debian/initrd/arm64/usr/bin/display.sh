#!/bin/sh

DISP_LIST="HDMI-A-1 HDMI-A-2 DP-1 DP-2 DSI-1 DSI-2"

case $1 in
hdmi1)
	disp_on=HDMI-A-1
	;;
hdmi2)
	disp_on=HDMI-A-2
	;;
dp1)
	disp_on=DP-1
	;;
dp2)
	disp_on=DP-2
	;;
dsi1)
	disp_on=DSI-1
	;;
dsi2)
	disp_on=DSI-2
	;;
auto)
	disp_on=""
	for d in ${DISP_LIST}
	do
		if [ ! -f /sys/class/drm/card0-$d/status ]; then
			continue
		fi
		if [ "$(cat /sys/class/drm/card0-$d/status)" == "connected" ]; then
			disp_on=$d
			break
		fi
	done
	;;
*)
	echo "Usage: display.sh hdmi1|hdmi2|dp1|dp2|dsi1|dsi2|auto"
	exit 0
	;;
esac

if [ -z ${disp_on} ]; then
	exit 0
fi


for d in ${DISP_LIST}
do
	if [ ! -f /sys/class/drm/card0-$d/status ]; then
		continue
	fi
	if [ $d == ${disp_on} ]; then
		echo on > /sys/class/drm/card0-$d/status
	else
		echo off > /sys/class/drm/card0-$d/status
	fi
done
