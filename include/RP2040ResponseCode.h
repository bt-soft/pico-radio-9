#pragma once

// Response codes for RP2040 FIFO communication
enum class RP2040ResponseCode : uint32_t { RESP_ACK = 100, RESP_SAMPLING_RATE = 101, RESP_DATA_BLOCK = 102, RESP_USE_FFT_ENABLED = 103 };
