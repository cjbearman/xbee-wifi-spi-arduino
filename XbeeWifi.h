/*
 * File			xbeewifi.h
 *
 * Synopsis		Support for Xbee Wifi (XB24-WF...) modules through SPI bus API
 *
 * Author		Chris Bearman
 *
 * Version		2.1
 *
 * License		This software is released under the terms of the Mozilla Public License (MPL) version 2.0
 * 			Full details of licensing terms can be found in the "LICENSE" file, distributed with this code
 *
 * Hardware		Minimum connections required:
 *				SPI Bus (MISO, MOSI, SCK)
 *				Chip select line
 *				Attention line
 *			Additional connections desired:
 *				RESET line
 *				DOUT line
 *			
 *			Inclusion of GPIO connections to Xbee RESET and DOUT line allows automatic startup in
 * 			SPI mode, regardless of Xbee configuration. These lines may be omitted from the hardware
 *			desgign but in this case the Xbee must be pre-configured for SPI operation
 *
 *			Take care to operate the XBee correctly at 3.3v. If using a 5v Arduino, ensure necessary
 *			hardware is included to provide 5v <-> 3.3v logic level conversion for all connections
 *
 * Instructions		Create a new instance of this class (XbeeWifi)
 *			Call init method. Must include digital pin numbers for CS (chip select) and ATTN (attention)
 *			lines. Ideally also provide digital pin numbers for DOUT and RESET lines.
 *			Inclusion of DOUT and RESET lines will cause the XBee to automatically reset into SPI mode
 *			upon calling init
 *
 *			Call at_cmd_xxxx methods to issue AT commands
 *			Call register_ methods to register callback functions for asynchronous operations :
 *				IP Data Reception
 *				Modem Status Reception
 *				Network Scan Reception
 *				Remote Data Sample Reception
 *
 *			Call process method frequently to ensure that incoming data and notifications are
 *			processed and dispatched to your callback functions and that the SPI bus doesn't overflow
 *			from excess pending data to be delivered
 *
 *			Callback functions should *NEVER* call any method on this object since this can cause
 *			recursive behaviors and stack overflow (crash).
 * 
 *                      If you've got lots of memory and want a buffered data feed, check out the XbeeWifiBuffered
 *                      class instead. 
 */
#ifndef __XBEEWIFI_H
#define __XBEEWIFI_H
#include <Arduino.h>

// Set up a macro depending on architecture
#ifdef __SAM3X8E__
#define ARCH_SAM
#include "xbee_sam.h"
#else
#define ARCH_ATMEGA
#include "xbee_atmega.h"
#endif

// The compiler is good at optimizing out unused methods, however, certain methods are implicitly used
// to support incoming data that is of unknown type even if that data is then discarded
// If you know you won't be using certain subsystems, then you can uncomment one or more of the following lines
// to further optimize code size

// If you won't be using network scan, uncomment XBEE_OMIT_SCAN
// #define XBEE_OMIT_SCAN

// If you won't be receiving data at all
// #define XBEE_OMIT_RX_DATA

// If you won't be using remote data sampling, uncomment XBEE_OMIT_SAMPLE
// #define XBEE_OMIT_RX_SAMPLE

// If you want to omit support for Xbee compatability mode, uncomment XBEE_OMIT_COMPAT_MODE
// #define XBEE_OMIT_COMPAT_MODE

// Definitions of the various API frame types
#define XBEE_API_FRAME_TX64			0x00
#define XBEE_API_FRAME_REMOTE_CMD_REQ		0x07
#define XBEE_API_FRAME_ATCMD			0x08
#define XBEE_API_FRAME_ATCMD_QUEUED		0x09
#define XBEE_API_FRAME_TX_IPV4			0x20
#define XBEE_API_FRAME_RX64_INDICATOR		0x80
#define XBEE_API_FRAME_REMOTE_CMD_RESP		0x87
#define XBEE_API_FRAME_ATCMD_RESP		0x88
#define XBEE_API_FRAME_TX_STATUS		0x89
#define XBEE_API_FRAME_MODEM_STATUS		0x8A
#define XBEE_API_FRAME_IO_DATA_SAMPLE_RX	0x8F
#define XBEE_API_FRAME_RX_IPV4			0xB0

// Modem Status Values that could be passed to the
// status callback function
#define XBEE_MODEM_STATUS_RESET			0x00
#define XBEE_MODEM_STATUS_WATCHDOG_RESET	0x01
#define XBEE_MODEM_STATUS_JOINED		0x02
#define XBEE_MODEM_STATUS_NO_LONGER_JOINED	0x03
#define XBEE_MODEM_STATUS_IP_CONFIG_ERROR	0x04
#define XBEE_MODEM_STATUS_S_OR_J_WITHOUT_CON	0x82
#define XBEE_MODEM_STATUS_AP_NOT_FOUND		0x83
#define XBEE_MODEM_STATUS_PSK_NOT_CONFIGURED	0x84
#define XBEE_MODEM_STATUS_SSID_NOT_FOUND	0x87
#define XBEE_MODEM_STATUS_FAILED_WITH_SECURITY	0x88
#define XBEE_MODEM_STATUS_INVALID_CHANNEL	0x8A
#define XBEE_MODEM_STATUS_FAILED_TO_JOIN	0x8E

// Definitions of AT commands for addressing
#define XBEE_AT_ADDR_DEST_ADDR			"DL"
#define XBEE_AT_ADDR_IPADDR			"MY"
#define XBEE_AT_ADDR_NETMASK			"MK"
#define XBEE_AT_ADDR_GATEWAY			"GW"
#define XBEE_AT_ADDR_SERNO_HIGH			"SH"
#define XBEE_AT_ADDR_SERNO_LOW			"SL"
#define XBEE_AT_ADDR_NODEID			"NI"
#define XBEE_AT_ADDR_DEST_PORT			"DE"
#define XBEE_AT_ADDR_SERIAL_COM_SERVICE_PORT	"C0"
#define XBEE_AT_ADDR_DEV_TYPE_ID		"DD"
#define XBEE_AT_ADDR_MAX_RF_PAYLOAD_BYTES	"NP"

// Definitions of AT commands for Network parameters
#define XBEE_AT_NET_SSID			"ID"
#define XBEE_AT_NET_TYPE			"AH"
#define XBEE_AT_NET_IPPROTO			"IP"
#define XBEE_AT_NET_ADDRMODE			"MA"
#define XBEE_AT_NET_TCP_TIMEOUT			"TM"

// Options associated with network commands
#define XBEE_NET_TYPE_IBSS_JOINER		0x00
#define XBEE_NET_TYPE_IBSS_CREATOR		0x01
#define XBEE_NET_TYPE_IBSS_INFRASTRUCTURE	0x02

#define XBEE_NET_IPPROTO_UDP			0x00
#define XBEE_NET_IPPROTO_TCP			0x01

#define XBEE_NET_ADDRMODE_DHCP			0x00
#define XBEE_NET_ADDRMODE_STATIC		0x01

// Definition of AT commands for Security
#define XBEE_AT_SEC_ENCTYPE			"EE"
#define XBEE_AT_SEC_KEY				"PK"

// Options associated with security commands
#define XBEE_SEC_ENCTYPE_NONE			0x00
#define XBEE_SEC_ENCTYPE_WPA			0x01
#define XBEE_SEC_ENCTYPE_WPA2			0x02
#define XBEE_SEC_ENCTYPE_WEP			0x03

// Definition of AT commands for RF control
#define XBEE_AT_RF_POWER_LEVEL			"PL"
#define XBEE_AT_RF_CHANNEL			"CH"
#define XBEE_AT_RF_BITRATE			"BR"

// Options associated with RF commands
#define XBEE_RF_BITRATE_AUTO			0x00
#define XBEE_RF_BITRATE_1MBPS			0x01
#define XBEE_RF_BITRATE_2MBPS			0x02
#define XBEE_RF_BITRATE_5MBPS			0x03
#define XBEE_RF_BITRATE_11MBPS			0x04
#define XBEE_RF_BITRATE_6MBPS			0x05
#define XBEE_RF_BITRATE_9MBPS			0x06
#define XBEE_RF_BITRATE_12MBPS			0x07
#define XBEE_RF_BITRATE_18MBPS			0x08
#define XBEE_RF_BITRATE_24MBPS			0x09
#define XBEE_RF_BITRATE_36_BMPS			0x0A
#define XBEE_RF_BITRATE_48_MBPS			0x0B
#define XBEE_RF_BITRATE_54_MBPS			0x0C
#define XBEE_RF_BITRATE_MCS0			0x0D
#define XBEE_RF_BITRATE_MCS1			0x0E
#define XBEE_RF_BITRATE_MCS2			0x0F
#define XBEE_RF_BITRATE_MCS3			0x10
#define XBEE_RF_BITRATE_MCS4			0x11
#define XBEE_RF_BITRATE_MCS5			0x12
#define XBEE_RF_BITRATE_MCS6			0x13
#define XBEE_RF_BITRATE_MCS7			0x14

// Definition of AT commands for diagnostics
#define XBEE_AT_DIAG_FIRMWARE_VERSION		"VR"
#define XBEE_AT_DIAG_HARDWARE_VERSION		"HV"
#define XBEE_AT_DIAG_ASSOC_INFO			"AI"
#define XBEE_AT_DIAG_ACTIVE_SCAN		"AS"
#define XBEE_AT_DIAG_TEMPERATURE		"TP"
#define XBEE_AT_DIAG_CONFIG_CODE		"CK"
#define XBEE_AT_DIAG_SUPPLY_VOLTAGE		"%V"
#define XBEE_AT_DIAG_RSSI			"DB"

// Options associated with diagnostic commands
#define XBEE_DIAG_ASSOC_INSV			0x00
#define XBEE_DIAG_ASSOC_INIT_INPROG		0x01
#define XBEE_DIAG_ASSOC_DISCONNECTING		0x13
#define XBEE_DIAG_ASSOC_SSID_NOT_FOUND		0x22
#define XBEE_DIAG_ASSOC_SSID_NOT_CONFIGURED	0x23
#define XBEE_DIAG_ASSOC_JOIN_FAILED		0x27
#define XBEE_DIAG_ASSOC_PENDING_DHCP		0x41
#define XBEE_DIAG_ASSOC_JOINED_IN_SETUP		0x42
#define XBEE_DIAG_ASSOC_SCANNING		0xFF

// Definition of AT commands for serial control
#define XBEE_AT_SERIAL_API_ENABLE		"AP"
#define XBEE_AT_SERIAL_INTERFACE_DATA_RATE	"BD"
#define XBEE_AT_SERIAL_SERIAL_PARITY		"NB"
#define XBEE_AT_SERIAL_STOP_BITS		"SB"
#define XBEE_AT_SERIAL_PACKET_TIMEOUT		"RO"
#define XBEE_AT_SERIAL_DIO7_CONFIG		"D7"
#define XBEE_AT_SERIAL_DIO6_CONFIG		"D6"

// Options associated with serial control commands
#define XBEE_SERIAL_API_TRANSPARENT		0x00
#define XBEE_SERIAL_API_ENABLE_NOESC		0x01
#define XBEE_SERIAL_API_ENABLE_ESC		0x02

#define XBEE_SERIAL_DATA_RATE_1200		0x00
#define XBEE_SERIAL_DATA_RATE_2400		0x01
#define XBEE_SERIAL_DATA_RATE_4800		0x02
#define XBEE_SERIAL_DATA_RATE_9600		0x03
#define XBEE_SERIAL_DATA_RATE_19200		0x04
#define XBEE_SERIAL_DATA_RATE_38400		0x05
#define XBEE_SERIAL_DATA_RATE_57600		0x06
#define XBEE_SERIAL_DATA_RATE_115200		0x07
#define XBEE_SERIAL_DATA_RATE_230400		0x08

#define XBEE_SERIAL_PARITY_NONE			0x00
#define XBEE_SERIAL_PARITY_EVENT		0x01
#define XBEE_SERIAL_PARITY_ODD			0x02

#define XBEE_SERIAL_STOPBITS_1			0x00
#define XBEE_SERIAL_STOPBITS_2			0x01

#define XBEE_SERIAL_DIO7_DISABLED		0x00
#define XBEE_SERIAL_DIO7_CTS			0x01
#define XBEE_SERIAL_DIO7_DIGITAL_IN		0x03
#define XBEE_SERIAL_DIO7_DIGITAL_OUT_LOW	0x04
#define XBEE_SERIAL_DIO7_DIGITAL_OUT_HIGH	0x05
#define XBEE_SERIAL_DIO7_RS485_TX_ENABLE_HIGH	0x06
#define XBEE_SERIAL_DIO7_RS485_TX_ENABLE_LOW	0x07

#define XBEE_SERIAL_DIO6_DISABLED		0x00
#define XBEE_SERIAL_DIO6_RTS			0x01
#define XBEE_SERIAL_DIO6_DIGITAL_IN		0x03
#define XBEE_SERIAL_DIO6_DIGITAL_OUT_LOW	0x04
#define XBEE_SERIAL_DIO6_DIGITAL_OUT_HIGH	0x05

// Definition of AT commands for IO control
#define XBEE_AT_IO_FORCE_SAMPLE			"IS"
#define XBEE_AT_IO_SAMPLE_RATE			"IR"
#define XBEE_AT_IO_DIGITAL_CHANGE_DETECTION	"IC"
#define XBEE_AT_IO_SAMPLE_FROM_SLEEP_RATE	"IF"
#define XBEE_AT_IO_DIO10_CONFIG			"P0"
#define XBEE_AT_IO_DIO11_CONFIG			"P1"
#define XBEE_AT_IO_DIO12_CONFIG			"P2"
#define XBEE_AT_IO_DOUT_CONFIG			"P3"
#define XBEE_AT_IO_DIN_CONFIG			"P4"
#define XBEE_AT_IO_AD0_DIO0_CONFIG		"D0"
#define XBEE_AT_IO_AD1_DIO1_CONFIG		"D1"
#define XBEE_AT_IO_AD2_DIO2_CONFIG		"D2"
#define XBEE_AT_IO_AD3_DIO3_CONFIG		"D3"
#define XBEE_AT_IO_DIO4_CONFIG			"D4"
#define XBEE_AT_IO_DIO5_CONFIG			"D5"
#define XBEE_AT_IO_DIO8_CONFIG			"D8"
#define XBEE_AT_IO_DIO9_CONFIG			"D9"
#define XBEE_AT_IO_ASSOC_LED_BLINK_TIME		"LT"
#define XBEE_AT_IO_PULLUP			"PR"
#define XBEE_AT_IO_PULL_DIRECTION		"PD"
#define XBEE_AT_IO_ANALOG_VOLTAGE_REF		"AV"
#define XBEE_AT_IO_PWM0_DUTY_CYCLE		"M0"
#define XBEE_AT_IO_PWM1_DUTY_CYCLE		"M1"

// Options associated with IO commands
#define XBEE_IO_DISABLED			0x00
#define XBEE_IO_ENABLED				0x01
#define XBEE_IO_ANALOG_INPUT			0x02
#define XBEE_IO_DIGITAL_INPUT_MONITORED		0x03
#define XBEE_IO_DIGITAL_INPUT_DEFAULT_LOW	0x04
#define XBEE_IO_DIGITAL_INPUT_DEFAULT_HIGH	0x05
#define XBEE_IO_SPI_MISO			0X01
#define XBEE_IO_SPI_ATTN			0x01
#define XBEE_IO_SPI_CLK				0x01
#define XBEE_IO_SPI_SELECT			0x01
#define XBEE_IO_SPI_MOSI			0x01
#define XBEE_IO_ASSOC_INDICATOR			0x01
#define XBEE_IO_SLEEP_REQ			0x01
#define XBEE_IO_ON_SLEED_INDICATOR		0x01
#define XBEE_IO_AVREF_1_25V			0x00
#define XBEE_IO_AVREF_2_5V			0x01

// Definition of AT commands for AT command options
#define XBEE_AT_CMD_OPT_CMD_MODE_TIMEOUT	"CT"
#define XBEE_AT_CMD_OPT_EXIT_COMMAND_MODE	"CN"
#define XBEE_AT_CMD_OPT_GUARD_TIMES		"GT"
#define XBEE_AT_CMD_OPT_CMD_MODE_CHAR		"CC"

// Definition of AT commands for sleep commands
#define XBEE_AT_SLEEP_MODE			"SM"
#define XBEE_AT_SLEEP_PERIOD			"SP"
#define XBEE_AT_SLEEP_OPTIONS			"SO"
#define XBEE_AT_WAKE_HOST			"WH"
#define XBEE_AT_WAKE_TIME			"ST"

// Options associated with sleep commands
#define XBEE_SLEEP_NO_SLEEP			0x00
#define XBEE_SLEEP_PIN_SLEEP			0x01
#define XBEE_SLEEP_CYCLIC_SLEEP			0x04
#define XBEE_SLEEP_CYCLIC_SLEEP_PIN_WAKE	0x05

// Definition of AT commands for execution
#define XBEE_AT_EXEC_APPLY_CHANGES		"AC"
#define XBEE_AT_EXEC_WRITE			"WR"
#define XBEE_AT_EXEC_RESTORE_DEFAULTS		"RE"
#define XBEE_AT_EXEC_SOFTWARE_RESET		"FR"
#define XBEE_AT_EXEC_NETWORK_RESET		"NR"

// This structure is used with the IP data callback to
// report information about the incoming IP data
typedef struct {
	uint8_t source_addr[4];		// Address from which the data originated	
	uint16_t source_port;		// Port from which the data originated
	uint16_t dest_port;		// Port on which the data arrived. If 0xBEE, data was received using app service
	uint8_t protocol;		// XBEE_NET_IPPROTO_UDP / TCP
	uint16_t sequence;		// Segment number
	uint16_t total_packet_length;	// Total length of the incoming packet
	uint16_t current_offset;	// Current offset within the incoming packet of this segment
	bool final;			// True for the final segment of this packet
	bool checksum_error;		// Checksum indication flag
} s_rxinfo;

// Note that due to buffer size restrictions, an incoming data packet (of up to 1400 bytes length)
// will be delivered in multiple calls to the ip data reception callback
// The sequence number will be the same for all calls for a given packet and then incremented
// for the next packet
// A checksum error will only be flagged (true) on the last given call for a packet / sequence


// This structure is used to provide transmission options when transmiting IP data
typedef struct {
	uint16_t dest_port;
	uint16_t source_port;
	uint8_t protocol;		// XBEE_NET_IPPROTO_UDP / TCP
	bool leave_open;
} s_txoptions;

// This packet is used for the sample reception callback to provide sample data
typedef struct {
	uint8_t source_addr[4];
	uint16_t digital_mask;
	uint8_t analog_mask;
	uint16_t digital_samples;
	uint16_t analog_samples;
} s_sample;

class XbeeWifi
{
	public:

	// Constructor
	XbeeWifi();

	// Must call before any other functions to initialize the xbee
	// Provide cs (required), atn (required) pins and reset (optional), dout (optional)
	// If reset and dout are not connected then the module will not be reset / forced into SPI mode
	// on init
	bool init(uint8_t cs, uint8_t atn, uint8_t reset = 0xFF, uint8_t dout = 0xFF);

	// Send AT command with data of various possible forms
	// atxx = Two digit string (i.e. "XY" would indicate ATXY command)
	// Set queued = true to delay execution until applied (per spec)
	// But note that queued AT commands are executed without confirmation
	// so errors will not be reported
#ifndef XBEE_OMIT_LOCAL_AT
	bool at_cmd_raw(const char *atxx, uint8_t *buffer, int len, bool queued = false);
	bool at_cmd_str(const char *atxx, const char *buffer, bool queued = false);
	bool at_cmd_byte(const char *atxx, uint8_t byte, bool queued = false);
	bool at_cmd_short(const char *atxx, uint16_t twobyte, bool queued = false);
	bool at_cmd_noparm(const char *atxx, bool queued = false);

	// Query an AT parameter
	// Provide a buffer (parmval) and it's length (maxlen)
	// Will return parmlen indicating the number of bytes read back into the buffer
	bool at_query(const char *atxx, uint8_t *parmval, int *parmlen, int maxlen);
#endif

	// Equivalent AT set / get methods for targetting a remote device
	// as described by the IP address (of form uint8_t ip[4])
	// Set apply=true (default) to cause the remote device to immediately apply the command
	// Use apply=false to defer application until subsequent apply operation
#ifndef XBEE_OMIT_REMOTE_AT
	bool at_remcmd_raw(uint8_t *ip, const char *atxx, uint8_t *buffer, int len, bool apply = true);
	bool at_remcmd_str(uint8_t *ip, const char *atxx, const char *buffer, bool apply = true);
	bool at_remcmd_byte(uint8_t *ip, const char *atxx, uint8_t byte, bool apply = true);
	bool at_remcmd_short(uint8_t *ip, const char *atxx, uint16_t twobyte, bool apply = true);
	bool at_remcmd_noparm(uint8_t *ip, const char *atxx, bool apply = true);
	bool at_remquery(uint8_t *ip, const char *atxx, uint8_t *parmval, int *parmlen, int maxlen);
#endif

	// Provide a reference of the last modem status
	volatile uint8_t last_status;

	// The following functions define callbacks for asynchronous data delivery
	// To stop delivery (and discard data) of any given type
	// set the associated callback to it's default (NULL)

	// Register a callback to receive incoming IP data
	// Callback should be of following form:
	//	void my_callback(uint8_t *data, int len, s_rxinfo *info)
#ifndef XBEE_OMIT_RX_DATA
	void register_ip_data_callback(void (*func)(uint8_t *, int, s_rxinfo *));
#endif

	// Register callback for modem status indications
	// Callback should be of following form:
	//	void my_callback(uint8_t status)
	void register_status_callback(void (*func)(uint8_t));

	// Register a callback for network scan returns
	// Callback should be of following form:
	//	void my_callback(uint8_t encryption_mode, int rssi, char *ssid)
#ifndef XBEE_OMIT_SCAN
	void register_scan_callback(void (*func)(uint8_t, int, char *));
#endif

	// Register a callback for remote data sample reception
	// Callback should be of following form:
	//	void my_callback(s_sample *sampledata)
#ifndef XBEE_OMIT_RX_SAMPLE
	void register_sample_callback(void (*func)(s_sample *));
#endif

	// Call as often as possible to check for inbound data
	// Will trigger register_ip_data_callback to receive and process any inbound data
	void process(bool rx_one_packet_only = false);

	// Transmit data to an endpoint
	// ip should be the binary form (uint8_t[4]) IP address
	// addr should be transmission options indicating port assignments and such. May be null when useAppService is true
	// data and len provide the data to be transmitted
	// Leave confirm=true to block for confirmation of delivery (TCP)
	// Set useAppService to true to use the compatability mode (64bit) app service to transmit the data to the 0xBEE port
	bool transmit(const uint8_t *ip, s_txoptions *addr, uint8_t *data, int len, bool confirm = true, bool useAppService = false);

	// Initiate a network scan
	// Will cause the registered scan callback to be called with information about APs that are heard
	// Causes network reset! Connection will be downed and will need to be reconfigured (or xBee reset if appropriate)
#ifndef XBEE_OMIT_SCAN
	bool initiateScan();
#endif

	protected:
#ifndef XBEE_OMIT_RX_data
	virtual void dispatch(uint8_t *data, int len, s_rxinfo *info);
#endif

	private:
	// This is the actual method that does all AT processing
	bool at_cmd(const char *atxx, const uint8_t *parmval, int parmlen, void *returndata, int *returnlen, bool queued);

	// And this is the equivalent for remote commands
	bool at_remcmd(uint8_t ip[4], const char *atxx, const uint8_t *parmval, int parmlen, void *returndata, int *returnlen, bool apply);

	// Read from SPI, single byte
	uint8_t read();

	// Write to SPI buffer of given length
	void write(const uint8_t *data, int len);

	// Receive an API frame, providing type, length and data to a max of bufsize
	// If bufsize is < len then data will be truncated
	int rx_frame(uint8_t *frame_type, unsigned int *len, uint8_t *data, int bufsize, unsigned long atn_wait_ms = 5000L, bool return_status = false, bool single_ip_rx_only = false);

	// Transmit an API frame of specified type, length and data
	void tx_frame(uint8_t type, unsigned int len, uint8_t *data);

	// Start / End SPI operation
	void spiStart();
	void spiEnd();

	// Perform the actual TX/RX on SPI bus
	uint8_t rxtx(uint8_t data);

	// Read and dispatch an inbound IP packet
#ifndef XBEE_OMIT_RX_DATA
	void rx_ip(unsigned int len, uint8_t frame_type);
#endif

	// Read and dispatch inbound sample packet
#ifndef XBEE_OMIT_RX_SAMPLE
	void rx_sample(unsigned int len);
#endif

	// Read and dispatch an inbound modem status packet
	void rx_modem_status(unsigned int len);

	// Wait for ATN to be asserted to a maximum period (millisecs)
	// Returns true on proper assert, false on timeout
	bool wait_atn(unsigned long int max_millis = 5000L);

	// Flush all content from the incoming SPI buffer (i.e. read until ATN de-asserts)
	void flush_spi();

	// Our internal records of our pin assignments
	uint8_t pin_cs;
	uint8_t pin_atn;
	uint8_t pin_dout;
	uint8_t pin_reset;
#ifdef ARCH_SAM
	uint8_t pin_cs_actual;
	uint8_t spi_ch;
#endif

	// RX seq
#ifndef XBEE_OMIT_RX_DATA
	uint16_t rx_seq;
#endif

	// The function pointer for IP callback
#ifndef XBEE_OMIT_RX_DATA
	void (*ip_data_func)(uint8_t *, int, s_rxinfo *);
#endif

	// The function pointer for modem status callback
	void (*modem_status_func)(uint8_t);

	// The function pointer for scan callback
#ifndef XBEE_OMIT_SCAN
	void (*scan_func)(uint8_t, int, char *);
#endif

	// The function pointer for sample callback
#ifndef XBEE_OMIT_RX_SAMPLE
	void (*sample_func)(s_sample *);
#endif

	// The next ATID to use for sequencing AT comamnd responses
	uint8_t next_atid;

#ifndef XBEE_OMIT_SCAN
	// Handles incoming active scan data (AT responses to AS command)
	void handleActiveScan(uint8_t *buf, int len);
#endif

	// Track RX callback depth
	uint8_t callback_depth;

#ifdef ARCH_ATMEGA
	// To be nice about things, we reset SPCR after using it, copy of SPCR held here
	// Ditto SPSR - which is in fact just the SPI2X bit which is the only writable bit here
	uint8_t spcr_copy;
	uint8_t spsr_copy;
#endif

	// True when we have the Xbee Chip Select asserted
	bool spiRunning;

	// True to prevent SPI bus from being de-selected by endSpi function
	// in some cases
	bool spiLocked;

};

#ifndef XBEE_OMIT_RX_DATA
// The XbeeWifiBuffered class is a derivative class that provides
// buffered access to the incoming IP data
// 
// This is problematic, which is why the main XbeeWifi class does not
// buffer data but dispatches it asynchronously via callback
//
// But on larger Arduinos (particuarly the Due) you could use this
// class and assign some memory as a buffer into which incoming packets
// are captured and then read them using the "available", "read" methods
//
// Strictly speaking if you want to do this you're probably better off
// usign the UART on the Xbee to read data instead of SPI
// Still - there might be a use case for this
class XbeeWifiBuffered : public XbeeWifi
{
	public:
	// Must provide a desired buffer size when constructing
	// If at any time incoming data is in excess of this buffer size, you will lose data
	// This is why you should probably be using the Xbee serial service instead of SPI
	XbeeWifiBuffered(uint16_t bufsize);

	// Destructor since we use dynamic allocation
	~XbeeWifiBuffered();

	// Returns the number of available bytes
	bool available();

	// Reads the next byte. Always returns 0 if no bytes were available
	uint8_t read();

	// Peeks the next byte. Always returns 0 if no bytes are in the buffer
	uint8_t peek();

	// Flush all items out of the buffer
	void flush();

	// Returns true if a buffer overrun has occurred (and resets the overrun
	// state to false unless reset is marked false)
	bool overran(bool reset = true);

	protected: 
	// We will rewrite the XbeeWifi::dispatch method to capture the incoming data
	// into the FIFO buffer
	virtual void dispatch(uint8_t *data, int len, s_rxinfo *info);

	private:
	// Move register_ip_data_callback to private space
	// This is not callable from the buffered version of the class
	void register_ip_data_callback(void (*func)(uint8_t *, int, s_rxinfo *));

	// The buffer
	uint8_t *buffer;

	// Declared size of the buffer
	uint16_t bufsize;

	// Head of the buffer
	uint16_t head;

	// Tail of the buffer
	uint16_t tail;

	// The number of bytes currently in the buffer
	uint16_t size;

	// Flag when a buffer overrun has occurred here
	bool buffer_overrun;
};
#endif

#endif /* __XBEE_WIFI_H_ */
