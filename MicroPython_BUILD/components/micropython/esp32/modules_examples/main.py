import machine, network, utime

print("")
print("Starting WiFi ...")
sta_if = network.WLAN(network.STA_IF)
_ = sta_if.active(True)
sta_if.connect("mySSID", "wifi_password")
tmo = 50
while not sta_if.isconnected():
    utime.sleep_ms(100)
    tmo -= 1
    if tmo == 0:
        break

if tmo > 0:
    ifcfg = sta_if.ifconfig()
    print("WiFi started, IP:", ifcfg[0])
    utime.sleep_ms(500)

    rtc = machine.RTC()
    print("Synchronize time from NTP server ...")
    rtc.ntp_sync(server="hr.pool.ntp.org")
    tmo = 100
    while not rtc.synced():
        utime.sleep_ms(100)
        tmo -= 1
        if tmo == 0:
            break

    if tmo > 0:
        utime.sleep_ms(200)
        print("Time set:", utime.strftime("%c"))
        print("")
        _ = network.ftp.start()
