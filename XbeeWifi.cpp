/*
 * File			xbeewifi.cpp
 *
 * Synopsis		Support for Xbee Wifi (XB24-WF...) modules through SPI bus
 *
 * Author		Chris Bearman
 *
 * Version		2.1
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

//#define XBEE_ENABLE_DEBUG

// Debug functions
#ifdef XBEE_ENABLE_DEBUG
// Debug is enabled, XBEE_DEBUG is a simple macro that just inserts the code within it's parameter
#define XBEE_DEBUG(x) (x)
#else 
// Debug is not enabled, the XBEE_DEBUG becomes a NOP macro that essentially discards it's parameter
#define XBEE_DEBUG(x)
#endif

// The following codes are returned by the rx_frame method, and used internally within this module
#define RX_SUCCESS 0
#define RX_FAIL_WAITING_FOR_ATN -1
#define RX_FAIL_INVALID_START_BYTE -2
#define RX_FAIL_TRUNCATED -3
#define RX_FAIL_CHECKSUM -4

// Constructor (default)
XbeeWifi::XbeeWifi() : 
	last_status(XBEE_MODEM_STATUS_RESET),
#ifndef XBEE_OMIT_RX_DATA
	rx_seq(0), 
	ip_data_func(NULL), 
#endif
	modem_status_func(NULL), 
#ifndef XBEE_OMIT_SCAN
	scan_func(NULL), 
#endif
#ifndef XBEE_OMIT_RX_SAMPLE
	sample_func(NULL),
#endif
	next_atid(0),
	callback_depth(0),
#ifdef ARCH_ATMEGA
	spcr_copy(SPCR),
	spsr_copy(SPSR),
#endif
	spiRunning(false),
	spiLocked(false)
{
}

// Write a buffer of given length to SPI
// Writing multiple bytes from a single function is optimal from a SPI bus usage perspective
void XbeeWifi::write(const uint8_t *data, int len)
{
	uint8_t rxbyte;
	XBEE_DEBUG(Serial.print(F("Write")));
	XBEE_DEBUG(Serial.println(len, DEC));

	// Output data
	for (int i = 0; i < len; i++) {
#ifdef XBEE_ENABLE_DEBUG
                if (digitalRead(pin_atn) == LOW) Serial.println("ATN Asserted during write");
#endif
		XBEE_DEBUG(Serial.print(F("OUT 0x")));
		XBEE_DEBUG(Serial.println(data[i], HEX));
		rxbyte = rxtx(data[i]);
	}
}

// Set up for SPI operation, assert chip select
void XbeeWifi::spiStart()
{
	if (spiRunning) return;
	spiRunning = true;
	XBEE_DEBUG(Serial.println("SPI Start"));
	delay(1);
#ifdef ARCH_ATMEGA
	spcr_copy = SPCR;
	spsr_copy = SPSR;
	SPCR = XBEE_SPCR;
	SPSR = XBEE_SPSR;
#endif
	digitalWrite(pin_cs, LOW);
#if NOP_COUNT > 0
	for (int i = 0 ; i < NOP_COUNT; i++) __asm__("nop\n\t");
#endif
}

// Clean up from SPI operation, de-assert chip select, unless SPI has been locked
void XbeeWifi::spiEnd()
{
	if (!spiRunning || spiLocked) return;
#if NOP_COUNT > 0
	for (int i = 0 ; i < NOP_COUNT; i++) __asm__("nop\n\t");
#endif
	spiRunning = false;
	XBEE_DEBUG(Serial.println("SPI End"));
	digitalWrite(pin_cs, HIGH);
#ifdef ARCH_ATMEGA
	SPCR = spcr_copy;
	SPSR = spsr_copy;
#endif
}

uint8_t XbeeWifi::rxtx(uint8_t data)
{
	uint8_t rx;
#ifdef ARCH_ATMEGA
	SPDR = data;
	while(!(SPSR & (1<<SPIF))) { };
	rx = SPDR;
#endif
#ifdef ARCH_SAM
	while ((SPI_INTERFACE->SPI_SR & SPI_SR_TDRE) == 0) { };
	SPI_INTERFACE->SPI_TDR = ((uint32_t) SPI_PCS(spi_ch) | (uint32_t) data);
	while ((SPI_INTERFACE->SPI_SR & SPI_SR_RDRF) == 0) { };
	rx = (SPI_INTERFACE->SPI_RDR & 0xFF);
#endif
	return rx;
}

// Read a buffer of given length from SPI
// Reading multiple bytes in a single function is again optimal
uint8_t XbeeWifi::read()
{
	// A read is accomplished by transmitting a meaningless byte
	uint8_t data = rxtx(0x00);
	XBEE_DEBUG(Serial.print("IN 0x"));
	XBEE_DEBUG(Serial.println(data, HEX));

	// Return the data
	return data;
}

// Initialize the XBEE
bool XbeeWifi::init(uint8_t cs, uint8_t atn, uint8_t reset, uint8_t dout)
{
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
#ifdef ARCH_ATMEGA
	// Don't want to do this on the DUE
	pinMode(MOSI, OUTPUT);
	pinMode(MISO, INPUT);
	pinMode(SCK, OUTPUT);
	pinMode(SS, OUTPUT);			// SS *MUST* be OUTPUT, even if not used as the select line
#endif
	pinMode(pin_cs, OUTPUT);
	digitalWrite(pin_cs, HIGH);
  
	// Set correct state for other signal lines
	pinMode(pin_atn, INPUT);
	digitalWrite(pin_atn, HIGH);	// Pull-up

#ifdef ARCH_SAM
	// Set up SPI
	PIO_Configure(
		g_APinDescription[PIN_SPI_MOSI].pPort,
		g_APinDescription[PIN_SPI_MOSI].ulPinType,
		g_APinDescription[PIN_SPI_MOSI].ulPin,
		g_APinDescription[PIN_SPI_MOSI].ulPinConfiguration);
	PIO_Configure(
		g_APinDescription[PIN_SPI_MISO].pPort,
		g_APinDescription[PIN_SPI_MISO].ulPinType,
		g_APinDescription[PIN_SPI_MISO].ulPin,
		g_APinDescription[PIN_SPI_MISO].ulPinConfiguration);
	PIO_Configure(
		g_APinDescription[PIN_SPI_SCK].pPort,
		g_APinDescription[PIN_SPI_SCK].ulPinType,
		g_APinDescription[PIN_SPI_SCK].ulPin,
		g_APinDescription[PIN_SPI_SCK].ulPinConfiguration);
	SPI_Configure(SPI_INTERFACE, SPI_INTERFACE_ID, SPI_MR_MSTR | SPI_MR_PS | SPI_MR_MODFDIS);
	SPI_Enable(SPI_INTERFACE);

	// pin_cs actual is the pin that the SPI controller is using for CS, it may - or may not - be the same pin
	// that we're actually using...
	pin_cs_actual = (pin_cs == BOARD_SPI_SS0 || pin_cs == BOARD_SPI_SS1 || pin_cs == BOARD_SPI_SS2 || pin_cs == BOARD_SPI_SS3) ? pin_cs : SPI_CS_DEFAULT;

/*
	We are NOT associating the CS pin to the SPI controller. This seems to work okay since we handle CS manually in all cases...
	If we were associating the actual CS pin, we'd do this...
	uint32_t spiPin = BOARD_PIN_TO_SPI_PIN(pin_cs_actual);
	PIO_Configure(
		g_APinDescription[spiPin].pPort,
		g_APinDescription[spiPin].ulPinType,
		g_APinDescription[spiPin].ulPin,
		g_APinDescription[spiPin].ulPinConfiguration);
*/

	// Set up SPI control register
	// 0x02 = SPI Mode 0 (CPOL = 0, CPHA = 0)
	SPI_ConfigureNPCS(SPI_INTERFACE, spi_ch, 0x02 | SPI_CSR_SCBR(SPI_BUS_DIVISOR) | SPI_CSR_DLYBCT(1));
#endif

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
		// by tri-moding the reset pin and applying internal pullup
		pinMode(pin_reset, INPUT);
		digitalWrite(pin_reset, HIGH);
 
		// We expect to see ATN go high to confirm SPI mode
		if (!wait_atn()) {
			// ATN did not go high
			XBEE_DEBUG(Serial.println(F("No ATN assert on reset")));
			return false;
		}

		// Tristate DOUT pin, we're done with it
		pinMode(pin_dout, INPUT);

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
void XbeeWifi::tx_frame(uint8_t type, unsigned int len, uint8_t *data)
{
	// Grab the SPI bus ASAP
	spiStart();

	// It is prudent to check for incoming frames cached, or otherwise we'd loose / corrupt
	// them as we transmit ours. By locking the SPI bus we prevent it from being released
	// after a packet is received (if a packet is received)
	spiLocked = true;
	process();

	// Safe now to send our packet and we must also make sure and unlock the SPI
	// bus so that when we deassert CS (spiEnd) later, it will in fact deassert
	spiLocked = false;

	// Calculate the proper checksum (sum of all bytes - type onward) subtracted from 0xFF
	uint8_t cs = type;
	for (unsigned int i = 0; i < len; i++) {
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
	spiEnd();
}

// Receive a SPI API frame
// Typically this is used to receive AT response frame
// It will trigger the asynchronous functions responsible for receiving other frame types as needed
// to ensure those frames get processed
int XbeeWifi::rx_frame(uint8_t *frame_type, unsigned int *len, uint8_t *data, int bufsize, unsigned long atn_wait_ms, bool return_status, bool single_ip_rx_only)
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
		spiStart();
		in = read();
		if (in != 0x7E) {
			XBEE_DEBUG(Serial.println(F("****** Failed in rx_frame, invalid start byte")));
			flush_spi();
			spiEnd();
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
				for (unsigned int i = 0 ; i < rxlen; i++) {
					in = read();
					cs += in;
					if (i < (unsigned int) bufsize) {
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
				*len = (rxlen > (unsigned int) bufsize) ? bufsize : rxlen;
				*frame_type = type;

				// And report appropriate status in return value
				spiEnd();
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
				//break; (implied by returns)

			default				:
				// This is an unexpected (possibly new, unsupported) frame
				// Drop it with debug
				XBEE_DEBUG(Serial.print(F("**** RX DROP Unsupported frame, type : 0x")));
				XBEE_DEBUG(Serial.println(type, HEX));
				for (unsigned int i = 0 ; i < rxlen + 1; i++) {
#ifdef XBEE_ENABLE_DEBUG
					uint8_t dropped = read();
					XBEE_DEBUG(Serial.print(F("Dropping : 0x")));
					XBEE_DEBUG(Serial.println(dropped, HEX));
#else
					read();
#endif
				}
				
				break;
		
		}
		spiEnd();
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
#ifdef XBEE_ENABLE_DEBUG
		uint8_t in = read();
#else
		read();
#endif
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
void XbeeWifi::process(bool rx_one_packet_only)
{
	int res;
	unsigned int len;
	uint8_t buf[XBEE_BUFSIZE];
	uint8_t type;
	do {
		// Receive frames with zero timeout
		// Since we're not currently expecting an exlicit response to anything
		// this simply serves to dispatch our asynchronous frames
		res = rx_frame(&type, &len, buf, XBEE_BUFSIZE, 0, false, true);

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

	// Create new empty sample record
	s_sample sample;
	memset(&sample, 0, sizeof(s_sample));

	// Initiate checksum processing
	uint8_t cs = XBEE_API_FRAME_IO_DATA_SAMPLE_RX;
	
	// Keep reading all data, reading in the appropriate values as they are reached
	for (unsigned int pos = 0 ; pos < len; pos++) {
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
				dispatch(buf, bufpos, &info);
				info.current_offset += bufpos;
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
	if (bufpos > 0) dispatch(buf, bufpos, &info);
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
		for (unsigned int i = 0 ; i < len + 1; i++) read();
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
	// Okay - let's grab the SPI bus and LOCK it (so that other agents within
	// this code base cannot release it)
	// This way we (hopefully) stop the Xbee from queuing up any inbound frames
	spiStart();
	spiLocked=true;

	// But it's possible that Xbee may have already had such frames pending
	// .. So deal with them
	if (digitalRead(pin_atn) == LOW) {
		XBEE_DEBUG(Serial.print(F("ATN asserted before transmit, call process")));
		process();
	}
	
	// Okay - the SPI bus should now be clear of incoming data
	// we are safe to send our own data...
	// We still have the SPI bus chip select locked.
	// Unlock it so that when the transmit releases it, it is actually released
	spiLocked = false;

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
#endif
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
	spiStart();
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
	spiEnd();

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
#endif // XBEE_OMIT_SCAN (CJB)

void XbeeWifi::dispatch(uint8_t *data, int len, s_rxinfo *info)
{
	XBEE_DEBUG(Serial.println(F("Non buffered dispatch")));
	if (ip_data_func) {
		callback_depth++;
		ip_data_func(data, len, info);
		callback_depth--;
	}
}

#ifndef XBEE_OMIT_RX_DATA
// Constructor for buffered XbeeWifi object
XbeeWifiBuffered::XbeeWifiBuffered(uint16_t bufsize) :
	bufsize(bufsize),
	head(0),
	tail(0),
	size(0),
	buffer_overrun(false)
{
	// Allocate memory to the buffer
	buffer = (uint8_t *) malloc(bufsize);
}

// Destructor. Can't really imagine where we'd be using it, but still
// memory allocation must be respected
XbeeWifiBuffered::~XbeeWifiBuffered()
{
	if (buffer) free(buffer);
}


// Override the virtual dispatch function from the parent object
// and instead of dispatching data to a callback, insert it into our circular
// buffer
void XbeeWifiBuffered::dispatch(uint8_t *data, int len, s_rxinfo *info)
{
	XBEE_DEBUG(Serial.print(F("(Buffered) Dispatching ")));
	XBEE_DEBUG(Serial.print(len, DEC));
	XBEE_DEBUG(Serial.println(F(" bytes to FIFO queue")));

	// For each incoming byte
	for (int i = 0 ; i < len; i ++) {
		// Make sure we have space
		if (size == bufsize) {
			// Oops, out of space. Remainder is dropped. Make sure overrun condition is flagged
			XBEE_DEBUG(Serial.println(F("FIFO overrun")));
			buffer_overrun = true;
			break;
		} else {
			// Add to head of buffer
			buffer[head++] = data[i];
			if (head == bufsize) head = 0;
			size++;
		}
	}
}

// Returns true if we have bytes available to read
bool XbeeWifiBuffered::available()
{
	if (size == 0) process();
	return (size > 0);
}

// Returns the next character but does not remove it from the buffer
// Always returns 0 if nothing available (but of course might return 0 if 0 is in the buffer too)
uint8_t XbeeWifiBuffered::peek()
{
	// If we have nothing in the buffer, it's time to process the SPI bus
	if (size == 0) process(false);

	if (size == 0) {
		// Still nothing, return 0
		return 0;
	} else {
		// Return tail of buffer
		return buffer[tail];
	}
}

// Returns the next character from the buffer
// Returns 0 if nothing available (same caveat as for peek)
uint8_t XbeeWifiBuffered::read()
{
	// If we have nothing in the buffer, it's time to process the SPI bus
	if (size == 0) process(false);

	if (size == 0) {
		// Still nothing, return 0
		return 0;
	} else {
		// Grab the buffer tail and advance / wrap the tail pointer
		uint8_t data = buffer[tail++];
		if (tail == bufsize) tail = 0;

		// Decrement the size since we just removed a byte and return the byte
		size--;
		return data;
	}
}

// Returns true if we overran the buffer
// Will reset state to false on every call unless reset is specified false
bool XbeeWifiBuffered::overran(bool reset)
{
	uint8_t result = buffer_overrun;
	if (reset) buffer_overrun = false;
	return result;
}

// Flush all content from the buffer
// Does not flush the SPI bus though.. This is deliberate.
void XbeeWifiBuffered::flush()
{
	head = tail = size = 0;
}
	

#endif
