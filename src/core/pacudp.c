#include "pacudp.h"
#include <string.h>

int pacudp_address_size(const unsigned char *p, size_t n)
{
    size_t size;
    if (!p || n < 1) return 0;
    if (p[0] == 1) size = 7;
    else if (p[0] == 4) size = 19;
    else if (p[0] == 3 && n >= 2 && p[1]) size = (size_t)p[1] + 4;
    else return 0;
    return size <= n && size <= 259 ? (int)size : 0;
}

size_t pacudp_restore(unsigned char *packet, size_t length, size_t capacity,
                      const unsigned char *original, size_t original_length)
{
    int received;
    size_t payload, result;
    if (!packet || !original || capacity < 3 || length < 4 || length > capacity ||
        packet[0] || packet[1] || packet[2] ||
        pacudp_address_size(original, original_length) != (int)original_length)
        return 0;
    received = pacudp_address_size(packet + 3, length - 3);
    if (!received) return 0;
    payload = length - 3 - (size_t)received;
    if (original_length > capacity - 3 || payload > capacity - 3 - original_length)
        return 0;
    result = 3 + original_length + payload;
    memmove(packet + 3 + original_length, packet + 3 + received, payload);
    memcpy(packet + 3, original, original_length);
    return result;
}
