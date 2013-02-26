/*
 * File                 basic_rx.ino
 *
 * Synopsis             Simple example sketch to demonstrate reception of data
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
#define CONFIG_KEY "whatever"                    // Password

// Create an xbee object to handle things for us
XbeeWifi xbee;

// Helper function converts binary IP address to textual form
// returning said text as an static buffer reference (so non-reentrant)
char * ipstr(uint8_t *ip)
{
  static char ipstrbuf[32];
  sprintf(ipstrbuf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return ipstrbuf;
}

// Receives inbound IP data and outputs to serial
void inbound_ip(unsigned char *data, int len, s_rxinfo *info)
{
  Serial.println();
  Serial.print(F("Inbound data from "));
  Serial.print(ipstr(info->source_addr));
  Serial.print(":");
  Serial.print(info->source_port, DEC);
  Serial.println(":");
  data[len] = 0;
  Serial.println((char *)data);
  Serial.println();
}

// Receives inbound modem status updates and outputs to serial
void inbound_status(uint8_t status)
{
  Serial.println();
  Serial.print(F("Modem status, received code "));
  Serial.print(status, DEC);
  Serial.print(F(" ("));
  switch(status) {
    case XBEE_MODEM_STATUS_RESET                 : Serial.print(F("Reset or power on")); break;
    case XBEE_MODEM_STATUS_WATCHDOG_RESET        : Serial.print(F("Watchdog reset")); break;
    case XBEE_MODEM_STATUS_JOINED                : Serial.print(F("Joined")); break;
    case XBEE_MODEM_STATUS_NO_LONGER_JOINED      : Serial.print(F("No longer joined")); break;
    case XBEE_MODEM_STATUS_IP_CONFIG_ERROR       : Serial.print(F("IP configuration error")); break;
    case XBEE_MODEM_STATUS_S_OR_J_WITHOUT_CON    : Serial.print(F("Send or join without connecting first")); break;
    case XBEE_MODEM_STATUS_AP_NOT_FOUND          : Serial.print(F("AP not found")); break;
    case XBEE_MODEM_STATUS_PSK_NOT_CONFIGURED    : Serial.print(F("Key not configured")); break;
    case XBEE_MODEM_STATUS_SSID_NOT_FOUND        : Serial.print(F("SSID not found")); break;
    case XBEE_MODEM_STATUS_FAILED_WITH_SECURITY  : Serial.print(F("Failed to join with security enabled")); break;
    case XBEE_MODEM_STATUS_INVALID_CHANNEL       : Serial.print(F("Invalid channel")); break;
    case XBEE_MODEM_STATUS_FAILED_TO_JOIN        : Serial.print(F("Failed to join AP")); break;
    default                                      : Serial.print(F("Unknown Status Code")); break;
  }
  Serial.println(F(")"));
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
    // Register for incoming data
    xbee.register_ip_data_callback(inbound_ip);
    xbee.register_status_callback(inbound_status);
    
    Serial.println("XBee found and configured");
  }  
}

// Main run loop
void loop()
{
  // Just keep calling the process method on the xbee object
  // When things happen, it will call our callback routines
  xbee.process();
}


