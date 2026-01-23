#include "packet_processors.h"




uint32_t CalculateCrc32(const uint8_t* data)
{
    if (!data) return 0;

    // Build table locally (once)
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        init = true;
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
    }

    // CRC over first 44 bytes
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < 44; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

static bool ValidateCrc32(const uint8_t* data)
{
    if (!data) return false;

    // Read stored CRC32 from data[44..47] (little-endian)
    const uint32_t expected =
        (uint32_t)data[44] |
        ((uint32_t)data[45] << 8) |
        ((uint32_t)data[46] << 16) |
        ((uint32_t)data[47] << 24);

    // Recompute CRC32 over data[0..43]
    // Build table locally (once)
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        init = true;
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
    }

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < 44; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    crc ^= 0xFFFFFFFFu;

    return crc == expected;
}

bool ParseAndValidateHeader(const uint8_t* data, size_t Length, PacketHeader& outHeader)
{
    if (!data) return false;

    // Must contain header + payload 
    if (Length < PACKET_HEADER_SIZE) return false;

    // Check start delimiter from raw bytes
    const uint16_t delimiter =
        (uint16_t)data[0] |
        ((uint16_t)data[1] << 8);

    if (delimiter != PACKET_START_DELIMITER) return false;

    //if (!ValidateCrc32(data)) return false;

    // Copy header out
    memcpy(&outHeader, data, PACKET_HEADER_SIZE);

    return true;
}



//==============================================================================//
//                                                                              //
//                                 Packet 1                                     //
//                                                                              //
//==============================================================================// 

bool Packet1::ProcessPacket(const uint8_t* UdpPacketIn, size_t Length, uint8_t* ProcessedDataOut)
{
    if (!UdpPacketIn || !ProcessedDataOut) return false;
    if (Length > PACKET_HEADER_SIZE + sizeof(Payload1) + sizeof(PACKET_END_DELIMITER)) return false;
    if (!ParseAndValidateHeader(UdpPacketIn, Length, LastHeader)) return false;
    if (UdpPacketIn[PACKET_HEADER_SIZE + sizeof(Payload1) + 0] != 0x5B) return false;
    if (UdpPacketIn[PACKET_HEADER_SIZE + sizeof(Payload1) + 1] != 0x03) return false;

    memcpy(&LastRx, UdpPacketIn + PACKET_HEADER_SIZE, sizeof(Payload1));
    memcpy(ProcessedDataOut, UdpPacketIn + PACKET_HEADER_SIZE, sizeof(Payload1));

    return true;
}

size_t Packet1::PreparePacket(uint8_t* ProcessedDataIn, size_t Length, uint8_t* UdpPacketOut)
{
    if (!ProcessedDataIn || !UdpPacketOut) return 0;
    if (Length != sizeof(Payload1)) return 0;

    // Fill header
    LastHeader.startDelimiter     = PACKET_START_DELIMITER;
    LastHeader.payloadSize        = sizeof(Payload1);
    LastHeader.reserved0          = 0;
    LastHeader.slaveUid           = CONFIG_ESP_NODE_UID;
    LastHeader.messageCounter++;
    LastHeader.prevCycleTimeUs    = UtilitiesClass::GetUptimeUs() - LastHeader.senderTimestampUs;
    LastHeader.senderTimestampUs  = UtilitiesClass::GetUptimeUs();
    LastHeader.chainedSlaveCount  = 0;
    LastHeader.espType            = 1;
    LastHeader.flags              = 0;
    LastHeader.headerVersion      = 1;
    LastHeader.networkId          = 1;
    LastHeader.chainDistance      = 0;
    LastHeader.ttl                = 10;
    LastHeader.reserved1          = 0;
    LastHeader.crc32              = 0; // to be computed later

    // Compute CRC32
    LastHeader.crc32 = CalculateCrc32(reinterpret_cast<const uint8_t*>(&LastHeader));

    // Save payload data
    memcpy(&LastTx, ProcessedDataIn, sizeof(Payload1));
    memcpy(LastFullPacket, &LastHeader, PACKET_HEADER_SIZE);
    memcpy(LastFullPacket + PACKET_HEADER_SIZE, &LastTx, sizeof(Payload1));
    LastFullPacket[PACKET_HEADER_SIZE + sizeof(Payload1) + 0] = 0x5B;
    LastFullPacket[PACKET_HEADER_SIZE + sizeof(Payload1) + 1] = 0x03;

    
    memcpy(UdpPacketOut, LastFullPacket, PACKET_HEADER_SIZE + sizeof(Payload1) + sizeof(PACKET_END_DELIMITER));
    return PACKET_HEADER_SIZE + sizeof(Payload1) + sizeof(PACKET_END_DELIMITER);
}