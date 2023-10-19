#!/bin/bash
#WIFI=ventilastation
#nmcli connection down ventilastation
#if nmcli -t -g GENERAL.STATE connection show $WIFI | grep activated ;
#then
#    echo esp wifi connected;
#else
#    echo connecting esp wifi;
#    nmcli connection up ventilastation
#fi
cd "$(dirname "$0")"
cd emulator
echo > /tmp/out.log 2>/tmp/err.log
. venv/bin/activate
while true; do
    python emu.py SERIAL --no-display #  >> /tmp/out.log 2>>/tmp/err.log
done
#python emu.py 192.168.4.1 --no-display
