/*
 * File                 interactive.ino
 *
 * Synopsis             Comprehensive interactive Xbee demo
 *
 * Author               Chris Bearman
 *
 * Instructions         You will need a microprocessor with a decent amount of flash to fit this sketch
 *                      since it contains many progmem strings and much functionality.
 *       
 *                      Download it and connect ot serial monitor at 57600.
 *
 *                      Type help for information
 * 
 *                      Use the set command to set parameters (i.e. set ipmode dhcp)
 *
 *                      Received data appears on screen as it arrives.
 *                      Transmit data to configured endpoint with transmit command.
 *                      Parameters are preserved in eeprom so that configuration is retained (excluding Arduino Due)
 *
 * Version              2.0
 */

#include <XbeeWifi.h>
#ifndef ARCH_SAM
#include <avr/eeprom.h>
#endif

// Number of bytes to use for general purpose buffers
#define MAX_BUF 512

// Definitions for endpoint types
#define ENDPOINT_RAW 0
#define ENDPOINT_APP 1

// Defintions for what pins we're using for hwat
#define XBEE_RESET 15
#define XBEE_ATN 2
#define XBEE_SELECT SS
#define XBEE_DOUT 23
#define PIN_LED 7

// Misc definitions
#define CONFIGURED 0xBEEE
#define MAX_SSID 32
#define MAX_PSK 63
#define MAX_IP 4

// Structure to hold our configuration
typedef struct {
  uint16_t config_marker;
  uint8_t ssid[MAX_SSID + 1];
  uint8_t psk[MAX_PSK +1];
  uint8_t encmode;
  uint8_t ipmode;
  uint8_t ip[MAX_IP];
  uint8_t nm[MAX_IP];
  uint8_t gw[MAX_IP];
  uint16_t port;
  uint8_t dest_ip[MAX_IP];
  uint16_t dest_port;
  uint8_t payload;
  uint8_t endpoint;
} s_config;

// Current configuration
s_config config;

// Xbee object
XbeeWifi xbee;

// Display help
void help()
{
  Serial.println(F("Help Information..."));
  Serial.println(F("To show current operational status: show status"));
  Serial.println(F("To view current configuration: show config"));
  Serial.println(F("To change any configuration item: set <item> <value>"));
  Serial.println(F("To restore to default configuration: defaults"));
  Serial.println(F("To initiate scan: scan (Must not be connected)"));
  Serial.println(F("To reset XBee: reset"));
  Serial.println(F("To transmit a phrase to configured endpoint: tx <phrase>"));
  Serial.println();
}

// Print a binary IP to serial
void print_ip(uint8_t *ip, bool lf=true)
{
  Serial.print(ipstr(ip));
  if (lf) Serial.println("");
}

// Read input from serial
// If reprompt = true indicates that we don't want to read anything but should
// redisplay the prompt and currently pending line contents since something poluted the display
// Call whenever serial reports input.
// Returns NULL unless something got read, in which case an internal buffer is returned with the input string
char *read_input(bool reprompt = false)
{
  static char last[MAX_BUF];
  static char current[MAX_BUF];
  static int pos = 0;
  
  if (reprompt) {
    Serial.print("->");
    for (int i = 0 ; i < pos; i++) {
      Serial.print((char)current[i]);
    }
    return NULL;
  }
  
  if (Serial.available()) {
    char c = Serial.read();
    if ((c == '\n' || c == '\r') && pos) {
      memcpy(last, current, MAX_BUF);
      last[pos] = 0;
      pos = 0;
      
      Serial.print("\r\n");
      
      return last;
    }
    
    if (c == 0x08 /* backspace */ && pos > 0) {
      pos--;
      Serial.print(c); 
      Serial.print(' ');
      Serial.print(c);
    }

    if (pos >= MAX_BUF - 1) {
      Serial.println(F("Sorry - input too long - trashed"));
      pos = 0;
    } else {
      if (c >= 32) {
        current[pos++] = c;
        Serial.print(c);
      }
    }
  }
  return NULL;
}

// Write configuration to EEPROM
void write_configuration() {
#ifndef ARCH_SAM
  eeprom_write_block(&config, 0, sizeof(s_config));
#endif
}

// Initialize configuration in memory AND EEPROM
void init_configuration() {
  memset(&config, 0, sizeof(s_config));
  
  // Set key initial values
  config.config_marker = CONFIGURED;
  config.ipmode = XBEE_NET_ADDRMODE_DHCP;
  config.encmode = XBEE_SEC_ENCTYPE_NONE;
  strcpy((char *) config.ssid, "mynetwork");
  config.port = 12345;
  config.dest_port = 12345;
  config.ip[0] = config.gw[0] = config.dest_ip[0] = 192;
  config.ip[1] = config.gw[1] = config.dest_ip[1] = 168;
  config.ip[2] = config.gw[2] = config.dest_ip[2] = 1;
  config.ip[3] = 254;
  config.dest_ip[3] = 253;
  config.gw[3] = 1;
  config.nm[0] = config.nm[1] = config.nm[2] = 255;
  config.payload = XBEE_NET_IPPROTO_TCP;
  config.endpoint = ENDPOINT_RAW;  // Raw IP

  // Write to EEPROM
  write_configuration();
}

// Load configuration from EEPROM to memory
// If EEPROM memory did not hold our config, then an initial configuration is deployed
void load_configuration()
{ 
#ifdef ARCH_SAM
  init_configuration();
#else
  eeprom_read_block(&config, 0, sizeof(s_config));
  if (config.config_marker != CONFIGURED) init_configuration();
#endif
}

// Apply in memory configuration to Xbee
void apply_configuration()
{
  if (!xbee.at_cmd_byte(XBEE_AT_NET_TYPE, XBEE_NET_TYPE_IBSS_INFRASTRUCTURE)) Serial.println(F("Set Infrastructure Mode Failed"));
  if (!xbee.at_cmd_byte(XBEE_AT_NET_IPPROTO, config.payload)) Serial.println(F("Set payload Failed"));

  if (!xbee.at_cmd_str(XBEE_AT_NET_SSID, (char *)config.ssid)) Serial.println(F("Set SSID Failed"));
  if (config.ipmode == XBEE_NET_ADDRMODE_DHCP) {
    if (!xbee.at_cmd_byte(XBEE_AT_NET_ADDRMODE, XBEE_NET_ADDRMODE_DHCP)) Serial.println(F("Set DHCP Failed"));
  } else {
    if (!xbee.at_cmd_byte(XBEE_AT_NET_ADDRMODE, XBEE_NET_ADDRMODE_STATIC)) Serial.println(F("Set STATIC Failed"));
    if (!xbee.at_cmd_str(XBEE_AT_ADDR_IPADDR, ipstr(config.ip))) Serial.println(F("Set IP Failed"));
    if (!xbee.at_cmd_str(XBEE_AT_ADDR_NETMASK, ipstr(config.nm))) Serial.println(F("Set Netmask Failed"));
    if (!xbee.at_cmd_str(XBEE_AT_ADDR_GATEWAY, ipstr(config.gw))) Serial.println(F("Set Gateway Failed"));
  }
  if (!xbee.at_cmd_short(XBEE_AT_ADDR_SERIAL_COM_SERVICE_PORT, config.port)) Serial.println(F("Set Port Failed"));
  if (!xbee.at_cmd_byte(XBEE_AT_SEC_ENCTYPE, config.encmode)) Serial.println(F("Set Encryption Mode Failed"));
  if (!xbee.at_cmd_str(XBEE_AT_SEC_KEY, (char *)config.psk)) Serial.println(F("Set Encryption Key Failed"));
}

// Setup algorithm
void setup()
{
  // Initialize serial
  Serial.begin(57600);
  
  // Initiailize XBEE
  if (!xbee.init(XBEE_SELECT, XBEE_ATN, XBEE_RESET, XBEE_DOUT)) {
    // Oops - failed
    // Flash the LED in three pulse groups
    pinMode(PIN_LED, OUTPUT);
    Serial.println(F("Failed to find the Xbee?"));
    while (true) {
      for(int i = 0; i < 3; i ++) {
        digitalWrite(PIN_LED, HIGH);
        delay(100);
        digitalWrite(PIN_LED, LOW);
        delay(100);
      }
      delay(400);
    }
  }
  
  // Load the EEPROM configuration
  load_configuration();
  
  // Apply it to xbee
  apply_configuration();
  
  // Welcome Message
  Serial.println(F("XBee Wifi Simple Controller Example"));
  Serial.println(F("Type help for instructions"));
  
  // Register callbacks
  xbee.register_ip_data_callback(ip_data_inbound);
  xbee.register_status_callback(status_inbound);
  xbee.register_scan_callback(scan_inbound);
  xbee.register_sample_callback(sample_inbound);
  
  // Display initial prompt
  Serial.print("->");
}

// Inbound sample data, output to serial
void sample_inbound(s_sample *sample)
{
  Serial.println();
  Serial.print(F("Sample Inbound from IP "));
  Serial.print(ipstr(sample->source_addr));
  Serial.print(F(": DM=0x"));
  Serial.print(sample->digital_mask, HEX);
  Serial.print(F(", DS=0x"));
  Serial.print(sample->digital_samples, HEX);
  Serial.print(F(", AM=0x"));
  Serial.print(sample->analog_mask, HEX);
  Serial.print(F(", AS=0x"));
  Serial.println(sample->analog_samples, HEX);
  read_input(true);

}

// Inbound scan data, output to serial
void scan_inbound(uint8_t encryption_mode, int rssi, char *ssid)
{
  Serial.println();
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
  read_input(true);
}

// Inbound status information, output to serial
void status_inbound(uint8_t status)
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
  read_input(true);
}

// Inbound IP data - output to serial
void ip_data_inbound(unsigned char *data, int len, s_rxinfo *info)
{
  Serial.println();
  Serial.print(F("Inbound data from "));
  Serial.print(ipstr(info->source_addr));
  Serial.print(":");
  Serial.print(info->source_port, DEC);
  Serial.print(" ");
  Serial.print(info->current_offset, DEC);
  Serial.print("/");
  Serial.print(info->total_packet_length, DEC);
  Serial.println(":");
  data[len] = 0;
  Serial.println((char *)data);
  Serial.println();
  
  // Cause the prompt and current input line to redisplay
  read_input(true);
}

// Convert IP to string and return as internal buffer
char * ipstr(uint8_t *ip)
{
  static char ipstrbuf[32];
  sprintf(ipstrbuf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return ipstrbuf;
}


// Parse string as IP and write to config parameter as 4 byte binary
bool parse_as_ip(char *value, uint8_t *config)
{
  for (int i = 0; i < strlen(value); i ++) {
    if (!(value[i] == '.' || isdigit(value[i]))) return false;
  }
  char *o1 = strtok(value, ".");
  char *o2 = strtok(NULL, ".");
  char *o3 = strtok(NULL, ".");
  char *o4 = strtok(NULL, ".");
  
  if (!o1 || !o2 || !o3 || !o4) return false;
  
  config[0] = atoi(o1);
  config[1] = atoi(o2);
  config[2] = atoi(o3);
  config[3] = atoi(o4);
  
  return true;
  
}

// Show configuration
void show_config()
{
    Serial.print(F("ipmode           : "));
    Serial.print(config.ipmode ? F("static") : F("dhcp"));
    Serial.println(F("   (dhcp, static)"));
    if (config.ipmode) {
      Serial.print(F("ipaddr           : "));
      print_ip(config.ip);
      Serial.print(F("netmask          : "));
      print_ip(config.nm);
      Serial.print(F("gateway          : "));
      print_ip(config.gw);
    }
    Serial.print(F("port             : "));
    Serial.println(config.port, DEC);
    Serial.print(F("destip           : "));
    print_ip(config.dest_ip);
    Serial.print(F("destport         : "));
    Serial.println(config.dest_port, DEC);
    Serial.print(F("payload          : "));
    Serial.print(config.payload == XBEE_NET_IPPROTO_TCP ? F("tcp") : F("udp"));
    Serial.println(F("   (tcp, udp)"));
    Serial.print(F("endpoint         : "));
    Serial.print(config.endpoint == ENDPOINT_RAW ? F("raw") : F("app"));
    Serial.println(F("   (raw, app)"));
    Serial.print(F("ssid             : "));
    Serial.println((char *)config.ssid);
    Serial.print(F("encryption       : "));
    switch(config.encmode) {
      case XBEE_SEC_ENCTYPE_NONE  : Serial.print(F("none")); break;
      case XBEE_SEC_ENCTYPE_WEP   : Serial.print(F("wep")); break;
      case XBEE_SEC_ENCTYPE_WPA   : Serial.print(F("wpa")); break;
      case XBEE_SEC_ENCTYPE_WPA2  : Serial.print(F("wpa2")); break;
    }
    Serial.println(F("   (none, wep, wpa, wpa2)"));
    if (config.encmode != XBEE_SEC_ENCTYPE_NONE) {
      Serial.print(F("password         : "));
      Serial.println((char *)config.psk);
    }
    Serial.println();
}

// When we get a "set" command, parse it to update input  
void update_config(char *input)
{
    bool apply_needed = false;
    char *cmd = strtok(input, " ");
    char *parm = strtok(NULL, " ");
    char *value = strtok(NULL, " ");
    
    if (strcmp(cmd, "set") == 0 && parm && value) {
      bool valid = false;
      if (strcmp(parm, "ipmode") == 0) {
        if (strcmp(value, "dhcp") == 0) {
          config.ipmode = XBEE_NET_ADDRMODE_DHCP;
          apply_needed = valid = true;
        } else if (strcmp(value, "static") == 0) {
          config.ipmode = XBEE_NET_ADDRMODE_STATIC;
          apply_needed = valid = true;
        }
      } else if (strcmp(parm, "ipaddr") ==0) {
        if (parse_as_ip(value, config.ip)) apply_needed = valid = true;
      } else if (strcmp(parm, "gateway") ==0) {
        if (parse_as_ip(value, config.gw)) apply_needed = valid = true;
      } else if (strcmp(parm, "netmask") ==0) {
        if (parse_as_ip(value, config.nm)) apply_needed = valid = true;
      } else if (strcmp(parm, "destip") ==0) {
        if (parse_as_ip(value, config.dest_ip)) valid = true;
      } else if (strcmp(parm, "port") ==0) {
        config.port = atoi(value);
        apply_needed = valid = true;
      } else if (strcmp(parm, "destport") ==0) {
        config.dest_port = atoi(value);
        valid = true;
      } else if (strcmp(parm, "payload") ==0) {
        if (strcmp(value, "tcp") == 0) {
          config.payload = XBEE_NET_IPPROTO_TCP;
          apply_needed = valid = true;
        } else if (strcmp(value, "udp") == 0) {
          config.payload = XBEE_NET_IPPROTO_UDP;
          apply_needed = valid = true;
        }
      } else if (strcmp(parm, "endpoint") == 0) {
        if (strcmp(value, "raw") == 0) {
          config.endpoint = ENDPOINT_RAW;
          valid = true;
        } else if (strcmp(value, "app") == 0) {
          config.endpoint = ENDPOINT_APP;
          valid = true;
        }
      } else if (strcmp(parm, "ssid") ==0) {
        strncpy((char *)config.ssid, value, MAX_SSID);
        apply_needed = valid = true;
      } else if (strcmp(parm, "password") ==0) {
        strncpy((char *)config.psk, value, MAX_PSK);
        apply_needed = valid = true;
      } else if (strcmp(parm, "encryption") ==0) {
        if (strcmp(value, "none") == 0) {
          config.encmode = XBEE_SEC_ENCTYPE_NONE;
          apply_needed = valid = true;
        } else if (strcmp(value, "wep") == 0) {
          config.encmode = XBEE_SEC_ENCTYPE_WEP;
          apply_needed = valid = true;
        } else if (strcmp(value, "wpa") == 0) {
          config.encmode = XBEE_SEC_ENCTYPE_WPA;
          apply_needed = valid = true;
        } else if (strcmp(value, "wpa2") == 0) {
          config.encmode = XBEE_SEC_ENCTYPE_WPA2;
          apply_needed = valid = true;
        }
      }
      if (!valid) {
        Serial.println(F("huh?"));
      } else {
        if (apply_needed) {
          write_configuration();
          apply_configuration();
        } else {
          write_configuration();
        }
        Serial.println("ok");
      }
    } else {
      Serial.println(F("huh?"));
    }
}

// Show system status by querying the xbee
void show_status()
{
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
  
  Serial.println();

}

// Main run loop
void loop()
{
  // Read input and if we get something, act upon it
  char *input = read_input();
  if (input) {
    if (strncmp(input, "set ", 4) == 0) {
      update_config(input);
    } else if (strcmp(input, "show config") == 0) {
      show_config();
    } else if (strcmp(input, "reset") == 0) {
      Serial.println("ok");
      xbee.init(XBEE_SELECT, XBEE_ATN, XBEE_RESET, XBEE_DOUT);
      apply_configuration();
    } else if (strcmp(input, "defaults") == 0) {
      Serial.println("ok");
      xbee.init(XBEE_SELECT, XBEE_ATN, XBEE_RESET, XBEE_DOUT);
      init_configuration();
      apply_configuration();
    } else if (strcmp(input, "show status") == 0) {
      show_status();
    } else if (strncmp(input, "tx ", 3) == 0) {
      s_txoptions txopt;
      txopt.dest_port = config.dest_port;
      txopt.source_port = config.port;
      txopt.protocol = config.payload;
      txopt.leave_open = true;
      Serial.print("Transmitting...");
      int res = xbee.transmit(config.dest_ip, &txopt, (uint8_t *)input +3, strlen((char *)input +3), true, config.endpoint == ENDPOINT_APP ? true : false);
      Serial.println(res ? "ok" : "fail");
    } else if (strcmp(input, "scan") == 0) {
      Serial.println(F("ok (recommend reset after scan since you will be disconnected"));
      if (!xbee.initiateScan()) Serial.println(F("Failed to start scan, error"));
    } else if (strcmp(input, "help") == 0) {
      help();
    } else {
      Serial.println(F("huh?"));
    }
    
    // Redisplay prompt
    Serial.println();
    Serial.print("->");
  }
  
  // Process the xbee SPI bus
  xbee.process();
}

