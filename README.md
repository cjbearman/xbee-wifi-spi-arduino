Arduino library for XBEE Wifi module, using SPI communication
=============================================================

Note: 

This library is not for use with non-wifi (Zigbee or other) XBEE modules. It is also not for use with the UART mode of the Wifi XBEE module. It implements the SPI interface to the XBEE Wifi module only. The SPI interface is optimal to utilize the full network functionality of the Wifi XBEE at the expense of GPIO lines used for the SPI bus integration.

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
-----------------

To register a function to receive inbound IP data register a function of the following prototype:

        void my_ip_inbound_function(uint8_t *data, int len, s_rxinfo *info);

Using the register_ip_data_callback method:

        xbee.register_ip_data_callback(my_ip_inbound_function);

Your function will be called when you run the process() method if there is IP data inbound. The data is provided in the data parameter, and the length of the data in the len function. The info structure contains information about the data, such as which IP:Port sent it.

Since we have limited memory, the maximum amount of data you'll receive in any one call to this callback will be 128 bytes (size of XBEE_BUFSIZE defined in XbeeWifi.h). This means that if an IP packet comes in with > 128 bytes, it will be fragmented and sent to you in multiple calls. 

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
----------------------
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
---------------------------
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



Optimizations
=============
This is a pretty large library. Arduino and avr-gcc are good at optimizing out unused methods, however, due to the callback nature of the library some functions will be included even when they are not needed.

A set of #defines are provided in the XbeeWifi.h function that can be uncommented to reduce the size of the final binary, at the expense of loosing some functionality.

You will find a list of these optional defines commented out at the top of the .h file. Simple uncomment then to limit the functionality and reduce sketch size.

