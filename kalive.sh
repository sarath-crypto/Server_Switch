#!/bin/bash

ts=$(date)
echo  "$ts kalive: Initializing..." >> /data/log.txt

while [ true ];
do
        rx=$(socat  -t2 -T2  -u udp4-recv:8088 -)
        if [[ ! -z "$rx" ]]; then
                if [ "$rx" == "serverkey" ]; then
                        data="serverkey"
                        echo $data | socat -t 0 - udp4-sendto:192.168.100.44:8088
                fi
                if [ "$rx" == "HALT" ]; then
                        ts=$(date)
                        echo "$ts kalive: Halt Request received" >> /home/sarath                                                                                                             /data/log.txt
                        shutdown
                        sleep 60
                fi
        fi
        sleep 2
done
