/*
 * File                 network_scan.ino
 *
 * Synopsis             Simple example sketch to demonstrate network scan
 *
 * Author               Chris Bearman
 *
 * Version              2.0
 */
#include <XbeeWifi.h>

// These are the pins that we are using to connect to the Xbee
#define XBEE_RESET 20
#define XBEE_ATN 2
#define XBEE_SELECT SS
#define XBEE_DOUT 23

// XBee object
XbeeWifi xbee;

// Receive and output scan data
void inbound_scan(uint8_t encryption_mode, int rssi, char *ssid)
{
  Serial.print(F("Scan Information, SSID="));
  Serial.print(ssid);
  Serial.print(F(", RSSI=-"));
  Serial.print(rssi, DEC);
  Serial.print(F("dB, Security="));
  switch(encryption_mode) {
    case XBEE_SEC_ENCTYPE_NONE  :  Serial.println(F("none")); break;
    case XBEE_SEC_ENCTYPE_WEP   :  Serial.println(F("wep")); break;
    case XBEE_SEC_ENCTYPE_WPA   :  Serial.println(F("wpa")); break;
    case XBEE_SEC_ENCTYPE_WPA2  :  Serial.println(F("wpa2")); break;
    default                     :  Serial.println(F("Unknown")); break;
  }
}

// Keep track of 10 second intervals at which point we'll initiate scans
unsigned long next_scan;

// Setup routine
void setup()
{
  // Serial at 57600
  Serial.begin(57600);
  
  // Initialize the xbee
  bool result = xbee.init(XBEE_SELECT, XBEE_ATN, XBEE_RESET, XBEE_DOUT);
  
  if (!result) {
    // Something failed
    Serial.println(F("XBee Init Failed"));
    while (true) { /* Loop forever - game over */}
  } else {
    // Register for incoming scan data
  xbee.register_scan_callback(inbound_scan);
    
    Serial.println("XBee found and configured");
  }  
  
  next_scan = millis();
  
}

// Main run loop
void loop()
{
  // Initiate a scan every ten seconds
  if (millis() >= next_scan) {
    if (xbee.initiateScan()) {
      Serial.println(F("Scan initiated"));
    } else {
      Serial.println(F("Failed to initiate scan"));
    }
    next_scan = millis() + 10000;
  }
  
  // Process the xbee
  xbee.process();
}


