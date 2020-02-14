/*
The MIT License (MIT)

Copyright (c) 2017 Lancaster University.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#ifndef CODAL_JD_PHYSICAL_H
#define CODAL_JD_PHYSICAL_H

#include "CodalConfig.h"
#include "ErrorNo.h"
#include "Pin.h"
#include "Event.h"
#include "DMASingleWireSerial.h"
#include "LowLevelTimer.h"
#include "JDDeviceManager.h"

#define JD_SERIAL_VERSION               1

// various timings in microseconds
// 8 data bits, 1 start bit, 1 stop bit.
#define JD_BYTE_AT_125KBAUD                                 80
// the maximum permitted time between bytes
#define JD_MAX_INTERBYTE_SPACING                            (2 * JD_BYTE_AT_125KBAUD)
// the minimum permitted time between the data packets
#define JD_MIN_INTERFRAME_SPACING                           (2 * JD_BYTE_AT_125KBAUD)
// the time it takes for the bus to be considered in a normal state
#define JD_BUS_NORMALITY_PERIOD                             (2 * JD_BYTE_AT_125KBAUD)
// the minimum permitted time between the low pulse and data being received is 40 us
#define JD_MIN_INTERLODATA_SPACING                          40
// max spacing is 3 times 1 byte at minimum baud rate (240 us)
#define JD_MAX_INTERLODATA_SPACING                          (3 * JD_BYTE_AT_125KBAUD)

#define JD_SERIAL_MAX_BUFFERS           10

#define JD_SERIAL_MAX_SERVICE_NUMBER    15

#define JD_SERIAL_RECEIVING             0x0001
#define JD_SERIAL_TRANSMITTING          0x0004
#define JD_SERIAL_RX_LO_PULSE           0x0008
#define JD_SERIAL_TX_LO_PULSE           0x0010

#define JD_SERIAL_BUS_LO_ERROR          0x0020
#define JD_SERIAL_BUS_TIMEOUT_ERROR     0x0040
#define JD_SERIAL_BUS_UART_ERROR        0x0080
#define JD_SERIAL_ERR_MSK               0x00E0

#define JD_SERIAL_BUS_STATE             0x0100
#define JD_SERIAL_BUS_TOGGLED           0x0200

#define JD_SERIAL_DEBUG_BIT             0x8000

#define JD_SERIAL_EVT_DATA_READY       1
#define JD_SERIAL_EVT_BUS_ERROR        2
#define JD_SERIAL_EVT_CRC_ERROR        3
#define JD_SERIAL_EVT_DRAIN            4
#define JD_SERIAL_EVT_RX_TIMEOUT       5

#define JD_SERIAL_EVT_BUS_CONNECTED    5
#define JD_SERIAL_EVT_BUS_DISCONNECTED 6

#define JD_SERIAL_HEADER_SIZE          16
#define JD_SERIAL_CRC_HEADER_SIZE      2  // when computing CRC, we skip the CRC and version fields, so the header size decreases by three.
// 255 minus size of the serial header, rounded down to 4
#define JD_SERIAL_MAX_PAYLOAD_SIZE     236
#define JD_SERIAL_PAYLOAD_SIZE         236

#define JD_SERIAL_MAXIMUM_BUFFERS      10

#define JD_SERIAL_TX_MAX_BACKOFF       1000

#ifndef JD_RX_ARRAY_SIZE
#define JD_RX_ARRAY_SIZE               10
#endif

#ifndef JD_TX_ARRAY_SIZE
#define JD_TX_ARRAY_SIZE               10
#endif

#if CONFIG_ENABLED(JD_DEBUG)
#define JD_DMESG      codal_dmesg
#else
#define JD_DMESG(...) ((void)0)
#endif

#define JD_SERIAL_FLAG_DEVICE_ID_IS_RECIPIENT 0x01 // device_identifier is the intended recipient (and not source) of the message

#define JD_PACKED __attribute__((__packed__)) __attribute__((aligned(4)))


namespace codal
{
    class JDService;
    // a struct containing the various diagnostics of the JACDAC physical layer.
    struct JDDiagnostics
    {
        uint32_t bus_state;
        uint32_t bus_lo_error;
        uint32_t bus_uart_error;
        uint32_t bus_timeout_error;
        uint32_t packets_sent;
        uint32_t packets_received;
        uint32_t packets_dropped;
    };

    struct JDPacket
    {
        // transport header
        uint16_t crc; // CRC16-CCIT
        uint8_t version; // JD_SERIAL_VERSION (1)
        uint8_t serial_flags;
        uint64_t device_identifier;

        // logical header
        uint8_t size; // of the payload (data[])
        uint8_t service_number; // index in control packet
        uint8_t service_command; // service-specific
        uint8_t service_flags; // service-specific

        uint8_t data[JD_SERIAL_PAYLOAD_SIZE];
    } JD_PACKED;

    enum class JDBusState : uint8_t
    {
        Receiving,
        Transmitting,
        Error,
        Unknown
    };

    enum class JDSerialState : uint8_t
    {
        ListeningForPulse,
        ErrorRecovery,
        Off
    };

    enum JDBusErrorState : uint16_t
    {
        Continuation = 0,
        BusLoError = JD_SERIAL_BUS_LO_ERROR,
        BusTimeoutError = JD_SERIAL_BUS_TIMEOUT_ERROR,
        BusUARTError = JD_SERIAL_BUS_UART_ERROR // a different error code, but we want the same behaviour.
    };

    /**
    * Class definition for a JACDAC interface.
    */
    class JDPhysicalLayer : public CodalComponent
    {
        friend class USBJACDAC;

        uint8_t bufferOffset;
        JDService* sniffer;

    protected:
        DMASingleWireSerial&  sws;
        Pin&  sp;
        LowLevelTimer& timer;

        Pin* busLED;
        Pin* commLED;

        bool busLEDActiveLo;
        bool commLEDActiveLo;

        JDSerialState state;

        uint32_t startTime;
        uint32_t lastBufferedCount;

        void loPulseDetected(uint32_t);
        int setState(JDSerialState s);
        void dmaComplete(Event evt);

        JDPacket* popRxArray();
        JDPacket* popTxArray();
        int addToTxArray(JDPacket* packet);
        int addToRxArray(JDPacket* packet);
        void sendPacket();
        void errorState(JDBusErrorState);

        /**
          * Sends a packet using the SingleWireSerial instance. This function begins the asynchronous transmission of a packet.
          * If an ongoing asynchronous transmission is happening, JD is added to the txQueue. If this is the first packet in a while
          * asynchronous transmission is begun.
          *
          * @returns DEVICE_OK on success, DEVICE_INVALID_PARAMETER if JD is NULL, or DEVICE_NO_RESOURCES if the queue is full.
          */
        virtual int queuePacket(JDPacket *p);

    public:

        static JDPhysicalLayer* instance;

        uint8_t txHead;
        uint8_t txTail;
        uint8_t rxHead;
        uint8_t rxTail;

        JDPacket* rxBuf; // holds the pointer to the current rx buffer
        JDPacket* txBuf; // holds the pointer to the current tx buffer
        JDPacket* rxArray[JD_RX_ARRAY_SIZE];
        JDPacket* txArray[JD_TX_ARRAY_SIZE];

        /**
          * Constructor
          *
          * @param sws an instance of sws.
          *
          * @param busStateLED an instance of a pin, used to display the state of the bus.
          *
          * @param commStateLED an instance of a pin, used to display the state of the bus.
          *
          * @param baud Defaults to 1mbaud
          */
        JDPhysicalLayer(DMASingleWireSerial&  sws, LowLevelTimer& timer, Pin* busStateLED = NULL, Pin* commStateLED = NULL, bool busLEDActiveLo = false, bool commLEDActiveLo = false, uint16_t id = DEVICE_ID_JACDAC_PHYS);

        /**
          * Retrieves the first packet on the rxQueue regardless of the device_class
          *
          * @returns the first packet on the rxQueue or NULL
          */
        JDPacket *getPacket();

        /**
          * Causes this instance of JACDAC to begin listening for packets transmitted on the serial line.
          */
        virtual void start();

        /**
          * Causes this instance of JACDAC to stop listening for packets transmitted on the serial line.
          */
        virtual void stop();

        int send(JDPacket* tx, bool computeCRC = true);

        /**
          * Sends a packet using the SingleWireSerial instance. This function begins the asynchronous transmission of a packet.
          * If an ongoing asynchronous transmission is happening, JD is added to the txQueue. If this is the first packet in a while
          * asynchronous transmission is begun.
          *
          * @param buf the buffer to send.
          *
          * @param len the length of the buffer to send.
          *
          * @returns DEVICE_OK on success, DEVICE_INVALID_PARAMETER if buf is NULL or len is invalid, or DEVICE_NO_RESOURCES if the queue is full.
          */
        int send(uint8_t* buf, int len, uint8_t service_number, uint32_t service_identifier, JDDevice* device);

        /**
         * Returns a bool indicating whether the JACDAC driver has been started.
         *
         * @return true if started, false if not.
         **/
        bool isRunning();

        /**
         * Returns the current state if the bus.
         *
         * @return true if connected, false if there's a bad bus condition.
         **/
        bool isConnected();

        /**
         * Returns the current state of the bus, either:
         *
         * * Receiving if the driver is in the process of receiving a packet.
         * * Transmitting if the driver is communicating a packet on the bus.
         *
         * If neither of the previous states are true, then the driver looks at the bus and returns the bus state:
         *
         * * High, if the line is currently floating high.
         * * Lo if something is currently pulling the line low.
         **/
        JDBusState getState();

        uint8_t getErrorState();
        JDDiagnostics getDiagnostics();

        void _timerCallback(uint16_t channels);
        void _dmaCallback(uint16_t errCode);
        void _gpioCallback(int state);
    };
} // namespace codal

#endif
