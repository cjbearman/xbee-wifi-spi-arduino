/*
 * File                 query_xbee.ino
 *
 * Synopsis             Simple example sketch to demonstrate AT query of Xbee
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
#define CONFIG_PAYLOAD XBEE_NET_IPPROTO_TCP      // Expecting TCP connections
#define CONFIG_PORT 12350                        // To this port
#define CONFIG_ENCMODE XBEE_SEC_ENCTYPE_WPA2     // Network type is WPA2 encrypted
#define CONFIG_SSID "Example"                    // SSID
#define CONFIG_KEY "password"                    // Password

// Create an xbee object to handle things for us
XbeeWifi xbee;

// Track 5 second status reporting intervals
unsigned long next_status;

// Helper function converts binary IP address to textual form
// returning said text as an static buffer reference (so non-reentrant)
char * ipstr(uint8_t *ip)
{
  static char ipstrbuf[32];
  sprintf(ipstrbuf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return ipstrbuf;
}

// Interrogate Xbee for basic network status
void show_status()
{
  Serial.println();

  uint8_t buf[XBEE_BUFSIZE];
  int len;
  if (xbee.at_query(XBEE_AT_ADDR_IPADDR, buf, &len, XBEE_BUFSIZE - 1)) {
    buf[len] = 0;
    Serial.print(F("IP Address      : "));
    Serial.println((char *)buf);
  } else {
    Serial.println(F("IP Query fail"));
  }
  if (xbee.at_query(XBEE_AT_ADDR_NETMASK, buf, &len, XBEE_BUFSIZE - 1)) {
    buf[len] = 0;
    Serial.print(F("Netmask         : "));
    Serial.println((char *)buf);
  } else {
    Serial.println(F("NM Query fail"));
  }
  if (xbee.at_query(XBEE_AT_ADDR_GATEWAY, buf, &len, XBEE_BUFSIZE - 1)) {
    buf[len] = 0;
    Serial.print(F("Gateway         : "));
    Serial.println((char *) buf);
  } else {
    Serial.println(F("GW Query fail"));
  }

  if (xbee.at_query(XBEE_AT_DIAG_ASSOC_INFO, buf, &len, 1)) {
    Serial.print(F("Operation State : "));
      switch(buf[0]) {
        case XBEE_DIAG_ASSOC_INSV                  : Serial.println(F("In Service")); break;
        case XBEE_DIAG_ASSOC_INIT_INPROG           : Serial.println(F("Init In Progress")); break;
        case XBEE_DIAG_ASSOC_DISCONNECTING         : Serial.println(F("Disconnecting")); break;
        case XBEE_DIAG_ASSOC_SSID_NOT_FOUND        : Serial.println(F("SSID Not Found")); break;
        case XBEE_DIAG_ASSOC_SSID_NOT_CONFIGURED   : Serial.println(F("SSID Not Configured")); break;
        case XBEE_DIAG_ASSOC_JOIN_FAILED           : Serial.println(F("Join Failed")); break;
        case XBEE_DIAG_ASSOC_PENDING_DHCP          : Serial.println(F("Pending DHCP")); break;
        case XBEE_DIAG_ASSOC_JOINED_IN_SETUP       : Serial.println(F("Joined, Setup In Progress")); break;
        case XBEE_DIAG_ASSOC_SCANNING              : Serial.println(F("Scanning")); break;
        default                                    : Serial.println(F("Unknown Code")); break;
      }
  } else {
    Serial.println(F("STATE query fail"));
  }
}

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
    result &= xbee.at_cmd_byte(XBEE_AT_NET_IPPROTO, CONFIG_PAYLOAD);
    result &= xbee.at_cmd_str(XBEE_AT_NET_SSID, CONFIG_SSID);
    result &= xbee.at_cmd_byte(XBEE_AT_NET_ADDRMODE, XBEE_NET_ADDRMODE_DHCP);
    result &= xbee.at_cmd_short(XBEE_AT_ADDR_SERIAL_COM_SERVICE_PORT, CONFIG_PORT);
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
    next_status = millis();
  }  
}

// Main run loop
void loop()
{
  // Every two seconds, show status
  if (millis() >= next_status) {
    show_status();
    next_status = millis() + 2000;
  }
  xbee.process();
}


