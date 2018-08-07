from machine import I2C
import time
import sgp30

i2c=I2C(sda=14,scl=27)
sensor=sgp30.SGP30(i2c)

#print("=============== func №1 ===============")
sensor.init()
#print("=======================================\n")

#print("=============== func №2 ===============")
print("get_serial_id():%u" % sensor.get_serial_id())
#print("=======================================\n")

#print("=============== func №3 ===============")
print("get_driver_version():%s" % sensor.get_driver_version())
#print("=======================================\n")

#print("=============== func №4 ===============")
print("get_feature_set_version():", sensor.get_feature_set_version())
#print("=======================================\n")

#print("=============== func №5 ===============")
print("get_configured_address():%u" % sensor.get_configured_address())
#print("=======================================\n")

for i in range(1,8):
    #print("=============== func №6 ===============")
    print(" co2=%u" % sensor.measure_co2_eq_blocking_read(), end='')
    #print("=======================================\n")
    time.sleep(1) 
    #print("=============== func №7 ===============")
    print(" voc=%u" % sensor.measure_tvoc_blocking_read(), end='')
    #print("=======================================\n")
    time.sleep(1) 
    #print("=============== func №8 ===============")
    print(" iaq:", sensor.measure_iaq_blocking_read(), end='')
    #print("=======================================\n")
    time.sleep(1) 
    #print("=============== func №9 ===============")
    print(" sig:", sensor.measure_signals_blocking_read())
    #print("=======================================\n")
    time.sleep(1) 

print("=============== func №10 ===============")
bl=sensor.get_iaq_baseline()
print("get_iaq_baseline():%u" % bl)
print("========================================\n")

print("=============== func №11 ===============")
tvoc_bl=sensor.get_tvoc_factory_baseline()
print("get_tvoc_factory_baseline():%u" % tvoc_bl)
print("========================================\n")


for i in range(1,8):
    #print("=============== func №20 ===============")
    #print(" mvoc:%u" % sensor.measure_tvoc(), end='')
    sensor.measure_tvoc()
    #print("========================================\n")
    time.sleep_ms(20) 
    #print("=============== func №14 ===============")
    print(" voc=%u" % sensor.read_tvoc(), end='')
    #print("========================================\n")

    time.sleep(1) 

    #print("=============== func №13 ===============")
    #print(" mco2:%u" % sensor.measure_co2_eq(), end='')
    sensor.measure_co2_eq()
    #print("========================================\n")
    time.sleep_ms(20) 
    #print("=============== func №15 ===============")
    print(" co2=%u" % sensor.read_co2_eq(), end='')
    #print("========================================\n")

    time.sleep(1) 

    #print("=============== func №16 ===============")
    #print(" miaq:%u" % sensor.measure_iaq(), end='')
    sensor.measure_iaq()
    #print("========================================\n")
    time.sleep_ms(20) 
    #print("=============== func №18 ===============")
    print(" iaq:", sensor.read_iaq(), end='')
    #print("========================================\n")

    time.sleep(1) 

    #print("=============== func №17 ===============")
    #print(" msig:%u" % sensor.measure_signals(), end='')
    sensor.measure_signals()
    #print("========================================\n")
    time.sleep_ms(20) 
    #print("=============== func №19 ===============")
    print(" sig:", sensor.read_signals())
    #print("========================================\n")

    time.sleep(1) 


print("=============== func №20 ===============")
print("current baseline=%u" % sensor.get_iaq_baseline())
print("set_iaq_baseline():%u" % sensor.set_iaq_baseline(bl))
print("new baseline=%u" % sensor.get_iaq_baseline())
print("========================================\n")

print("=============== func №21 ===============")
print("set_tvoc_baseline():%u" % sensor.set_tvoc_baseline(tvoc_bl))
print("new tvoc_factory_baseline=%u" % sensor.get_tvoc_factory_baseline())
print("========================================\n")

print("=============== func №22 ===============")
print("set_absolute_humidity():%u" % sensor.set_absolute_humidity(0x0b92))
print("========================================\n")

print("=============== func №23 ===============")
print("measure_test():%u" % sensor.measure_test())
print("========================================\n")


