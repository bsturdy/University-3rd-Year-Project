#include "throughput_tests.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

static const char* TAG = "THROUGHPUT";
static TaskHandle_t ThroughputTaskHandle = nullptr;

static uint32_t CalculateHeaderCrc32(const uint8_t* Header)
{
    if (Header == nullptr) return 0;

    uint32_t Crc = 0xFFFFFFFFu;
    for (size_t Index = 0; Index < PACKET_HEADER_SIZE; ++Index)
    {
        const uint8_t Byte = (Index >= 44 && Index <= 47) ? 0 : Header[Index];
        Crc ^= static_cast<uint32_t>(Byte);

        for (int Bit = 0; Bit < 8; ++Bit)
        {
            Crc = (Crc & 1u) ? ((Crc >> 1) ^ 0xEDB88320u) : (Crc >> 1);
        }
    }

    return Crc ^ 0xFFFFFFFFu;
}

static void WriteHeaderCrc32Le(uint8_t* Header)
{
    const uint32_t Crc = CalculateHeaderCrc32(Header);
    Header[44] = static_cast<uint8_t>(Crc & 0xFFu);
    Header[45] = static_cast<uint8_t>((Crc >> 8) & 0xFFu);
    Header[46] = static_cast<uint8_t>((Crc >> 16) & 0xFFu);
    Header[47] = static_cast<uint8_t>((Crc >> 24) & 0xFFu);
}

static size_t BuildThroughputPacket(uint8_t* Buffer,
                                    size_t BufferSize,
                                    size_t PayloadSize,
                                    uint8_t ForwardingMode,
                                    uint64_t DestinationUid,
                                    uint32_t Sequence)
{
    if (Buffer == nullptr) return 0;
    PayloadSize = std::min(PayloadSize, BufferSize > PACKET_HEADER_SIZE + 2 ? BufferSize - PACKET_HEADER_SIZE - 2 : 0);
    if (PayloadSize == 0) return 0;

    PacketHeader Header{};
    Header.startDelimiter = PACKET_START_DELIMITER;
    Header.payloadSize = htons(static_cast<uint16_t>(PayloadSize));
    Header.slaveUid = MY_UID;
    Header.destinationUid = DestinationUid;
    Header.senderTimestampUs = static_cast<uint64_t>(esp_timer_get_time());
    Header.prevCycleTimeUs = Sequence;
    Header.chainedSlaveCount = 0;
    Header.PacketType = THROUGHPUT_PACKET_TYPE;
    Header.flags = 0;
    Header.headerVersion = 1;
    Header.networkId = 0;
    Header.chainDistance = 0;
    Header.ttl = 10;
    Header.ForwardingMode = ForwardingMode;
    Header.crc32 = 0;

    memcpy(Buffer, &Header, sizeof(Header));

    for (size_t i = 0; i < PayloadSize; ++i)
    {
        Buffer[PACKET_HEADER_SIZE + i] = static_cast<uint8_t>((Sequence + i) & 0xFFu);
    }

    memcpy(Buffer + PACKET_HEADER_SIZE + PayloadSize, &PACKET_END_DELIMITER, sizeof(PACKET_END_DELIMITER));
    WriteHeaderCrc32Le(Buffer);

    return PACKET_HEADER_SIZE + PayloadSize + sizeof(PACKET_END_DELIMITER);
}

static bool MakeDestination(const char* Ip, uint16_t Port, sockaddr_in& Destination)
{
    if (Ip == nullptr || Ip[0] == '\0') return false;
    memset(&Destination, 0, sizeof(Destination));
    Destination.sin_family = AF_INET;
    Destination.sin_port = htons(Port);
    return inet_pton(AF_INET, Ip, &Destination.sin_addr) == 1;
}

static void PrintRate(const char* Label,
                      uint64_t Packets,
                      uint64_t Bytes,
                      uint32_t DurationMs)
{
    const double Seconds = static_cast<double>(DurationMs) / 1000.0;
    const double MbitPerSecond = Seconds > 0.0 ? (static_cast<double>(Bytes) * 8.0) / (Seconds * 1000000.0) : 0.0;
    const double PacketsPerSecond = Seconds > 0.0 ? static_cast<double>(Packets) / Seconds : 0.0;

    ESP_LOGW(TAG, "%s | packets=%llu bytes=%llu rate=%.3f Mbit/s pps=%.1f",
             Label,
             static_cast<unsigned long long>(Packets),
             static_cast<unsigned long long>(Bytes),
             MbitPerSecond,
             PacketsPerSecond);
}

static void RunSender(AccessPointStation* Wifi,
                      const char* TargetIp,
                      uint8_t ForwardingMode,
                      uint64_t DestinationUid)
{
    sockaddr_in Destination{};
    if (!MakeDestination(TargetIp, THROUGHPUT_TARGET_PORT, Destination))
    {
        ESP_LOGE(TAG, "Invalid throughput target IP: %s", TargetIp ? TargetIp : "(null)");
        return;
    }

    uint8_t Packet[UDP_PACKET_SIZE]{};
    const size_t PayloadSize = std::min(THROUGHPUT_PAYLOAD_BYTES, UDP_PACKET_SIZE - PACKET_HEADER_SIZE - 2);
    const int64_t StartUs = esp_timer_get_time();
    const bool RunContinuously = (THROUGHPUT_TEST_DURATION_MS == 0);
    const int64_t EndUs = RunContinuously
        ? INT64_MAX
        : StartUs + (static_cast<int64_t>(THROUGHPUT_TEST_DURATION_MS) * 1000);
    int64_t LastLogUs = StartUs;
    uint64_t WindowPackets = 0;
    uint64_t WindowBytes = 0;
    uint64_t SentPackets = 0;
    uint64_t SentBytes = 0;
    uint32_t Sequence = 0;

    ESP_LOGW(TAG, "TX start | target=%s port=%u payload=%u mode=%u duration_ms=%u",
             TargetIp,
             static_cast<unsigned>(THROUGHPUT_TARGET_PORT),
             static_cast<unsigned>(PayloadSize),
             static_cast<unsigned>(ForwardingMode),
             static_cast<unsigned>(THROUGHPUT_TEST_DURATION_MS));

    while (esp_timer_get_time() < EndUs)
    {
        if (ForwardingMode == 2 && !Wifi->IsConnectedToHost())
        {
            ESP_LOGW(TAG, "Parent link lost during forwarded throughput test; pausing sender");
            return;
        }

        const size_t PacketLength = BuildThroughputPacket(Packet,
                                                          sizeof(Packet),
                                                          PayloadSize,
                                                          ForwardingMode,
                                                          DestinationUid,
                                                          Sequence++);
        if (PacketLength == 0) continue;

        const size_t Sent = Wifi->SendData(Packet, static_cast<int>(PacketLength), Destination);
        if (Sent > 0)
        {
            SentPackets++;
            SentBytes += Sent;
            WindowPackets++;
            WindowBytes += Sent;
        }
        else
        {
            if constexpr (THROUGHPUT_TX_DELAY_TICKS > 0)
            {
                vTaskDelay(static_cast<TickType_t>(THROUGHPUT_TX_DELAY_TICKS));
            }
            else
            {
                taskYIELD();
            }
        }

        const int64_t NowUs = esp_timer_get_time();
        if (NowUs - LastLogUs >= 1000000)
        {
            PrintRate("TX live", WindowPackets, WindowBytes, static_cast<uint32_t>((NowUs - LastLogUs) / 1000));
            WindowPackets = 0;
            WindowBytes = 0;
            LastLogUs = NowUs;
        }

        if constexpr (THROUGHPUT_TX_BURST_PACKETS > 0)
        {
            if ((Sequence % THROUGHPUT_TX_BURST_PACKETS) == 0)
            {
                if constexpr (THROUGHPUT_TX_DELAY_TICKS > 0)
                {
                    vTaskDelay(static_cast<TickType_t>(THROUGHPUT_TX_DELAY_TICKS));
                }
                else
                {
                    taskYIELD();
                }
            }
        }
    }

    if (!RunContinuously)
    {
        PrintRate("TX complete", SentPackets, SentBytes, THROUGHPUT_TEST_DURATION_MS);
    }
}

static void RunSenderToChild(AccessPointStation* Wifi)
{
    const uint32_t WaitMs = 30000;
    const int64_t EndUs = esp_timer_get_time() + (static_cast<int64_t>(WaitMs) * 1000);

    while (esp_timer_get_time() < EndUs)
    {
        const char* ChildIp = Wifi->GetFirstChildIpAddress();
        if (ChildIp != nullptr && ChildIp[0] != '\0')
        {
            ESP_LOGW(TAG, "Using first child target IP: %s", ChildIp);
            RunSender(Wifi, ChildIp, 0, 0);
            return;
        }

        ESP_LOGW(TAG, "Waiting for child IP before node-to-node throughput test...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGE(TAG, "No child IP available for node-to-node throughput test");
}

static void RunSenderToParent(AccessPointStation* Wifi)
{
    while (true)
    {
        const char* ParentIp = Wifi->GetParentIpAddress();
        if (Wifi->IsConnectedToHost() && ParentIp != nullptr && ParentIp[0] != '\0')
        {
            ESP_LOGW(TAG, "Using parent target IP: %s", ParentIp);
            RunSender(Wifi, ParentIp, 2, MASTER_UID);
        }
        else
        {
            ESP_LOGW(TAG, "Waiting for parent IP before forwarded throughput test...");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void RunCounter(AccessPointStation* Wifi, const char* Label)
{
    Wifi->ResetThroughputStats();
    const uint32_t WindowMs = 1000;

    ESP_LOGW(TAG, "%s start | counting type-%u packets", Label, static_cast<unsigned>(THROUGHPUT_PACKET_TYPE));

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(WindowMs));
        const ThroughputStats Stats = Wifi->GetThroughputStats();
        PrintRate("RX", Stats.ReceivedPackets, Stats.ReceivedBytes, WindowMs);
        PrintRate("FWD", Stats.ForwardedPackets, Stats.ForwardedBytes, WindowMs);
        Wifi->ResetThroughputStats();
    }
}

static void RunThroughputTestMode(AccessPointStation* Wifi)
{
    if (Wifi == nullptr)
    {
        ESP_LOGE(TAG, "No WiFi object");
        return;
    }

    ESP_LOGW(TAG, "Throughput mode active | node_uid=%llu mode=%u",
             static_cast<unsigned long long>(MY_UID),
             static_cast<unsigned>(THROUGHPUT_TEST_MODE));

    switch (THROUGHPUT_TEST_MODE)
    {
        case THROUGHPUT_MODE_TX_TO_MASTER:
            RunSender(Wifi, THROUGHPUT_TARGET_IP, 0, MASTER_UID);
            break;

        case THROUGHPUT_MODE_TX_TO_NODE:
            if (strcmp(THROUGHPUT_TARGET_IP, "AUTO_CHILD") == 0)
            {
                RunSenderToChild(Wifi);
            }
            else
            {
                RunSender(Wifi, THROUGHPUT_TARGET_IP, 0, 0);
            }
            break;

        case THROUGHPUT_MODE_RX_NODE:
            RunCounter(Wifi, "RX node");
            break;

        case THROUGHPUT_MODE_TX_FORWARDED_TO_MASTER:
            RunSenderToParent(Wifi);
            break;

        case THROUGHPUT_MODE_RELAY_MONITOR:
            RunCounter(Wifi, "Relay monitor");
            break;

        default:
            ESP_LOGE(TAG, "Unknown throughput mode %u", static_cast<unsigned>(THROUGHPUT_TEST_MODE));
            break;
    }

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void ThroughputTask(void* Parameter)
{
    AccessPointStation* Wifi = static_cast<AccessPointStation*>(Parameter);
    RunThroughputTestMode(Wifi);
    ThroughputTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

bool StartThroughputTestMode(AccessPointStation* Wifi)
{
    if (Wifi == nullptr) return false;
    if (ThroughputTaskHandle != nullptr) return true;

    const BaseType_t Created = xTaskCreatePinnedToCore(
        ThroughputTask,
        "ThroughputTest",
        THROUGHPUT_TASK_STACK_BYTES,
        Wifi,
        4,
        &ThroughputTaskHandle,
        THROUGHPUT_TASK_CORE);

    if (Created != pdPASS)
    {
        ThroughputTaskHandle = nullptr;
        ESP_LOGE(TAG, "Failed to start throughput test task");
        return false;
    }

    ESP_LOGW(TAG, "Throughput test task started on core %u", static_cast<unsigned>(THROUGHPUT_TASK_CORE));
    return true;
}
