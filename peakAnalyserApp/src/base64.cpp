#include <string>
/*
 * Base64 decoder
 *    0. No error checking
 *    1. With decoded lenght known
 *    2. Skips CR LF
 */

static const unsigned char base64_lut[256] = {
0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,
0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,
0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62, 63, 62, 62, 63,
52,53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,
0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
15,16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0, 63,
0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
41,42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  0,  0,  0,  0,  0
};

void base64_decode(const std::string& encoded_string, unsigned char *decoded_string, size_t decoded_length)
{
    unsigned char* p = (unsigned char*)encoded_string.data();
    size_t encoded_length = encoded_string.length();

    size_t i = 0;
    size_t j = 0;
    while (i < encoded_length)
    {
        // skip CR LF
        if (p[i] == '\r' || p[i] == '\n') {
            i += 1;
            continue;
        }
        int n = base64_lut[p[i]] << 18 | base64_lut[p[i+1]] << 12 | base64_lut[p[i+2]] << 6 | base64_lut[p[i+3]];
        decoded_string[j++] = n >> 16;
        decoded_string[j++] = n >> 8 & 0xFF;
        decoded_string[j++] = n & 0xFF;
        i += 4;
    }
}
