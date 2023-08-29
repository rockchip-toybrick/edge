#!/system/bin/sh
INTERVAL=300
TIME=$(date "+%Y%m%d-%H%M")
SF_FD=/sdcard/rk/$TIME/sf_fd.txt
SS_FD=/sdcard/rk/$TIME/system_server_fd.txt
SF_DUMP=/sdcard/rk/$TIME/sf_dump.txt
COUNT=1

#SF_PID=$(ps -ef | grep surfaceflinger | grep -v "grep" | awk '{print $2}')
#SF_PID=$(ps -ef | grep surfaceflinger | grep -v "grep" | cut -c 11-16)
#SS_PID=$(ps -ef | grep system_server | grep -v "grep" | cut -c 11-16)
SF_PID=$(pgrep surfaceflinger)
SS_PID=$(pgrep system_server)

echo "$SF_PID"
echo "$SS_PID"

mkdir -p /sdcard/rk/$TIME
 #echo `date >> $LOG`
 #echo `date >> $SLAB`
while((1));do
  #echo `cat /proc/meminfo | busybox grep -E "Slab|SReclaimable|SUnreclaim" >> $MEMINFO`
  #echo `cat /proc/slabinfo >> $SLAB`
  NOW=`date`
  TIME_LABEL="\n$NOW:--------------------------------------------------:$COUNT"
  echo -e  $TIME_LABEL >> $SF_FD
  `ls -la /proc/${SF_PID}/fd  >> $SF_FD`
  
  echo -e  $TIME_LABEL >> $SS_FD
  `ls -la /proc/${SS_PID}/fd  >> $SS_FD`

  echo -e  $TIME_LABEL >> $SF_DUMP
  `dumpsys SurfaceFlinger >> $SF_DUMP`

  COUNT=$(expr $COUNT + 1 )
  sleep $INTERVAL
done
