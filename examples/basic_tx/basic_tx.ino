/*
 * File                 basic_tx.ino
 *
 * Synopsis             Simple example sketch to demonstrate transmission of data
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

// These are the configuration parameters we're going to use
#define CONFIG_ENCMODE XBEE_SEC_ENCTYPE_WPA2     // Network type is WPA2 encrypted
#define CONFIG_SSID "Example"                    // SSID
#define CONFIG_KEY "whatever"                    // Password

// Create an xbee object to handle things for us
XbeeWifi xbee;

unsigned long int next_tx;

// Setup routine
void setup()
{
  // Serial at 57600
  Serial.begin(57600);
  
  // Initialize the xbee
  bool result = xbee.init(XBEE_SELECT, XBEE_ATN, XBEE_RESET, XBEE_DOUT);

  if (result) {
    // Initialization okay so far, send setup parameters - if anything fails, result goes false
    result &= xbee.at_cmd_byte(XBEE_AT_NET_TYPE, XBEE_NET_TYPE_IBSS_INFRASTRUCTURE);
    result &= xbee.at_cmd_str(XBEE_AT_NET_SSID, CONFIG_SSID);
    result &= xbee.at_cmd_byte(XBEE_AT_NET_ADDRMODE, XBEE_NET_ADDRMODE_DHCP);
    result &= xbee.at_cmd_byte(XBEE_AT_SEC_ENCTYPE, CONFIG_ENCMODE);
    if (CONFIG_ENCMODE != XBEE_SEC_ENCTYPE_NONE) {
      result &= xbee.at_cmd_str(XBEE_AT_SEC_KEY, CONFIG_KEY);
    }
  }
  
  if (!result) {
    // Something failed
    Serial.println(F("XBee Init Failed"));
    while (true) { /* Loop forever - game over */}
  } else {    
    Serial.println("XBee found and configured");
  }  
  
  next_tx = millis();
}


// Main run loop
// Transmit "Hello World" on UDP, to 192.168.1.150 every 1 minute (using port 12345)
void loop()
{
  // Just keep calling the process method on the xbee object
  xbee.process();
  
  if (millis() > next_tx) {
    if (xbee.last_status != XBEE_MODEM_STATUS_JOINED) {
      Serial.println(F("Not yet up and running"));
    } else {
      Serial.println(F("Transmitting now"));
      
      // Create an s_txoptions object to describe the port, protocol and behaviors
      s_txoptions txopts;
      txopts.dest_port=12345;
      txopts.source_port=12345;
      txopts.protocol = XBEE_NET_IPPROTO_UDP;

      // Create a binary IP address representation      
      char ip[] = { 192, 168, 1, 150 };
      
      // Transmit the frame
      if (!xbee.transmit((uint8_t *)ip, &txopts, (uint8_t *)"Hello World", 11)) {
        Serial.println(F("Transmit failed"));
      } else {
        Serial.println(F("Transmit OK"));    
      }  
    }
   
     // Repeat in one minute
     next_tx = millis() + 1000L;
  }
}


