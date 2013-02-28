/*
 * File			xbee_atmega.h
 *
 * Synopsis		Support macros for ATMEGA based chips (Uno, etc..)
 *
 * Author		Chris Bearman
 *
 * Version		1.0
 *
 * License		This software is released under the terms of the Mozilla Public License (MPL) version 2.0
 * 			Full details of licensing terms can be found in the "LICENSE" file, distributed with this code
 *
 * Instructions		Tweak the content below to address specific requirements of the non ARM based Arduinos
 */
#ifndef __XBEEATMEGA_H__
#define __XBEEATMEGA_H__

/* Define ARCH_ATMEGA which will be used elsewhere when instructions
   specific to this board type are needed */
#define ARCH_ATMEGA

/* 
   The SPI_BUS_DIVISOR can be one of the following values:
   2,4,8,16,32,64,128

   Take the master clock (typically 16Mhz) and divide by this number
   this will be your SPI bus speed

   For example, with a 16Mhz clock, a value of 8 gives a 1Mhz clock

   According to datasheet the Xbee Wifi unit supports up to 3.5Mhz SPI
   bus.

*/
#define SPI_BUS_DIVISOR 8

/* Buffer size - keep it small on this platform 
   You can reduce this if you're running low on DRAM, but if that's 
   the case you're likely already in trouble... 
   Keep this value >=48 bytes as an absolute minimum */
#define XBEE_BUFSIZE 128

/* Implementation of various speeds, don't mess with this */
#if SPI_BUS_DIVISOR == 2
// FCPU/2 (8Mhz typical)
#define XBEE_SPCR (1 << SPE) | (1 << MSTR)
#define XBEE_SPSR (1 << SPI2X)
#elif SPI_BUS_DIVISOR == 4
// FCPU/4 (4Mhz typical)
#define XBEE_SPCR (1 << SPE) | (1 << MSTR)
#define XBEE_SPSR 0x00
#elif SPI_BUS_DIVISOR == 8
// FCPU/8 (2Mhz typical)
#define XBEE_SPCR (1 << SPE) | (1 << MSTR) | (1 << SPR0)
#define XBEE_SPSR (1 << SPI2X)
#elif SPI_BUS_DIVISOR == 32
// FCPU/32 (500Khz typical)
#define XBEE_SPCR (1 << SPE) | (1 << MSTR) | (1 << SPR1)
#define XBEE_SPSR (1 << SPI2X)
#elif SPI_BUS_DIVISOR == 64
// FCPU/64 (250Khz typical)
#define XBEE_SPCR (1 << SPE) | (1 << MSTR) | (1 << SPR1)
#define XBEE_SPSR 0x00
#elif SPI_BUS_DIVISOR == 128
// FCPU/128 (125Khz typical)
#define XBEE_SPCR (1 << SPE) | (1 << MSTR) | (1 << SPR1) | (1 << SPR0)
#define XBEE_SPSR 0x00
#else 
// FCPU/16 (1Mhz typical, default)
#define XBEE_SPCR (1 << SPE) | (1 << MSTR) | (1 << SPR0)
#define XBEE_SPSR 0x00
#endif

/* Delay for post CS assert and pre CS retract */
#define NOP_COUNT 1

/* For assistance with debugging, the F(xxx) macro assists in embedding
   strings in progmem  */
class __FlashStringHelper;
#define F(str) reinterpret_cast<__FlashStringHelper *>(PSTR(str))

#endif // __XBEEATMEGA_H__
