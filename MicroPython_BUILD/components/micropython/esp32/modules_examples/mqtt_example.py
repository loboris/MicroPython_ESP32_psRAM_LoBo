import network

def conncb(task):
    print("[{}] Connected".format(task))

def disconncb(task):
    print("[{}] Disconnected".format(task))

def subscb(task):
    print("[{}] Subscribed".format(task))

def pubcb(pub):
    print("[{}] Published: {}".format(pub[0], pub[1]))

def datacb(msg):
    print("[{}] Data arrived from topic: {}, Message:\n".format(msg[0], msg[1]), msg[2])


mqtt = network.mqtt("loboris", "mqtt://loboris.eu", user="wifimcu", password="wifimculobo", cleansession=True, connected_cb=conncb, disconnected_cb=disconncb, subscribed_cb=subscb, published_cb=pubcb, data_cb=datacb)
# secure connection requires more memory and may not work
# mqtts = network.mqtt("eclipse", "mqtts//iot.eclipse.org", cleansession=True, connected_cb=conncb, disconnected_cb=disconncb, subscribed_cb=subscb, published_cb=pubcb, data_cb=datacb)
# wsmqtt = network.mqtt("eclipse", "ws://iot.eclipse.org:80/ws", cleansession=True, data_cb=datacb)

mqtt.start()

#mqtt.config(lwt_topic='status', lwt_msg='Disconected')




'''

# Wait until status is: (1, 'Connected')

mqtt.subscribe('test')
mqtt.publish('test', 'Hi from Micropython')

mqtt.stop()

'''

# ==================
# ThingSpeak example
# ==================

import network

def datacb(msg):
    print("[{}] Data arrived from topic: {}, Message:\n".format(msg[0], msg[1]), msg[2])

thing = network.mqtt("thingspeak", "mqtt://mqtt.thingspeak.com", user="anyName", password="ThingSpeakMQTTid", cleansession=True, data_cb=datacb)
# or secure connection
#thing = network.mqtt("thingspeak", "mqtts://mqtt.thingspeak.com", user="anyName", password="ThingSpeakMQTTid", cleansession=True, data_cb=datacb)

thingspeakChannelId = "123456"                           # enter Thingspeak Channel ID
thingspeakChannelWriteApiKey = "ThingspeakWriteAPIKey"   # EDIT - enter Thingspeak Write API Key
thingspeakFieldNo = 1
thingSpeakChanelFormat = "json"

pubchan = "channels/{:s}/publish/{:s}".format(thingspeakChannelId, thingspeakChannelWriteApiKey)
pubfield =  "channels/{:s}/publish/fields/field{}/{:s}".format(thingspeakChannelId, thingspeakFieldNo, thingspeakChannelWriteApiKey)
subchan = "channels/{:s}/subscribe/{:s}/{:s}".format(thingspeakChannelId, thingSpeakChanelFormat, thingspeakChannelWriteApiKey)
subfield = "channels/{:s}/subscribe/fields/field{}/{:s}".format(thingspeakChannelId, thingspeakFieldNo, thingspeakChannelWriteApiKey)

thing.start()
tmo = 0
while thing.status()[0] != 2:
    utime.sleep_ms(100)
    tmo += 1
    if tmo > 80:
        print("Not connected")
        break

# subscribe to channel
thing.subscribe(subchan)

# subscribe to field
thing.subscribe(subfield)

# publish to channel
# Payload can include any of those fields separated b< ';':
# "field1=value;field2=value;...;field8=value;latitude=value;longitude=value;elevation=value;status=value"
thing.publish(pubchan, "field1=25.2;status=On line")

# Publish to field
thing.publish(pubfield, "24.5")

