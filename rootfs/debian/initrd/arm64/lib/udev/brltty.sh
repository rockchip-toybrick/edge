#!/bin/sh
grep -q brltty /proc/cmdline && exit 0
exec /lib/brltty/brltty.sh -E "$@"
