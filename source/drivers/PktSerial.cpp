#include "PktSerial.h"
#include "Event.h"
#include "EventModel.h"
#include "codal_target_hal.h"
#include "CodalDmesg.h"
#include "CodalFiber.h"
#include "SingleWireSerial.h"
#include "Timer.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define CODAL_ASSERT(cond)                                                                         \
    if (!(cond))                                                                                   \
    target_panic(909)

using namespace codal;

void PktSerial::dmaComplete(Event evt)
{
    DBG_DMESG("DMA");
    if (evt.value == SWS_EVT_ERROR)
    {
        DBG_DMESG("ERR");
        if (status & PKT_SERIAL_TRANSMITTING)
        {
            DBG_DMESG("TX ERROR");
            status &= ~(PKT_SERIAL_TRANSMITTING);
            free(txBuf);
            txBuf = NULL;
        }

        if (status & PKT_SERIAL_RECEIVING)
        {
            DBG_DMESG("RX ERROR");
            status &= ~(PKT_SERIAL_RECEIVING);
            timeoutCounter = 0;
            sws.abortDMA();
            Event(this->id, PKT_SERIAL_EVT_BUS_ERROR);
        }
    }
    else
    {
        // rx complete, queue packet for later handling
        if (evt.value == SWS_EVT_DATA_RECEIVED)
        {
            status &= ~(PKT_SERIAL_RECEIVING);
            // move rxbuf to rxQueue and allocate new buffer.
            addToQueue(&rxQueue, rxBuf);
            rxBuf = (PktSerialPkt*)malloc(sizeof(PktSerialPkt));
            Event(id, PKT_SERIAL_EVT_DATA_READY);
        }

        if (evt.value == SWS_EVT_DATA_SENT)
        {
            status &= ~(PKT_SERIAL_TRANSMITTING);
            free(txBuf);
            txBuf = NULL;
            // we've finished sending... trigger an event in random us (in some cases this might not be necessary, but it's not too much overhead).
            system_timer_event_after_us(4000, this->id, PKT_SERIAL_EVT_DRAIN);  // should be random
        }
    }

    sws.setMode(SingleWireDisconnected);

    // force transition to output so that the pin is reconfigured.
    sp.setDigitalValue(1);
    configure(true);
}

void PktSerial::onFallingEdge(Event)
{

    DBG_DMESG("FALL: %d %d", (status & PKT_SERIAL_RECEIVING) ? 1 : 0, (status & PKT_SERIAL_TRANSMITTING) ? 1 : 0);
    // guard against repeat events.
    if (status & (PKT_SERIAL_RECEIVING | PKT_SERIAL_TRANSMITTING) || !(status & DEVICE_COMPONENT_RUNNING))
        return;

    sp.eventOn(DEVICE_PIN_EVENT_NONE);
    sp.getDigitalValue(PullMode::None);

    timeoutCounter = 0;
    status |= (PKT_SERIAL_RECEIVING);

    DBG_DMESG("RX START");
    sws.receiveDMA((uint8_t*)rxBuf, PKT_SERIAL_PACKET_SIZE);
}

void PktSerial::periodicCallback()
{
    // calculate 1 packet at baud
    if (timeoutCounter == 0)
    {
        uint32_t timePerSymbol = 1000000/sws.getBaud();
        timePerSymbol = timePerSymbol * 100 * PKT_SERIAL_PACKET_SIZE;
        timeoutValue = (timePerSymbol / SCHEDULER_TICK_PERIOD_US);
    }

    if (status & PKT_SERIAL_RECEIVING)
    {
        DBG_DMESG("H");
        timeoutCounter++;

        if (timeoutCounter > timeoutValue)
        {
            DBG_DMESG("TIMEOUT");
            sws.abortDMA();
            Event(this->id, PKT_SERIAL_EVT_BUS_ERROR);
            timeoutCounter = 0;
            status &= ~(PKT_SERIAL_RECEIVING);

            sws.setMode(SingleWireDisconnected);
            sp.setDigitalValue(1);
            configure(true);
        }
    }
}

PktSerialPkt* PktSerial::popQueue(PktSerialPkt** queue)
{
    if (*queue == NULL)
        return NULL;

    PktSerialPkt *ret = *queue;

    target_disable_irq();
    *queue = (*queue)->next;
    target_enable_irq();

    return ret;
}

PktSerialPkt* PktSerial::removeFromQueue(PktSerialPkt** queue, uint8_t address)
{
    if (*queue == NULL)
        return NULL;

    PktSerialPkt* ret = NULL;

    target_disable_irq();
    PktSerialPkt *p = (*queue)->next;
    PktSerialPkt *previous = *queue;

    if (address == (*queue)->address)
    {
        *queue = p;
        ret = previous;
    }
    else
    {
        while (p != NULL)
        {
            if (address == p->address)
            {
                ret = p;
                previous->next = p->next;
                break;
            }

            previous = p;
            p = p->next;
        }
    }

    target_enable_irq();

    return ret;
}

int PktSerial::addToQueue(PktSerialPkt** queue, PktSerialPkt* packet)
{
    int queueDepth = 0;
    packet->next = NULL;

    target_disable_irq();
    if (*queue == NULL)
        *queue = packet;
    else
    {
        PktSerialPkt *p = *queue;

        while (p->next != NULL)
        {
            p = p->next;
            queueDepth++;
        }

        if (queueDepth >= PKT_SERIAL_MAXIMUM_BUFFERS)
        {
            free(packet);
            return DEVICE_NO_RESOURCES;
        }

        p->next = packet;
    }
    target_enable_irq();

    return DEVICE_OK;
}

void PktSerial::configure(bool events)
{
    sp.getDigitalValue(PullMode::Up);

    if(events)
        sp.eventOn(DEVICE_PIN_EVENT_ON_EDGE);
    else
        sp.eventOn(DEVICE_PIN_EVENT_NONE);
}

/**
 * Constructor
 *
 * @param p the transmission pin to use
 *
 * @param sws an instance of sws created using p.
 */
PktSerial::PktSerial(codal::Pin& p, DMASingleWireSerial&  sws, uint16_t id) : sws(sws), sp(p)
{
    rxBuf = NULL;
    txBuf = NULL;

    rxQueue = NULL;
    txQueue = NULL;

    this->id = id;
    status = 0;

    timeoutValue = 0;
    timeoutCounter = 0;

    sws.setBaud(1000000);
    sws.setDMACompletionHandler(this, &PktSerial::dmaComplete);

    if (EventModel::defaultEventBus)
    {
        EventModel::defaultEventBus->listen(sp.id, DEVICE_PIN_EVT_FALL, this, &PktSerial::onFallingEdge, MESSAGE_BUS_LISTENER_IMMEDIATE);
        EventModel::defaultEventBus->listen(this->id, PKT_SERIAL_EVT_DRAIN, this, &PktSerial::sendPacket, MESSAGE_BUS_LISTENER_IMMEDIATE);
    }
}

/**
 * Retrieves the first packet on the rxQueue irregardless of the device_class
 *
 * @returns the first packet on the rxQueue or NULL
 */
PktSerialPkt* PktSerial::getPacket()
{
    return popQueue(&rxQueue);
}

/**
 * Retrieves the first packet on the rxQueue with a matching device_class
 *
 * @param address the address filter to apply to packets in the rxQueue
 *
 * @returns the first packet on the rxQueue matching the device_class or NULL
 */
PktSerialPkt* PktSerial::getPacket(uint8_t address)
{
    return removeFromQueue(&rxQueue, address);
}

/**
 * Causes this instance of PktSerial to begin listening for packets transmitted on the serial line.
 */
void PktSerial::start()
{
    if (rxBuf == NULL)
        rxBuf = (PktSerialPkt*)malloc(sizeof(PktSerialPkt));

    configure(true);

    target_disable_irq();
    status = 0;
    status |= (DEVICE_COMPONENT_RUNNING | DEVICE_COMPONENT_STATUS_SYSTEM_TICK);
    target_enable_irq();

    Event evt(0, 0, CREATE_ONLY);

    // if the line is low, we may be in the middle of a transfer, manually trigger rx mode.
    if (sp.getDigitalValue(PullMode::Up) == 0)
    {
        DBG_DMESG("TRIGGER");
        onFallingEdge(evt);
    }

    sp.eventOn(DEVICE_PIN_EVENT_ON_EDGE);
}

/**
 * Causes this instance of PktSerial to stop listening for packets transmitted on the serial line.
 */
void PktSerial::stop()
{
    status &= ~(DEVICE_COMPONENT_RUNNING | DEVICE_COMPONENT_STATUS_SYSTEM_TICK);
    if (rxBuf)
    {
        free(rxBuf);
        rxBuf = NULL;
    }

    configure(false);
}

void PktSerial::sendPacket(Event)
{
    status |= PKT_SERIAL_TX_DRAIN_ENABLE;

    // if we are receiving, randomly back off
    if (status & PKT_SERIAL_RECEIVING)
    {
        DBG_DMESG("RXing");
        system_timer_event_after_us(4000, this->id, PKT_SERIAL_EVT_DRAIN);  // should be random
        return;
    }

    if (!(status & PKT_SERIAL_TRANSMITTING))
    {
        // if the bus is lo, we shouldn't transmit
        if (sp.getDigitalValue(PullMode::Up) == 0)
        {
            DBG_DMESG("BUS LO");
            Event evt(0, 0, CREATE_ONLY);
            onFallingEdge(evt);
            system_timer_event_after_us(4000, this->id, PKT_SERIAL_EVT_DRAIN);  // should be random
            return;
        }

        // performing the above digital read will disable fall events... re-enable
        sp.setDigitalValue(1);
        configure(true);

        // if we have stuff in our queue, and we have not triggered a DMA transfer...
        if (txQueue)
        {
            DBG_DMESG("TX B");
            status |= PKT_SERIAL_TRANSMITTING;
            txBuf = popQueue(&txQueue);

            sp.setDigitalValue(0);
            target_wait_us(10);
            sp.setDigitalValue(1);

            // return after 100 us
            system_timer_event_after_us(100, this->id, PKT_SERIAL_EVT_DRAIN);
            return;
        }
    }

    // we've returned after a DMA transfer has been flagged (above)... start
    if (status & PKT_SERIAL_TRANSMITTING)
    {
        DBG_DMESG("TX S");
        sws.sendDMA((uint8_t *)txBuf, PKT_SERIAL_PACKET_SIZE);
        return;
    }

    // if we get here, there's no more to transmit
    status &= ~(PKT_SERIAL_TX_DRAIN_ENABLE);
    return;
}

/**
 * Sends a packet using the SingleWireSerial instance. This function begins the asynchronous transmission of a packet.
 * If an ongoing asynchronous transmission is happening, pkt is added to the txQueue. If this is the first packet in a while
 * asynchronous transmission is begun.
 *
 * @param pkt the packet to send.
 *
 * @returns DEVICE_OK on success, DEVICE_INVALID_PARAMETER if pkt is NULL, or DEVICE_NO_RESOURCES if the queue is full.
 */
int PktSerial::send(PktSerialPkt *pkt)
{
    int ret = addToQueue(&txQueue, pkt);

    if (!(status & PKT_SERIAL_TX_DRAIN_ENABLE))
    {
        Event e(0,0,CREATE_ONLY);
        sendPacket(e);
    }

    return ret;
}

/**
 * Sends a packet using the SingleWireSerial instance. This function begins the asynchronous transmission of a packet.
 * If an ongoing asynchronous transmission is happening, pkt is added to the txQueue. If this is the first packet in a while
 * asynchronous transmission is begun.
 *
 * @param buf the buffer to send.
 *
 * @param len the length of the buffer to send.
 *
 * @returns DEVICE_OK on success, DEVICE_INVALID_PARAMETER if buf is NULL or len is invalid, or DEVICE_NO_RESOURCES if the queue is full.
 */
int PktSerial::send(uint8_t* buf, int len, uint8_t address)
{
    if (buf == NULL || len <= 0 || len >= PKT_SERIAL_DATA_SIZE)
        return DEVICE_INVALID_PARAMETER;

    PktSerialPkt* pkt = (PktSerialPkt*)malloc(sizeof(PktSerialPkt));
    memset(pkt, 0, sizeof(PktSerialPkt));

    // very simple crc
    pkt->crc = 0;
    pkt->address = address;
    pkt->size = len;

    memcpy(pkt->data, buf, len);

    // skip the crc.
    uint8_t* crcPointer = (uint8_t*)&pkt->address;

    // simple crc
    for (int i = 0; i < PKT_SERIAL_PACKET_SIZE - 2; i++)
        pkt->crc += crcPointer[i];

    return send(pkt);
}

bool PktSerial::isRunning()
{
    return (status & DEVICE_COMPONENT_RUNNING) ? true : false;
}