/*
 * File                 basic_tx.ino
 *
 * Synopsis             Simple example sketch to demonstrate transmission of data
 *                      at the highest possible rate
 *
 * Author               Chris Bearman
 *
 * Version              1.0
 */
#include <XbeeWifi.h>

// These are the pins that we are using to connect to the Xbee
#define XBEE_RESET 15
#define XBEE_ATN 2
#define XBEE_SELECT SS
#define XBEE_DOUT 23

// These are the configuration parameters we're going to use
#define CONFIG_ENCMODE XBEE_SEC_ENCTYPE_WPA2     // Network type is WPA2 encrypted
#define CONFIG_SSID "Example"                    // SSID
#define CONFIG_KEY "whatever"                    // Password

// Create an xbee object to handle things for us
XbeeWifi xbee;

// Setup routine

// Size and buffer to hold our test data
#define TEST_FRAME_SIZE 1024
char testFrame[TEST_FRAME_SIZE];

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
    Serial.println("XBee Init Failed");
    while (true) { /* Loop forever - game over */}
  } else {    
    Serial.println("XBee found and configured");
  }    
  
  // Set up data in test frame
  for (int i = 0 ; i < TEST_FRAME_SIZE; i++) {
    testFrame[i] = 65 + (i % 26);  //(A thru Z, repeating)
  }
  
}

// To help split lines
int split = 0;

// Main run loop
// Transmit "Hello World" on UDP, to 192.168.1.150 every 1 minute (using port 12345)
void loop()
{
  // Process the XBEE
  xbee.process();

  if (xbee.last_status != XBEE_MODEM_STATUS_JOINED) {
    Serial.println("Not yet up and running");
    delay(1000);
  } else {    
    // Create an s_txoptions object to describe the port, protocol and behaviors
    s_txoptions txopts;
    txopts.dest_port=12345;
    txopts.source_port=12345;
    txopts.leave_open=true;
    txopts.protocol = XBEE_NET_IPPROTO_TCP;  // Change to UDP if desired
    
    // Create a binary IP address representation      
    char ip[] = { 192, 168, 1, 150 };
    
    // Transmit the frame
    // You could turn off frame confirmation (add false as fifth option) but you'll overwhelm the device and loose 
    // most data unless you can tweak your transmits to exactly the best intervals...
    if (xbee.transmit((uint8_t *)ip, &txopts, (uint8_t *)testFrame, TEST_FRAME_SIZE)) {
      // Send "o" to serial for sent Okay
      Serial.print("o");
    } else {
      // "X" if send failed
      Serial.print("X");
    }  
    
    // Start a new line every sixty or so characters
    if ((++split % 60) == 0) {
      Serial.println("");
    }
  }
}


