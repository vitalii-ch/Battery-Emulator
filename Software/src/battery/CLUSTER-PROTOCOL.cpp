#include "CLUSTER-PROTOCOL.h"

namespace cluster_protocol {
void encode_frame0(uint8_t[8], uint16_t, int16_t, uint16_t, uint8_t, uint8_t) {}
void encode_frame1(uint8_t[8], uint16_t, uint16_t, uint16_t, uint16_t) {}
void encode_frame2(uint8_t[8], int16_t, int16_t, uint16_t, uint8_t, uint8_t) {}
void encode_frame3(uint8_t[8], uint32_t, uint32_t) {}
void encode_frame4(uint8_t[8], uint16_t, uint16_t, uint16_t, uint8_t, uint8_t) {}
void decode_frame0(const uint8_t[8], PackSnapshot&) {}
void decode_frame1(const uint8_t[8], PackSnapshot&) {}
void decode_frame2(const uint8_t[8], PackSnapshot&) {}
void decode_frame3(const uint8_t[8], PackSnapshot&) {}
void decode_frame4(const uint8_t[8], PackSnapshot&) {}
}
