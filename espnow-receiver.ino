#include <esp_now.h>
#include <Arduino.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif ESP32
#include <WiFi.h>
#endif

#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCBoards.h>

#ifdef BOARD_HAS_USB_SERIAL
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
#include <SLIPEncodedSerial.h>
 SLIPEncodedSerial SLIPSerial(Serial); 
#endif

bool espnow_hasError = false;
std::string espnow_errorMsg;
esp_now_peer_info_t broadcastPeer;

// expects the received ESPNow message to be an OSCMessage
// prepends the sender's MAC address (string) as the first OSC argument
// note: ignores malformed messages
void onESPNowRecv(const uint8_t *mac, const uint8_t *data, int data_len) {

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  OSCMessage origMsg;
  // we need to loose const on data, but msg.fill doesnt touch data
  origMsg.fill((uint8_t*)data, data_len);

  OSCMessage msg(&origMsg);
  msg.empty();
  msg.add(macStr);
  for (int i = 0; i < origMsg.size(); ++i)
    msg.add(origMsg.getOSCData(i));

  if (!msg.hasError()) {
    SLIPSerial.beginPacket();
    msg.send(SLIPSerial);
    SLIPSerial.endPacket();
  }
}

// TODO: forward OSC to ESPNow clients
// for now, it only replies to /status
void onSerial() {
  OSCBundle bundleIN;
  int size;
  while(!SLIPSerial.endofPacket())
  if ((size = SLIPSerial.available()) > 0) {
    while(size--) bundleIN.fill(SLIPSerial.read());
  }

  if (!bundleIN.hasError()) {
    bundleIN.route("/status", replyStatus);
  }
}

void replyStatus(OSCMessage& msg, int addrOffset) {
  OSCMessage reply("/status.reply");
  if (espnow_hasError) {
    reply.add("error")
      .add(espnow_errorMsg.c_str());
  } else {
    reply.add("ok")
      .add(WiFi.macAddress().c_str());
  }

  SLIPSerial.beginPacket();
  reply.send(SLIPSerial);
  SLIPSerial.endPacket();
}



void slip_ping() {
  OSCMessage ping("/ping");
  SLIPSerial.beginPacket();
  ping.send(SLIPSerial);
  SLIPSerial.endPacket();
}

/*****************************
ESPNow broadcast setup
*****************************/

bool connectToWiFi() {
  #ifdef ESP8266
    WiFi.mode(WIFI_STA); // MUST NOT BE WIFI_MODE_NULL
  #elif ESP32
    WiFi.mode(WIFI_MODE_STA);
  #endif
  if (esp_now_init() != ESP_OK) {
    espnow_setError("esp_now_init failed");
    return false;
  }
  esp_now_register_recv_cb(onESPNowRecv);
  // Serial.println("Receiver ready.");
  // Serial.print("ESP Mac Address: ");
  // Serial.println(WiFi.macAddress());
  return true;
}

bool espnowBroadcastInit() {
  memset(&broadcastPeer, 0, sizeof(broadcastPeer));
  // for broadcasts the addr needs to be ff:ff:ff:ff:ff:ff
	for (int ii = 0; ii < 6; ++ii) {
		broadcastPeer.peer_addr[ii] = (uint8_t)0xff;
	}
	// broadcastPeer.channel = CHANNEL; // pick a channel
	broadcastPeer.encrypt = 0; // no encryption

  return espnowBroadcastPair();
}

bool espnowBroadcastPair() {
  const esp_now_peer_info_t *peer = &broadcastPeer;
  const uint8_t *peer_addr = broadcastPeer.peer_addr;
  // exit if already paired
  if (esp_now_is_peer_exist(peer_addr)) return true;
  // pair
  esp_err_t addStatus = esp_now_add_peer(peer);
  switch(addStatus) {
    case ESP_OK:
    case ESP_ERR_ESPNOW_EXIST:
      // pair success
      return true;
    case ESP_ERR_ESPNOW_NOT_INIT:
      espnow_setError("ESPNOW Not Init"); return false;
    case ESP_ERR_ESPNOW_ARG:
      espnow_setError("Pairing: invalid argument"); return false;
    case ESP_ERR_ESPNOW_FULL:
      espnow_setError("Peer list full"); return false;
    case ESP_ERR_ESPNOW_NO_MEM:
      espnow_setError("Out of memory"); return false;
    default:
      espnow_setError("Unknown error"); return false;
  }
}

void espnow_setError(const char* errorMsg) {
  espnow_hasError = true;
  espnow_errorMsg = errorMsg;
}

/*****************************
Arduino setup
*****************************/

void setup() {
  SLIPSerial.begin(115200);

  if (!connectToWiFi()) return;

  if (!espnowBroadcastInit()) return;
}

void loop() {
  onSerial();
}
