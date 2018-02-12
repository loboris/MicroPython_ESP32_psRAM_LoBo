#include "80211raw.h"
#include "string.h"

const void wifi_rawtx(const void *buffer)
{
	esp_wifi_80211_tx(WIFI_IF_AP, buffer, sizeof(buffer), false);
}

uint8_t beacon_raw[] = {
	0x80, 0x00,							// 0-1: Frame Control
	0x00, 0x00,							// 2-3: Duration
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff,	// 4-9: Destination address (broadcast)
	0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06,	// 10-15: Source address
	0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06,	// 16-21: BSSID
	0x00, 0x00,							// 22-23: Sequence / fragment number
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,			// 24-31: Timestamp (GETS OVERWRITTEN TO 0 BY HARDWARE)
	0x64, 0x00,							// 32-33: Beacon interval
	0x31, 0x04,							// 34-35: Capability info
	0x00, 0x00, /* FILL SSID HERE */	// 36-38: SSID parameter set, 0x00:length:content
	0x01, 0x08, 0x82, 0x84,	0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,	// 39-48: Supported rates
	0x03, 0x01, 0x01,					// 49-51: DS Parameter set, current channel 1 (= 0x01),
	0x05, 0x04, 0x01, 0x02, 0x00, 0x00,	// 52-57: Traffic Indication Map

};
#define SRCADDR_OFFSET 10
#define BSSID_OFFSET 16
#define SEQNUM_OFFSET 22
#define BEACON_SSID_OFFSET 38
const void wifi_beacontx(char *ssid[])
{
	uint8_t packet[200];
	uint8_t ssidSize = (sizeof(ssid) / sizeof(char *));
	uint8_t line = 0;
	// Keep track of beacon sequence numbers on a per-line-basis
	uint16_t seqnum[0] = { 0 };

	// Insert one line of ssid into beacon packet
	printf("%i %i %s\r\n", strlen(ssid[line]), ssidSize, ssid[line]);

	memcpy(packet, beacon_raw, BEACON_SSID_OFFSET - 1);
	packet[BEACON_SSID_OFFSET - 1] = strlen(ssid[line]);
	memcpy(&packet[BEACON_SSID_OFFSET], ssid[line], strlen(ssid[line]));
	memcpy(&packet[BEACON_SSID_OFFSET + strlen(ssid[line])], &beacon_raw[BEACON_SSID_OFFSET], sizeof(beacon_raw) - BEACON_SSID_OFFSET);

	// Last byte of source address / BSSID will be line number - emulate multiple APs broadcasting one song line each
	packet[SRCADDR_OFFSET + 5] = line;
	packet[BSSID_OFFSET + 5] = line;

	// Update sequence number
	packet[SEQNUM_OFFSET] = (seqnum[line] & 0x0f) << 4;
	packet[SEQNUM_OFFSET + 1] = (seqnum[line] & 0xff0) >> 4;
	seqnum[line]++;
	if (seqnum[line] > 0xfff)
		seqnum[line] = 0;

	wifi_rawtx(packet);

	if (++line >= ssidSize)
		line = 0;
}

const void wifi_msgtx(char *msg)
{
	uint8_t packet[200];
	//
	wifi_rawtx(packet);
}
