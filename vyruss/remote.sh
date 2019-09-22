WIFI=ventilastation

if nmcli -t -g GENERAL.STATE connection show $WIFI | grep activated ;
then
    echo esp wifi connected;
else
    echo connecting esp wifi;
    nmcli connection up ventilastation
fi
cd emulator
. venv/bin/activate
python emu.py 192.168.4.1
