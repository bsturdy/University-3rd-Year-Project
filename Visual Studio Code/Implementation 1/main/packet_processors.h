#ifndef packet_processors_H
#define packet_processors_H

#include <cstddef>
#include <cstdint>
#include "UtilitiesClass.h"
#include "sdkconfig.h"
#include <cstring>

constexpr uint16_t PACKET_START_DELIMITER   = 0xB502; // on little-endian ESP32
constexpr size_t   PACKET_HEADER_SIZE       = 48;
constexpr uint16_t PACKET_END_DELIMITER     = 0x035B; // on little-endian ESP32



#pragma pack(push, 1)
struct PacketHeader
{
    uint16_t startDelimiter;      // 0x02B5
    uint16_t payloadSize;         // bytes after header
    uint32_t reserved0;

    uint64_t slaveUid;
    uint64_t messageCounter;
    uint64_t senderTimestampUs;

    uint32_t prevCycleTimeUs;

    uint8_t  chainedSlaveCount;
    uint8_t  espType;
    uint8_t  flags;
    uint8_t  headerVersion;
    uint8_t  networkId;
    uint8_t  chainDistance;
    uint8_t  ttl;
    uint8_t  reserved1;

    uint32_t crc32;
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 48, "PacketHeader must be 48 bytes");



struct Payload1
{
    uint8_t Test1;
    uint8_t Test2;
    uint8_t Test3;
    uint8_t Test4;
    uint8_t Test5;
    uint8_t Test6;
    uint8_t Test7;
    uint8_t Test8;
    uint8_t Test9[6];
};



class ITF_PacketProcessor 
{
    public:
        virtual ~ITF_PacketProcessor() = default;

        virtual bool ProcessPacket(const uint8_t* UdpPacketIn, size_t Length, uint8_t* ProcessedDataOut) = 0;
        virtual size_t PreparePacket(uint8_t* ProcessedDataIn, size_t Length, uint8_t* UdpPacketOut) = 0;
};

class Packet1 final : public ITF_PacketProcessor
{
    private:
        PacketHeader LastHeader{};
        Payload1 LastTx{};
        Payload1 LastRx{};

        uint8_t LastFullPacket[PACKET_HEADER_SIZE + sizeof(Payload1) + sizeof(PACKET_END_DELIMITER)]{};

    public:
        bool ProcessPacket(const uint8_t* UdpPacketIn, size_t Length, uint8_t* ProcessedDataOut) override;
        size_t PreparePacket(uint8_t* ProcessedDataIn, size_t Length, uint8_t* UdpPacketOut) override;
};




#endif