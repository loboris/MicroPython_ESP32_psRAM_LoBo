
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

mqtt = network.mqtt("loboris", "loboris.eu", user="wifimcu", password="wifimculobo", cleansession=True, connected_cb=conncb, disconnected_cb=disconncb, subscribed_cb=subscb, published_cb=pubcb, data_cb=datacb)

# secure connection requires more memory and may not work
# mqtts = network.mqtt("eclipse", "iot.eclipse.org", secure=True, cleansession=True, connected_cb=conncb, disconnected_cb=disconncb, subscribed_cb=subscb, published_cb=pubcb, data_cb=datacb)


'''

# Wait until status is: (1, 'Connected')

mqtt.subscribe('test')
mqtt.publish('test', 'Hi from Micropython')

'''

