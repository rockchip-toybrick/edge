#!/bin/sh
pid=/var/run/brltty.pid
[ -r $pid ] && kill -0 `cat $pid` && exit 0

echo debconf-set debian-installer/framebuffer false > /lib/debian-installer.d/S20brltty
rm -f /lib/debian-installer.d/S19brltty

if [ -f /var/run/brltty-Xorg ]
then
	rm /var/run/brltty-Xorg
(
	XORG=""
	while [ -z "$XORG"  ]
	do
		XORG=`pidof Xorg`
		sleep 1
	done
	kill "$XORG"
) &
(
	BTERM=""
	while [ -z "$BTERM"  ]
	do
		BTERM=`pidof bterm`
		sleep 1
	done
	kill "$BTERM"
) &
fi

exec /bin/brltty -P $pid "$@"
