Arduino library for XBEE Wifi module, using SPI communication
=============================================================

Notes: 

This library is not for use with non-wifi (Zigbee or other) XBEE modules. It is also not for use with the UART mode of the Wifi XBEE module. It implements the SPI interface to the XBEE Wifi module only. The SPI interface is optimal to utilize the full network functionality of the Wifi XBEE at the expense of GPIO lines used for the SPI bus integration.

Note that using integrating with the Xbee Wifi using SPI is a demanding job for a small microcontroller. Simple communications problems are often better solved UART interface to the device. This library does not provide support for the much simpler requirements of integration through the UART interface - it is soley dedicated to integration using the more complex SPI interface.

I am no longer actively maintaining this library. I will generally answer questions if time permits, however, please be aware you're going to be largely on your own.

Feb 17 2015
Since this library was published a couple of years ago, Digi has released a new version of the Xbee wifi. Although I believe this library will work with the new chip, there will at the very least be some unsupported functions, such as the new cloud management interactions provided by the device. If time permits in the near future, I will purchase one of the new variants and update the library to support it.

Hardware Configuration
======================
        Arduino                                         XBEE
        MISO                       <--->                MISO        (required)
        MOSI                       <--->                MOSI        (required)
        SCLK                       <--->                SCLK        (required)
        SS or other pin            <--->                CS          (required)
        digital pin                <--->                ATN         (required)
        digital pin                <--->                RESET       (optional)
        digital pin                <--->                DOUT        (optional)

The optional pin connections ARE optionally used to force the XBEE module into SPI mode during initialization. If not provided then the XBEE must be pre-configured (using XCTU or other methods) with the correct pin assignments for SPI operation before use with this library. It is important to note that the Xbee is not directly compatible with 5v Arduino devices. The Xbee requires 3.3v power and signals. If using a 5v Arduino, you *MUST* provide a 3.3v power supply for the Xbee and you *MUST* convert the signal leverl for *ALL* connections to 3.3v using appropriate level shifting hardware.

Make sure and pay attention to the instructions on power supply design in the Xbee Wifi manual - particuarly the capacitor recommendations. When running with an Arduino based board powered at 3.3v I have encounted issues tripping BOD (brown out) resets on the microcontroller. Lowering the BOD threshold in the microcontrolelr fuses as well as providing plenty (1000uF+) of capacitance on the power feed is advisible.

This library supports both Arduino Uno (and similar) boards based on ATMEGA chipset and Arduino Due based on SAM chipset. Support for Due is new and is likely less stable at this time.

SPI Bus Speed
=============
The Xbee Wifi chip (per spec sheet) supports a maximum SPI bus speed of 3.5Mhz.

The SPI bus speed is set by a macro definition in either:
	xbee_atmega.h		For Uno and similar (ATMEGA chipset based) boards
	xbee_sam.h		For Due

The default SPI bus speed I am using is 1Mhz. This is very conservative (and assumes a 16Mhz clock for ATMEGA devices, 84Mhz clock for Arduino DUE). The clock speed definition can be easily changed by altering the clock divisor setting in either of the above files. Just read the comments and change the SPI_BUS_DIVISOR macro, per the instructions.

Technicaly 3.5Mhz can be achieved, exactly, on Arduino Due - I have tested this without issue.

Due to the lower primary clock speed and more limited divisor options on the ATMEGA based boards, you are probably limited to using 2Mhz as a maximum unless you're using a non-standard crystal. I have tried overclocking to 4Mhz without success.

Primary Functions
=================
Send / Receive IP packets to/from any IP address using both native IPv4 and application compatability modes (port 0xBEE) as provided by the Wifi XBEE device.
Issue AT (control) commands to the local XBEE and remote XBEE devices
Receive data samples from remote XBEE devices
Remove modem status indications from local XBEE device
Initiate and receive active network scan data from local XBEE device

Installation
============
Create a directory called XbeeWifi under sketches/libraries - where sketches is your base sketches directory. Check out this GIT repository to the new directory.

Restart Arduino IDE. You should now be able to import the XbeeWifi library and find XbeeWifi examle sketches.

Connecting to the Xbee
======================

FIrst of all figure out which pins you are using on your Arduino.

        #define XBEE_RESET 20
        #define XBEE_ATN 2
        #define XBEE_SELECT SS
        #define XBEE_DOUT 23

Import the XbeeWifi library into your sketch. Create an XbeeWifi object:

        XbeeWifi xbee;

Initialize the Xbee from your setup routine:

        if (!xbee.init(XBEE_SELECT, XBEE_ATN, XBEE_RESET, XBEE_DOUT)) {
                // Failed to initialize
        }


Configuring the Xbee
====================
The XbeeWifi library provides functions for you to make AT calls to the Xbee. These are used to configure the device. Here is a typical configuration that might be used to configure the Xbee for connection to a typical WPA2 encrypted home network:

        xbee.at_cmd_byte(XBEE_AT_NET_TYPE, XBEE_NET_TYPE_IBSS_INFRASTRUCTURE);
        xbee.at_cmd_byte(XBEE_AT_NET_IPPROTO, XBEE_NET_IPPROTO_TCP);
        xbee.at_cmd_str(XBEE_AT_NET_SSID, "my_network");
        xbee.at_cmd_byte(XBEE_AT_NET_ADDRMODE, XBEE_NET_ADDRMODE_DHCP);
        xbee.at_cmd_short(XBEE_AT_ADDR_SERIAL_COM_SERVICE_PORT, 12345);
        xbee.at_cmd_byte(XBEE_AT_SEC_ENCTYPE, XBEE_SEC_ENCTYPE_WPA2);
        xbee.at_cmd_str(XBEE_AT_SEC_KEY, "MyVerySecretPassphrase");

Each at_cmd_xxxx function returns a boolean (true or false) to indicate success / failure. By default each command will wait for confirmation from the Xbee before returning. Other parameters on these functions allow you to skip confirmation if you wish.

Note that remote versions of these comamnds are available for issuing at commands on remote Xbees.

Servicing the Xbee
==================
You must call xbee.prorcess() continuously - typically once during each iteration of your loop() method. You must call this method frequently since it services any inbound data from the Xbee. Failure to call this method frequently will result in SPI buffer overruns and loss of data. The process method will in turn call your registered callback methods as and when data is available for them.

Registering For Callbacks
=========================
Assuming you want to receive data from the Xbee, you will want to register for one or more of four possible callback functions. If you don't register one or more of these callbacks then any inbound data associated with them is silently discarded.

IP Data Reception
=================

To register a function to receive inbound IP data register a function of the following prototype:

        void my_ip_inbound_function(uint8_t *data, int len, s_rxinfo *info);

Using the register_ip_data_callback method:

        xbee.register_ip_data_callback(my_ip_inbound_function);

Your function will be called when you run the process() method if there is IP data inbound. The data is provided in the data parameter, and the length of the data in the len function. The info structure contains information about the data, such as which IP:Port sent it.

The maximum amount of data received in any call is defined by XBEE_BUFSIZE macro (define in xbee_atmega.h). The default for ATMEGA based platforms is 128 bytes, meaning if more than 128 bytes come in in a single packet, it will be framented and delivered to you in multiple calls.

Increase the size of this if memory allows and you are expecting larger packets. Decrease as low as 48 bytes if you're tragically short on DRAM.

For Arduino Due users, this is defined in xbee_sam.h at a default of 1472 bytes which should be enough to convey most packets in a single call (based on a 1500 MTU less minimum headers).

If it's necessary to reconstruct the entire packet, you must buffer it up and track the reassembly. The following elements of the info structure contain information to assist with reassembly:

        sequence
                .. Starts at zero for first segment of each packet. Increments for each successive segment.
        total_packet_length
                .. The total size of the incoming packet
        current_offset
                .. Starts at zero for first segment. Increments to indicate of the current offset of the incoming segment within the overall packet.
        final
                .. Indicates that this is the final segment to be sent for this packet
        checksum_error
                .. Indicates that a checksum error occurred on this packet

Note that the checksum error will be only set to true on the final packet (final = true) because we don't know until that point. If the source / destination port is 0xBEE then this would indicate a packet received by the Xbee application compatability mode, which exclusively uses this port.

Modem Status Reception
======================
To register for modem status updates, register a function of the following prototype:

        void my_modem_status_function(uint8_t status);

Using the register_status_callback method

        xbee.register_status_callback(my_modem_status_function);

The status value received can be compared against one of the following defined values:

        XBEE_MODEM_STATUS_RESET                 0x00
        XBEE_MODEM_STATUS_WATCHDOG_RESET        0x01
        XBEE_MODEM_STATUS_JOINED                0x02
        XBEE_MODEM_STATUS_NO_LONGER_JOINED      0x03
        XBEE_MODEM_STATUS_IP_CONFIG_ERROR       0x04
        XBEE_MODEM_STATUS_S_OR_J_WITHOUT_CON    0x82
        XBEE_MODEM_STATUS_AP_NOT_FOUND          0x83
        XBEE_MODEM_STATUS_PSK_NOT_CONFIGURED    0x84
        XBEE_MODEM_STATUS_SSID_NOT_FOUND        0x87
        XBEE_MODEM_STATUS_FAILED_WITH_SECURITY  0x88
        XBEE_MODEM_STATUS_INVALID_CHANNEL       0x8A
        XBEE_MODEM_STATUS_FAILED_TO_JOIN        0x8E

Remote Data Sample callback
===========================
The remote sample callback is used to receive remote IO data samples from a remote Xbee. Should such a sample arrive, it will be dispatched using this callback.

To register for sample callback, register a function of the following prototype:

        void my_sample_callback(s_sample *sample);

Using the register_sample_callback method

        xbee.register_sample_callback(sample);

The contents of the sample structure contain the various sample fields as defined in the Xbee documentation. Namely masks and data representations for the various possible sampled IO ports.

Network Scan callback
---------------------

If you wish to scan for networks you must register the scan callback.

To register for scan callback, register a function of the following prototype:

        void my_callback(uint8_t encryption_mode, int rssi, char *ssid);

Using the register_scan_callback method

        xbee.register_scan_callback(my_scan_callback);

To scan for networks you must then call initiateScan() 

        xbee.initiateScan()

Your callback function will be called (by process) for each network found.

Note that initiating a network scan will force the Xbee to reset it's network parameters, causing you to disconnect from any connected network. You must reconfigure the network settings of the Xbee using appropriate AT commands after using the scan should you wish it to reconnect.


Stack Safety
============

Both sending AT commands and transmiting data typically involve receiving a confirmation on the SPI bus once the operation completes. Since the SPI bus is serial and other data may be pending, please consider the following restrictions:

        AT commands will be aborted automatically and not sent. The at functions will return false.

        Transmission of data will be forced as unacknowledged.

Why? Because other data may be pending on the SPI bus that data must be serviced before the confirmation of the AT command or transmission can occur. Accordingly, this could cause recursion of your callback functions and possible stack overflow and certainly complex behaviors.

Although it is possible to transmit (without confirmation) from inside a callback, it is not advised since it would be very hard to prevent SPI buffer overruns.


Buffered Reception
==================
The callback pattern described above is used because:
a) We don't have much memory to work with
b) We cannot predict what packets (IP data, modem status etc..) may be inbound at any point in time from the device.

So unless we want to buffer up data, which we probably don't have the memory to do, we need to be flexible in when the data is delivered. Hence the callback pattern.

HOWEVER... If you really REALLY dislike callbacks, and have a simple application case, an alternative implementation is also provided. The XbeeWifiBuffered class is very similar to XbeeWifi, however, when constructing the object you specify a number of bytes (say 1024) to use as a buffer. You don't register for IP data callback, instead, you use the "available" and "read" methods to read data from the buffer. 

This is a more risk approach, althogh much easier to code to:

Problems:
	1. You're chewing up memory
		.. Less of a problem on bigger boards with more DRAM such as Mega or Due.
	2. More risk of data loss
		.. If a packet comes in that exceeds the buffer size, some data WILL be discarded
		.. Other cases can cause data loss if you're not servicing the object fast enough.

See the "buffered" example sketch for more information on using this mode.

However, consider that if you're considering this approach you might be better off using the serial (non SPI) mode of the Xbee.

Note that you do not have to call "process" when using this object. The calls to available() and read() will ensure that the bus is serviced.

You should always read all data on every application loop to avoid potential data loss! I.E. keep read()ing until available() returns false.

All other functions are unmodified (i.e. use callbacks for scanning, modem status etc..). It is recommended to empty the buffer PRIOR to using any other command (such as issuing an AT command operation). Reason here is that using one of these other commands may require the buffer on the Xbee to be flushed out to get to the new command response, possibly overwhelming the receive buffer if data is still pending.

Optimizations
=============
This is a pretty large library. Arduino and avr-gcc are good at optimizing out unused methods, however, due to the callback nature of the library some functions will be included even when they are not needed.

A set of #defines are provided in the XbeeWifi.h function that can be uncommented to reduce the size of the final binary, at the expense of loosing some functionality.

You will find a list of these optional defines commented out at the top of the .h file. Simple uncomment then to limit the functionality and reduce sketch size.


Limitations
===========
Support for the Arduino DUE is now provided. It is experimental at this time. 

Adding other devices to the SPI bus at the same time as the XbeeWifi, using this library should be supported - but is not tested.

I have outstanding questions on whether it is possible for a packet to be dispatched (Xbee -> Arduino) on the SPI bus during the transmission of a packet (Arduino -> Xbee). It appears that this does not occur. I have not found an instance of the ATTN line being asserted, or the reception of a 0x7E (start byte) from the Xbee during transmission of a packet. A good test for this is sending a transmission from the xbee to it's own IP. This behaves mostly as expected- although there appears to be an Xbee bug on receipt - the received packet comes back over SPI but the IP address is all zeros and the data is corrupt. I will send an email to Digi about this minor problem, as well as questions over the details of the SPI bus implementation.

Since there is no obvious instance where an XBEE -> Arduino transmission commences AFTER the Arduino asserts the chip select, this case is not handled by the library (which is a relief because that would require buffering and extra RAM consumption).
