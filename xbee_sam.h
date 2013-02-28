/*
 * File			xbee_sam.h
 *
 * Synopsis		Support macros for SAM based chips (Arduino DUE)
 *
 * Author		Chris Bearman
 *
 * Version		1.0
 *
 * License		This software is released under the terms of the Mozilla Public License (MPL) version 2.0
 * 			Full details of licensing terms can be found in the "LICENSE" file, distributed with this code
 *
 * Instructions		Tweak the content below to address specific requirements of the Arduino DUE 
 */
#ifndef __XBEESAM_H__
#define __XBEESAM_H__

/* Define ARCH_SAM which will be used elsewhere when instructions
   specific to this board type are needed */
#define ARCH_SAM

/* The SPI_BUS_DIVISOR can be any value 0..255

   Take the master clock (typically 84Mhz) and divide by this number
   this will be your SPI bus speed

   For example, with a 84Mhz clock, a value of 84 gives a 1Mhz clock

   According to datasheet the Xbee Wifi unit supports up to 3.5Mhz SPI
   bus which would equate to a divisor of 24 */
#define SPI_BUS_DIVISOR 84u

/* The Arduino DUE supports four chip selects, three of which are on real pins
   the fourth of which is not available. These are:

     BOARD_SPI_SS0	Digital 10 (internal pin #77)
     BOARD_SPI_SS1	Digital 4 (internal pin #87)
     BOARD_SPI_SS2	Digital 52 (internal pin #86)
     BOARD_SPI_SS3	(Not exposed on board? Need to check this..., internal pin #78)

   However, this library allows you to use any pin you wish by disassociating
   the real chip select pin from the PIO controller and controlling chip select
   manually. Still, the controller has to think it's using a pin. The code will
   make it think it's using BOARD_SPI_SS3 unless you actually use one of the intended
   CS pins. This is ideal, since this pin is not exposed on the board.

   You can change this if you wish here, although there is likely no need to do so */
#define SPI_CS_DEFAULT BOARD_SPI_SS3

/* Define the maximum size of our working buffers
   The maximum size of a UDP data portion on a 1500 byte MTU (ethernet)
   network seems like it's a reasonable choice given we have enough
   memory on this platform */
#define XBEE_BUFSIZE 1472

/* Insert a NOP loop of this many iterations after asserting and prior to clearing CS
   Needed for stability at higher SPI clock frequencies */
#define NOP_COUNT 1

/* For assistance with debugging, the F(xxx) macro assists in embedding
   strings in progmem.. But we don't do this on the SAM so the F(xxx) macro
   just does nothing except pass it's content straight through */
#define F(str) (str)

#endif // __XBEESAM_H__
