/*
 * File			xbeewifi.cpp
 *
 * Synopsis		Support for Xbee Wifi (XB24-WF...) modules through SPI bus
 *
 * Author		Chris Bearman
 *
 * Version		1.0
 *
 * License		This software is released under the terms of the Mozilla Public License (MPL) version 2.0
 * 			Full details of licensing terms can be found in the "LICENSE" file, distributed with this code
 *
 * Instructions		See xbeewifi.h
 */
#include "XbeeWifi.h"
#include <Arduino.h>

// Debugging...
// Uncomment the following line to enable debug output to serial
// When using debug, caller is responsible for initializing Serial interface

#define XBEE_ENABLE_DEBUG

// Debug functions
#ifdef XBEE_ENABLE_DEBUG

// Debug is enabled, XBEE_DEBUG is a simple macro that just inserts the code within it's parameter
#define XBEE_DEBUG(x) (x)

// Logic to allow us to do inline embedding of PROGMEM strings, for convenience and ease of use
class __FlashStringHelper;
#define F(str) reinterpret_cast<__FlashStringHelper *>(PSTR(str))

#else

// Debug is not enabled, the XBEE_DEBUG becomes a NOP macro that essentially discards it's parameter
#define XBEE_DEBUG(x)
#endif

// For consistent use - definition of SPCR settings for SPI bus
// Add | (1 << SPR0) | (1 < SPR1)     to slow down the SPI bus for easier monitoring if needed
#define XBEE_SPCR (1 << SPE) | (1 << MSTR) 

// The following codes are returned by the rx_frame method, and used internally within this module
#define RX_SUCCESS 0
#define RX_FAIL_WAITING_FOR_ATN -1
#define RX_FAIL_INVALID_START_BYTE -2
#define RX_FAIL_TRUNCATED -3
#define RX_FAIL_CHECKSUM -4

// Constructor (default)
XbeeWifi::XbeeWifi() : 
#ifndef XBEE_OMIT_RX_DATA
	ip_data_func(NULL), 
	rx_seq(0), 
#endif
	modem_status_func(NULL), 
#ifndef XBEE_OMIT_SCAN
	scan_func(NULL), 
#endif
#ifndef XBEE_OMIT_RX_SAMPLE
	sample_func(NULL),
#endif
	next_atid(0),
	last_status(XBEE_MODEM_STATUS_RESET),
	callback_depth(0)
{
}

// Block until current SPI operation completes
void XbeeWifi::waitSPI()
{
	while(!(SPSR & (1<<SPIF))) { };
}

// Write a buffer of given length to SPI
// Writing multiple bytes from a single function is optimal from a SPI bus usage perspective
void XbeeWifi::write(const uint8_t *data, int len)
{
	XBEE_DEBUG(Serial.print(F("Write")));
	XBEE_DEBUG(Serial.println(len, DEC));
	// Take a copy of SPCR so we can reset it when done
	uint8_t spcr = SPCR;

	// Enable SPI with appropriate parameters
	SPCR = XBEE_SPCR;

	// Send chip select low
	digitalWrite(pin_cs, LOW);

	// Output data
	for (int i = 0; i < len; i++) {
		XBEE_DEBUG(Serial.print(F("OUT 0x")));
		XBEE_DEBUG(Serial.println(data[i], HEX));
		SPDR = data[i];
		waitSPI();
	}
	
	// Return chip select to high
	digitalWrite(pin_cs, HIGH);

	// Reset SPCR
	SPCR = spcr;
}

// Read a buffer of given length from SPI
// Reading multiple bytes in a single function is again optimal
uint8_t XbeeWifi::read()
{
	// Take a copy of SPCR so we can reset it when done
	uint8_t spcr = SPCR;

	// Enable SPI with appropriate parameters
	SPCR = XBEE_SPCR;

	// Sned chip select low
	digitalWrite(pin_cs, LOW);

	// Read data by sending 0x00
	SPDR = 0x00;
	waitSPI();
	uint8_t data = SPDR;
	XBEE_DEBUG(Serial.print("IN 0x"));
	XBEE_DEBUG(Serial.println(data, HEX));

	// Send chip select high
	digitalWrite(pin_cs, HIGH);

	// Reset SPCR
	SPCR = spcr;

	// Return the data
	return data;
}

// Initialize the XBEE
bool XbeeWifi::init(uint8_t cs, uint8_t atn, uint8_t reset, uint8_t dout)
{
	int atnn;
	unsigned long sanity;

	// Capture pin assignments for later use
	pin_cs = cs;
	pin_atn = atn;
	pin_reset = reset;
	pin_dout = dout;

	// Output details for debugging
	XBEE_DEBUG(Serial.print(F("CS = ")));
	XBEE_DEBUG(Serial.print(pin_cs, DEC));
	XBEE_DEBUG(Serial.print(F(", ATN = ")));
	XBEE_DEBUG(Serial.print(pin_atn, DEC));
	XBEE_DEBUG(Serial.print(F(", DOUT = ")));
	XBEE_DEBUG(Serial.print(pin_dout, DEC));
	XBEE_DEBUG(Serial.print(F(", RST = ")));
	XBEE_DEBUG(Serial.println(pin_reset, DEC));

	// Set correct states for SPI lines
	pinMode(MOSI, OUTPUT);
	pinMode(MISO, INPUT);
	pinMode(SCK, OUTPUT);
	pinMode(SS, OUTPUT);			// SS *MUST* be OUTPUT, even if not used as the select line
	pinMode(pin_cs, OUTPUT);
	digitalWrite(pin_cs, HIGH);
  
	// Set correct state for other signal lines
	pinMode(pin_atn, INPUT);
	digitalWrite(pin_atn, HIGH);	// Pull-up

	// Do we have pin assignments for RESET and DOUT?
	if (pin_reset != 0xFF && pin_dout != 0xFF) {
		// Yes - we can do a reset on the XBee and force the device into SPI mode

		// Tristate the reset pin
		pinMode(pin_reset, INPUT);
 
		// Set DOUT to OUTPUT and bring it low 
		pinMode(pin_dout, OUTPUT);
		digitalWrite(pin_dout, LOW);
	
		// Set RESET to OUTPUT and go LOW to reset the chip
		// now that DOUT is low which forces SPI mode
		pinMode(pin_reset, OUTPUT);
		digitalWrite(pin_reset, LOW);

		// Stay in reset for 1/10 sec to ensure the device gets the message
		delay(100);

		// Take XBEE out of reset, still leaving DOUT LOW
		// by tri-moding the reset pin
		pinMode(pin_reset, INPUT);
 
		// We expect to see ATN go high to confirm SPI mode
		if (!wait_atn()) {
			// ATN did not go high
			XBEE_DEBUG(Serial.println(F("No ATN assert on reset")));
			return false;
		}

		// The reset / force SPI auto-queues a status frame
		// so go ahead and read it
		uint8_t buf[XBEE_BUFSIZE];
		uint8_t type;
		unsigned int len;

		// Normally rx_frame consumes and dispatches modem status frames separately
		// however we explicitly request it to be returned in this case
		int result = rx_frame(&type, &len, buf, XBEE_BUFSIZE, 5000L, true);
	
		if (result == RX_SUCCESS && type == XBEE_API_FRAME_MODEM_STATUS) {
			// Good status frame - we have an Xbee talking to us!
			return true;
		} else {
			XBEE_DEBUG(Serial.println(F("****** Failure rx status")));
			return false;
		}
	} else {
		// Don't have assignments for RESET and DOUT so we have to assume
		// that the XBee has already been correctly pre-configured for SPI
		return true;
	}
}

// Transmit a SPI API frame
// type should be the type of frame (XBEE_API_FRAME_.....)
// data (of length len) should be all data within the frame, excluding frame id, length or checksum
bool XbeeWifi::tx_frame(uint8_t type, unsigned int len, uint8_t *data)
{
	// Calculate the proper checksum (sum of all bytes - type onward) subtracted from 0xFF
	uint8_t cs = type;
	for (int i = 0; i < len; i++) {
		cs += data[i];
	}
	cs = 0xff - cs;

	// Set up the header
	uint8_t hdr[4];
	hdr[0] = 0x7e;				// Start indicator
	hdr[1] = (((len + 1) >> 8) & 0xff);	// Length MSB
	hdr[2] = ((len + 1) & 0xff);		// Length LSB
	hdr[3] = type;				// API Frame Type

	// Send
	write(hdr, 4);				// Write header
	write(data, len);			// Write the data to SPI
	write(&cs, 1);				// And the checksum
}

// Receive a SPI API frame
// Typically this is used to receive AT response frame
// It will trigger the asynchronous functions responsible for receiving other frame types as needed
// to ensure those frames get processed
int XbeeWifi::rx_frame(uint8_t *frame_type, unsigned int *len, uint8_t *data, int bufsize, unsigned long atn_wait_ms, bool return_status)
{
	// Before we do anything else, set the received length to zero
	// for the case where we don't sucesfully receive anything
	*len = 0;

	// This will be our working received length
	unsigned int rxlen = 0;

	// This will be the current character read
	uint8_t in;

	// This will be the type of packet (API frame type)
	uint8_t type;

	// This will be our truncation flag
	bool truncated = false;

	// Repeat this operation until we receive a returnable packet
	do {
		// Wait on ATN
		if (!wait_atn(atn_wait_ms)) {
			if (atn_wait_ms > 0) {
				XBEE_DEBUG(Serial.println(F("****** Failed in rx_frame waiting for ATN")));
			}
			return RX_FAIL_WAITING_FOR_ATN;
		}
	
		// Read start byte
		in = read();
		if (in != 0x7E) {
			XBEE_DEBUG(Serial.println(F("****** Failed in rx_frame, invalid start byte")));
			flush_spi();
			return RX_FAIL_INVALID_START_BYTE;
		}
	
		// Read length (MSB and LSB) and combine
		rxlen = ((read() << 8 | read()) - 1);	// -1 because we do not include type in our length
		XBEE_DEBUG(Serial.print(F("rx_frame Length Of ")));
		XBEE_DEBUG(Serial.print(rxlen, HEX));
		XBEE_DEBUG(Serial.println(F(" bytes")));
	
		// Read frame type
		type = read();
		XBEE_DEBUG(Serial.print(F("Read type 0x")));
		XBEE_DEBUG(Serial.println(type, HEX));

		uint8_t cs, cs_incoming;

		switch(type) {
#ifndef XBEE_OMIT_RX_DATA
			case XBEE_API_FRAME_RX_IPV4		:
#ifndef XBEE_OMIT_COMPAT_MODE
			case XBEE_API_FRAME_RX64_INDICATOR	:
#endif
				XBEE_DEBUG(Serial.println(F("Is IP reception, route to rx_ip")));
				// This is an IP V4 RX packet, which can be very long and requires
				// special handling due to memory constraints
				// So process that here, it's not the packet we're directly interested in
				rx_ip(rxlen, type);
				break;
#endif

#ifndef XBEE_OMIT_RX_SAMPLE
			case XBEE_API_FRAME_IO_DATA_SAMPLE_RX	:
				// This is a remote sample, which may arrive at any time
				// again - we're not interested int it directly - it will process
				// it's own async callbacks as needed
				rx_sample(rxlen);
				break;
#endif

			case XBEE_API_FRAME_MODEM_STATUS	:
				// This is a modem status frame, which crops up from time to time
				// We will return this if explicitly requested, otherwise it's routed
				// to it's own asynchronous handler
				if (!return_status) {
					XBEE_DEBUG(Serial.println(F("IS MS, route to rx_modem_status")));
					rx_modem_status(rxlen);
					break;
				} 
	
				// If return_status == true, we drop through to the next case
				// which processes the modem status frame like any other

			case XBEE_API_FRAME_TX_STATUS		:
			case XBEE_API_FRAME_REMOTE_CMD_RESP	:
			case XBEE_API_FRAME_ATCMD_RESP		:
				// We want to handle and return this frame

				cs = type;
				for (int i = 0 ; i < rxlen; i++) {
					in = read();
					cs += in;
					if (i <bufsize) {
						data[i] = in;
					} else {
						truncated = true;
					}
				}
				// Complete checksum calculation
				cs = 0xFF - cs;

				// Read incoming checksum (last byte of packet)
				cs_incoming = read();

				// Set up returned values
				*len = (rxlen > bufsize) ? bufsize : rxlen;
				*frame_type = type;

				// And report appropriate status in return value
				if (truncated) {
					XBEE_DEBUG(Serial.println(F("****** RX fail, truncation")));
					return RX_FAIL_TRUNCATED;
				} else if (cs != cs_incoming) {
					XBEE_DEBUG(Serial.println(F("****** RX fail, checksum")));
					XBEE_DEBUG(Serial.print(F("RX CS 0x")));
					XBEE_DEBUG(Serial.println(cs_incoming, HEX));
					XBEE_DEBUG(Serial.print(F("CALC CS 0x")));
					XBEE_DEBUG(Serial.println(cs, HEX));
					return RX_FAIL_CHECKSUM;
				} else {
					return RX_SUCCESS;
				}

			default				:
				// This is an unexpected (possibly new, unsupported) frame
				// Drop it with debug
				XBEE_DEBUG(Serial.print(F("**** RX DROP Unsupported frame, type : 0x")));
				XBEE_DEBUG(Serial.println(type, HEX));
				for (int i = 0 ; i < rxlen + 1; i++) {
					uint8_t dropped = read();
					XBEE_DEBUG(Serial.print(F("Dropping : 0x")));
					XBEE_DEBUG(Serial.println(dropped, HEX));
				}
				
				break;
		
		}
	} while(true);	// Break out via return statement
}

// Dispatch an AT CMD with raw buffer as parameter
bool XbeeWifi::at_cmd_raw(const char *atxx, uint8_t *buffer, int len, bool queued)
{
	return at_cmd(atxx, buffer, len, NULL, 0, queued);
}

// Same - to remote
bool XbeeWifi::at_remcmd_raw(uint8_t *ip, const char *atxx, uint8_t *buffer, int len, bool apply)
{
	return at_remcmd(ip, atxx, buffer, len, NULL, 0, apply);
}

// Dispatch an AT CMD with string buffer as parameter
bool XbeeWifi::at_cmd_str(const char *atxx, const char *buffer, bool queued)
{
	return at_cmd(atxx, (uint8_t *)buffer, strlen(buffer), NULL, 0, queued);
}

// Same - to remote
bool XbeeWifi::at_remcmd_str(uint8_t *ip, const char *atxx, const char *buffer, bool apply)
{
	return at_remcmd(ip, atxx, (uint8_t *)buffer, strlen(buffer), NULL, 0, apply);
}

// Dispatch an AT CMD with single byte parameter
bool XbeeWifi::at_cmd_byte(const char *atxx, uint8_t byte, bool queued)
{
	return at_cmd(atxx, &byte, 1, NULL, 0, queued);
}

// Same - to remote
bool XbeeWifi::at_remcmd_byte(uint8_t *ip, const char *atxx, uint8_t byte, bool apply)
{
	return at_remcmd(ip, atxx, &byte, 1, NULL, 0, apply);
}

// Dispatch an AT CMD with word paramter
bool XbeeWifi::at_cmd_short(const char *atxx, uint16_t twobyte, bool queued)
{
	uint16_t swap = ((twobyte >>8) | ((twobyte & 0xFF) << 8));
	return at_cmd(atxx, (uint8_t *) &swap, 2, NULL, 0, queued);
}

// Same - to remote
bool XbeeWifi::at_remcmd_short(uint8_t *ip, const char *atxx, uint16_t twobyte, bool apply)
{
	uint16_t swap = ((twobyte >>8) | ((twobyte & 0xFF) << 8));
	return at_remcmd(ip, atxx, (uint8_t *) &swap, 2, NULL, 0, apply);
}

// Dispatch a non parameterized AT CMD
bool XbeeWifi::at_cmd_noparm(const char *atxx, bool queued)
{
	return at_cmd(atxx, NULL, 0, NULL, 0, queued);
}

// Same to remote
bool XbeeWifi::at_remcmd_noparm(uint8_t *ip, const char *atxx, bool apply)
{
	return at_remcmd(ip, atxx, NULL, 0, NULL, 0, apply);
}

// AT command processor backend
// atxx = char[2+] where first two characters are the AT code
// parmval = the parameter value
// parmlen = and it's length
// returndata = buffer for returned data (or NULL if not interested)
// returnlen = size of return data buffer
// queued = true means use the queued (non immediate) AT operation
bool XbeeWifi::at_cmd(const char *atxx, const uint8_t *parmval, int parmlen, void *returndata, int *returnlen, bool queued)
{
	XBEE_DEBUG(Serial.print(F("Run AT Query ")));
	XBEE_DEBUG(Serial.print(atxx[0]));
	XBEE_DEBUG(Serial.print(atxx[1]));
	XBEE_DEBUG(Serial.println());

	// If we are in RX callback, then we can't do ATs
	if (callback_depth > 0) {
		XBEE_DEBUG(Serial.println(F("***** AT Reject - in RX callback")));
		return false;
	}

	// Parameter must be able to fit into our maximum allowable buffer size
	if (parmlen > XBEE_BUFSIZE - 3) {
		XBEE_DEBUG(Serial.println(F("****** Too big AT")));
		return false;
	}

	// If this is an immediate operation, increment the atid - mostly for debug reasons
	if (!queued) {
		next_atid++;
		if (next_atid == 0) next_atid++;
	}

	// Construct packet
	uint8_t buf[XBEE_BUFSIZE];
	buf[0] = queued ? 0x00 : next_atid;
	buf[1] = atxx[0];
	buf[2] = atxx[1];
	memcpy(buf + 3, parmval, parmlen);

	// Transmit
	tx_frame(queued ? XBEE_API_FRAME_ATCMD_QUEUED : XBEE_API_FRAME_ATCMD, parmlen + 3, buf);

	// If this was immediate, then we are expecting an AT response
	// Unless this was an AS (active scan) which we handle as a strange special case
	if (!queued && (atxx[0] != 'A' || atxx[1] != 'S')) {
		// AT response expected
		uint8_t type;
		unsigned int len;
		int res;
		if ((res = rx_frame(&type, &len, buf, XBEE_BUFSIZE)) == RX_SUCCESS) {
			if (type == XBEE_API_FRAME_ATCMD_RESP && buf[0] == next_atid && buf[3] == 0) {
				// Correct frame type and success code found
				if (returndata != NULL) {
					// Caller wants the parameter returned
					*returnlen = (len - 4);
					memcpy(returndata, buf + 4, *returnlen > XBEE_BUFSIZE ? XBEE_BUFSIZE : *returnlen);
				}
				return true;
			} else {
				// Failure - either not the correct type of packet
				// or wrong ATID or non success indication
				XBEE_DEBUG(Serial.println(F("****** Failed AT CMD RESP")));
			}
		} else {
			// Failed to receive a packet
			XBEE_DEBUG(Serial.print(F("****** Failed AT CMD RESP, error ")));
			XBEE_DEBUG(Serial.println(res, DEC));
		}
	
		// Something failed, return false
		return false;
	} else {
		// Was queued or was an active scan which we handle separately
		// so we don't have a response to look for so we have to assume success
		return true;
	}
}

// This is the equivalent back end for AT command processing for remote nodes
bool XbeeWifi::at_remcmd(uint8_t *ip, const char *atxx, const uint8_t *parmval, int parmlen, void *returndata, int *returnlen, bool apply)
{
	XBEE_DEBUG(Serial.print(F("Run AT Query, remote ")));
	XBEE_DEBUG(Serial.print(atxx[0]));
	XBEE_DEBUG(Serial.print(atxx[1]));
	XBEE_DEBUG(Serial.println());

	// Parameter must be able to fit into our maximum allowable buffer size
	if (parmlen > XBEE_BUFSIZE - 15) {
		XBEE_DEBUG(Serial.println(F("****** Too big AT")));
		return false;
	}

	// Increment ATID
	next_atid++;
	if (next_atid == 0) next_atid++;

	// Construct packet
	uint8_t buf[XBEE_BUFSIZE];
	buf[0] = next_atid;
	memset(buf + 1, 0, 4);
	memcpy(buf + 5, ip, 4);
	buf[9] = apply ? 0x02 : 0x00;
	buf[10] = atxx[0];
	buf[11] = atxx[1];
	memcpy(buf + 12, parmval, parmlen);

	// Transmit
	tx_frame(XBEE_API_FRAME_REMOTE_CMD_REQ, parmlen + 12, buf);

	// If this was immediate, then we are expecting an AT response
	// Unless this was an AS (active scan) which we handle as a strange special case
	// REMAT response expected
	uint8_t type;
	unsigned int len;
	int res;
	if ((res = rx_frame(&type, &len, buf, XBEE_BUFSIZE)) == RX_SUCCESS) {
		if (type == XBEE_API_FRAME_REMOTE_CMD_RESP && 
			buf[0] == next_atid && 
			buf[11] == 0 &&
			memcmp(ip, buf + 5, 4) == 0) {
			// Correct frame type, success code and ip
			if (returndata != NULL) {
				// Caller wants the parameter returned
				*returnlen = (len - 12);
				memcpy(returndata, buf + 12, *returnlen > XBEE_BUFSIZE ? XBEE_BUFSIZE : *returnlen);
			}
			return true;
		} else {
			XBEE_DEBUG(Serial.println(F("****** Failed AT/REM CMD RESP")));
		}
	} else {
		XBEE_DEBUG(Serial.print(F("****** Failed AT/REM CMD RESP, error ")));
		XBEE_DEBUG(Serial.println(res, DEC));
	}
	return false;
}

// Query an AT for it's parameter value
// atxx = char[2+] coptaining AT seuqence in first two positions
// parmval is the paramever value buffer for return
// parmlen will return the length of the parameter
// if maxlen < parmlen then parmval will be truncated
bool XbeeWifi::at_query(const char *atxx, uint8_t *parmval, int *parmlen, int maxlen)
{
	char retbuf[XBEE_BUFSIZE];
	int len;

	if (at_cmd(atxx, NULL, 0, retbuf, &len, false)) {
		*parmlen = len;
		memcpy(parmval, retbuf, len > maxlen ? maxlen : len);
		return true;
	} else {
		XBEE_DEBUG(Serial.println(F("****** Failed AT QRY")));
		return false;
	}
}

// Equivalent for remote device
bool XbeeWifi::at_remquery(uint8_t *ip, const char *atxx, uint8_t *parmval, int *parmlen, int maxlen)
{
	char retbuf[XBEE_BUFSIZE];
	int len;

	if (at_remcmd(ip, atxx, NULL, 0, retbuf, &len, true)) {
		*parmlen = len;
		memcpy(parmval, retbuf, len > maxlen ? maxlen : len);
		return true;
	} else {
		XBEE_DEBUG(Serial.println(F("**** Failed ATREMQRY")));
		return false;
	}
}

// Wait until atn asserts, for a given maximum number of milliseconds
// returns true if assert is found
// Call with max_mllis = 0 to get a simple true/false on whether ATN is currently asserted
bool XbeeWifi::wait_atn(unsigned long int max_millis)
{
	if (max_millis > 0) {
		XBEE_DEBUG(Serial.println(F("Waiting for ATN")));
	}
	unsigned long int sanity = millis() + max_millis;
	int atn;
	do {
		atn = digitalRead(pin_atn);
		if (atn == LOW) return true;
		if (millis() >= sanity) return false;
	} while(true);
}

// Flush SPI until ATN de-asserts, meaning XBEE has no queued data
// This is an emergency recovery function to ensure resync of the SPI bus at the potential loss
// of much data
// If something really goes off the rails, this function can be used to nudge things back on track
// It should never be hit in normal operation unless we have software errors or possibly noise on the SPI bus
void XbeeWifi::flush_spi()
{
	while(digitalRead(pin_atn) == LOW) {
		uint8_t in = read();
		XBEE_DEBUG(Serial.print(F("Flushed one from spi: 0x")));
		XBEE_DEBUG(Serial.println(in, HEX));
	}
}
		
// Register a callback for IP data delivery
#ifndef XBEE_OMIT_RX_DATA
void XbeeWifi::register_ip_data_callback(void (*func)(uint8_t *, int, s_rxinfo *))
{
	ip_data_func = func;
}
#endif

// Register a callback for status (modem status) delivery
void XbeeWifi::register_status_callback(void (*func)(uint8_t))
{
	modem_status_func = func;
}

// Register a callback for active scan data delivery
#ifndef XBEE_OMIT_SCAN
void XbeeWifi::register_scan_callback(void (*func)(uint8_t, int, char *))
{
	scan_func = func;
}
#endif

// Register a callback for remote sample data delivery
#ifndef XBEE_OMIT_RX_SAMPLE
void XbeeWifi::register_sample_callback(void (*func)(s_sample *))
{
	sample_func = func;
}
#endif

// This method should be called repeatedly by the run loop to ensure
// that the SPI bus is serviced in an expeditious manner to prevent overruns
// and ensure timely delivery of asynchronous callbacks
void XbeeWifi::process()
{
	int res;
	unsigned int len;
	uint8_t buf[XBEE_BUFSIZE];
	uint8_t type;
	do {
		// Receive frames with zero timeout
		// Since we're not currently expecting an exlicit response to anything
		// this simply serves to dispatch our asynchronous frames
		res = rx_frame(&type, &len, buf, XBEE_BUFSIZE, 0);

		// IP / Status / Sample packets are already handled, the only thing we need to handle here
		// is AT response to active scan
#ifndef XBEE_OMIT_SCAN
		if (type == XBEE_API_FRAME_ATCMD_RESP && res == RX_SUCCESS) {
			handleActiveScan(buf, len);
		}
#endif

		// Keep doing this until we get a report of timeout (0 length of course) waiting
		// for ATN, meaning ATN is no longer asserted and the SPI bus is empty
	} while(res != RX_FAIL_WAITING_FOR_ATN);
}

// Receive a remote sample packet
// Packet must have already been read to type before calling with length of remaining data
#ifndef XBEE_OMIT_RX_SAMPLE
void XbeeWifi::rx_sample(unsigned int len)
{
	XBEE_DEBUG(Serial.print(F("RX Sample len 0x")));
	XBEE_DEBUG(Serial.println(len, HEX));
	uint8_t buf[XBEE_BUFSIZE];

	// Create new empty sample record
	s_sample sample;
	memset(&sample, 0, sizeof(s_sample));

	// Initiate checksum processing
	uint8_t cs = XBEE_API_FRAME_IO_DATA_SAMPLE_RX;
	
	// Keep reading all data, reading in the appropriate values as they are reached
	for (int pos = 0 ; pos < len; pos++) {
		uint8_t incoming = read();
		cs += incoming;
		switch(pos) {
			case 4	: sample.source_addr[0] = incoming; break;
			case 5	: sample.source_addr[1] = incoming; break;
			case 6	: sample.source_addr[2] = incoming; break;
			case 7	: sample.source_addr[3] = incoming; break;
			case 11	: sample.digital_mask |= ((uint16_t) incoming) << 8; break;
			case 12	: sample.digital_mask |= incoming; break;
			case 13 : sample.analog_mask = incoming; break;
			case 14 : sample.digital_samples |= ((uint8_t) incoming) << 8; break;
			case 15 : sample.digital_samples |= incoming;
			case 16 : sample.analog_samples |= ((uint8_t) incoming) << 8; break;
			case 17 : sample.analog_samples |= incoming; break;
		}
	}
	
	// Read and validate checksum
	uint8_t incoming_cs = read();
	cs = 0xff - cs;
	if (incoming_cs != cs) {
		// Invalid checksum, ignore this packet
		XBEE_DEBUG(Serial.print(F("Incoming sample CS mismatch RX=0x")));
		XBEE_DEBUG(Serial.print(incoming_cs, HEX));
		XBEE_DEBUG(Serial.print(F(", CALC=0x")));
		XBEE_DEBUG(Serial.println(cs, HEX));
	} else {
		// Valid checksum, dispatch this sample to the callback, if registered
		XBEE_DEBUG(Serial.println(F("Sample dispatch")));
		if (sample_func) sample_func(&sample);
	}
}
#endif

// Receive an IP packet (either IPv4 or compatability IP packet)
// Must have read to frame type and call with both frame type and length
#ifndef XBEE_OMIT_RX_DATA
void XbeeWifi::rx_ip(unsigned int len, uint8_t frame_type)
{
	uint8_t buf[XBEE_BUFSIZE + 1];	// Leave 1 byte for user termination with \0 for safety
	int bufpos = 0;
	int pos = 4;

	// Create a new IP data record
	s_rxinfo info;
	memset(&info, 0, sizeof(s_rxinfo));

	// Set total length of packet
	info.total_packet_length = len - 0x0A;

	// Initialize checksum processing
	uint8_t cs = frame_type;

	// If this is an application compatability IP packet we assert source and dest port
	// of 0xBEE as defined by spec
#ifndef XBEE_OMIT_COMPAT_MODE
	if (frame_type != XBEE_API_FRAME_RX_IPV4) {
		info.source_port = info.dest_port = 0xBEE;
		XBEE_DEBUG(Serial.println(F("RX APPMODE")));
	} else {
		XBEE_DEBUG(Serial.println(F("RX RAW")));
	}
#endif
	
	// Read data and process in based on packet type
	do {
		uint8_t inbound = read();
		XBEE_DEBUG(Serial.print(F("Inbound PKT Data 0x")));
		XBEE_DEBUG(Serial.println(inbound, HEX));
		cs += inbound;
#ifndef XBEE_OMIT_COMPAT_MODE
		if (frame_type == XBEE_API_FRAME_RX_IPV4) {
#endif
			// Handle IPV4 header info
			switch(pos) {
				case 4	:	info.source_addr[0] = inbound; break;
				case 5	:	info.source_addr[1] = inbound; break;
				case 6	:	info.source_addr[2] = inbound; break;
				case 7	:	info.source_addr[3] = inbound; break;
				case 8	:	info.dest_port |= (((uint16_t) inbound) << 8); break;
				case 9	:	info.dest_port |= ((uint16_t) inbound); break;
				case 10	:	info.source_port |= (((uint16_t) inbound) << 8); break;
				case 11	:	info.source_port |= ((uint16_t) inbound); break;
				case 12	:	info.protocol = inbound; break;
			}
#ifndef XBEE_OMIT_COMPAT_MODE
			} else {
			// Handle application compatability header info
			switch(pos) {
				case 7	:	info.source_addr[0] = inbound; break;
				case 8	:	info.source_addr[1] = inbound; break;
				case 9	:	info.source_addr[2] = inbound; break;
				case 10	:	info.source_addr[3] = inbound; break;
			}
		}
#endif

		if (pos > 0x0D) {
			// Past the header - reading actual packet data now
			if (bufpos == XBEE_BUFSIZE && len > 1) {
				// We've exhausted our inbound buffer, we must dispatch
				// this buffer now, even though we haven't had chance to check
				// the checksum unless of course this was the last byte
				// in which case we still defer
				if (ip_data_func) {
					callback_depth++;
					ip_data_func(buf, bufpos, &info);
					callback_depth--;
					info.current_offset += bufpos;
				}
				bufpos = 0;
			}
			buf[bufpos++] = inbound;
		}
		pos++;
	} while (--len > 0);

	// Complete checksum processing
	uint8_t inbound_cs = read();
	cs = 0xFF - cs;
	if (inbound_cs != cs) {
		XBEE_DEBUG(Serial.println(F("****** CS Fail inbound rx")));
		info.checksum_error = true;
	}

	// The last packet for a given sequence will always set the checksum error
	// if it occured
	// Dispatch the IP data to the callback function - if defined
	info.final = true;
	if (bufpos > 0 && ip_data_func) ip_data_func(buf, bufpos, &info);
	rx_seq++;
}
#endif

// Receive modem status packet
void XbeeWifi::rx_modem_status(unsigned int len)
{
	// Length SHOULD be a single byte
	if (len != 1) {
		// Oops....
		// Read the frame out and ignore
		for (int i = 0 ; i < len + 1; i++) read();
		XBEE_DEBUG(Serial.println(F("Non 1 length on incoming modem status frame")));
	} else {
		uint8_t status = read();
		uint8_t cs = 0xFF - (uint8_t) (status + XBEE_API_FRAME_MODEM_STATUS);
		uint8_t incoming_cs = read();
		if (incoming_cs == cs) {
			// Record last status
			last_status = status;
			// Dispatch status
			if (modem_status_func) modem_status_func(status);
		} else {
			// Bad checksum - discard
			XBEE_DEBUG(Serial.println(F("Checksum mismatch on incoming modem status frame")));
		}
	}
}

// Transmits data of length to the address and with options specified in addr struct
// If confirm=true (default) then send confirmation is requested and reflected in
// the return from this function
// useAppService=true would be used to use the application compatability (0xBEE port) method, true for
// raw IPV4
// When using app compat mode, addr can be null because it is unused
bool XbeeWifi::transmit(const uint8_t *ip, s_txoptions *addr, uint8_t *data, int len, bool confirm, bool useAppService)
{
	XBEE_DEBUG(Serial.print(F("XMIT frame of length ")));
	XBEE_DEBUG(Serial.println(len, DEC));
	XBEE_DEBUG(Serial.print(F("XMIT mode : ")));
	XBEE_DEBUG(Serial.println(useAppService ? F("APP") : F("RAW")));

	// If we're in the RX callback, we cannot risk confirmation, so we force confirm=false
	if (callback_depth > 0) {
		confirm = false;
		XBEE_DEBUG(Serial.print(F("Transmit during RX callback - force no confirmation")));
	}

	// Attempt to send nothing will be considered an error
	if (len <= 0) return false;

	// Set up the header for the data
	uint8_t hdrbuf[15];

	// If we've been asked for confirmation, then we'll be needing
	// an atid
	if (confirm) {
		next_atid++;
		if (next_atid == 0) next_atid++;
	}

	// Construct the header
#ifndef XBEE_OMIT_COMPAT_MODE
	int hdrlen = useAppService ? 0x0E : 0x0F;
#else
	int hdrlen = 0x0E;
#endif;
	int offset = 0;
	hdrbuf[offset++]  = 0x7E;				// Start byte
	hdrbuf[offset++]  = (len + hdrlen - 3) >> 8;		// Length MSB
	hdrbuf[offset++]  = (len + hdrlen - 3) & 0xFF;		// Length LSB
#ifndef XBEE_OMIT_COMPAT_MODE
	hdrbuf[offset++]  = useAppService ? XBEE_API_FRAME_TX64 : XBEE_API_FRAME_TX_IPV4;	// Frame type
#else
	hdrbuf[offset++] = XBEE_API_FRAME_TX_IPV4;
#endif
	hdrbuf[offset++]  = confirm ? next_atid : 0x00;	// ATID (or 00 if no confirm required)
	if (useAppService) {
		hdrbuf[offset++] = 0x00;
		hdrbuf[offset++] = 0x00;
		hdrbuf[offset++] = 0x00;
		hdrbuf[offset++] = 0x00;
	}
	hdrbuf[offset++]  = ip[0];			// IP Address...
	hdrbuf[offset++]  = ip[1];
	hdrbuf[offset++]  = ip[2];
	hdrbuf[offset++]  = ip[3];
#ifndef XBEE_OMIT_COMPAT_MODE
	if (!useAppService) {
#endif
		hdrbuf[offset++] = addr->dest_port >> 8;		// Dest port MSB
		hdrbuf[offset++] = addr->dest_port & 0xFF;		// Dest port LSB
		hdrbuf[offset++] = addr->source_port >> 8;		// Source port MSB
		hdrbuf[offset++] = addr->source_port & 0xFF;		// Source port LSB
		hdrbuf[offset++] = addr->protocol == XBEE_NET_IPPROTO_TCP ? XBEE_NET_IPPROTO_TCP : XBEE_NET_IPPROTO_UDP;
		hdrbuf[offset++] = addr->leave_open ? 0x00 : 0x01;	// TCP Leave open / immediate close
#ifndef XBEE_OMIT_COMPAT_MODE
	} else {
		hdrbuf[offset++] = 0x00;
	}
#endif
	
	// Write the header, and then the data to SPI
	write(hdrbuf, hdrlen);
	write(data, len);

	// Calculate the checksum
	uint8_t cs = 0;
	int i;
	for(i = 3; i < hdrlen; i ++) cs += hdrbuf[i];
	for (i = 0; i < len; i ++) cs += data[i];
	cs = 0xFF - cs;

	// Write the checksum
	write(&cs, 1);

	// If asked to confirm we sent a packet with a non-zero ATID
	// and must now listen for a response
	if (confirm) {
		uint8_t type;
		unsigned int len;
		uint8_t buf[XBEE_BUFSIZE];
		// Attempt to receive a frame - use a long timeout for ATN (1 minute)
		if (rx_frame(&type, &len, buf, XBEE_BUFSIZE, 60000L) == RX_SUCCESS) {
			if (type != XBEE_API_FRAME_TX_STATUS) {
				// Did not get the expected frame back
				// Probably not the best idea, but clean out the SPI bus
				XBEE_DEBUG(Serial.println(F("****** Receive of frame not TX status")));
				flush_spi();
				return false;
			}
			if (buf[0] != next_atid) {
				// ATID mismatch
				// Very weird - clean out the SPI bus
				XBEE_DEBUG(Serial.println(F("****** Receive of frame, ATID mismatch")));
				flush_spi();
				return false;
			}
			if (buf[1] != 0x00) {
				// Transmission operation success, but failed to transmit
				XBEE_DEBUG(Serial.print(F("****** TX Failure, code=")));
				XBEE_DEBUG(Serial.println(buf[1], HEX));
				return false;
			}
		} else {
			// ATN Timeout or structural problem with received frame
			XBEE_DEBUG(Serial.println(F("****** RX TX Status frame failed RX")));
			flush_spi();
			return false;
		}
	}

	XBEE_DEBUG(Serial.println(F("Frame sent successfully")));
	return true;
}

// Initiate active scan
// Note that network reset will occur meaning association to any AP will be lost
#ifndef XBEE_OMIT_SCAN
bool XbeeWifi::initiateScan()
{
	// Initiate network reset
	if (!at_cmd_noparm(XBEE_AT_EXEC_NETWORK_RESET)) return false;

	// Wait for effect (probably not needed - but still - why not)
	delay(250);

	// Initiate active scan and return success
	return at_cmd_noparm(XBEE_AT_DIAG_ACTIVE_SCAN);
}
#endif //XBEE_OMIT_SCAN

#ifndef XBEE_OMIT_SCAN
// Handle incoming active scan data
void XbeeWifi::handleActiveScan(uint8_t *buf, int len)
{
	XBEE_DEBUG(Serial.println(F("Handle active scan")));
	char ssid[33];
	int rssi;
	uint8_t encmode;

	// Check that this is an AS response status 0
	if (buf[1] == 'A' && buf[2] == 'S' && buf[3] == 0x00) {
		// Valid AS response frame
		if (len <= 8) {
			XBEE_DEBUG(Serial.println(F("AS end frame? rx")));
		} else {
 
			encmode = buf[6];
			rssi = (int) buf[7];
			memset(ssid, 0, 33);
			memcpy(ssid, buf + 8, (len - 8) > 32 ? 32 : (len - 8));
			if (scan_func) scan_func(encmode, rssi, ssid);
		}
	} else {
		XBEE_DEBUG(Serial.println(F("Invalid AS response frame")));
	}
}
#endif //XBEE_OMIT_SCAN
