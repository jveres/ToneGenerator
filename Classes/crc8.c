
#include <stdio.h>

static unsigned char _crc_table[256];
static int _crc_table_init = 0;
static const int _crc_poly = 0x107;
static unsigned char _crc_start = 0x0;

unsigned char crc8_table_lookup(const char *buf, int len)
{
    unsigned char CRC = _crc_start;
    if (_crc_table_init == 0)
    {
        for (int c=0; c<256; c++)
        {
            CRC = c;
            for(int i=0; i<8; i++)
            {
                if(CRC & 0x80 ) CRC = (CRC << 1) ^ _crc_poly;
                else CRC <<= 1;
            }
            CRC &= 0xff;
            _crc_table[c] = CRC;
        }
        _crc_table_init = 1;
        CRC = _crc_start;
    }
    for (int j=0; j<len; j++) CRC = _crc_table[CRC ^ buf[j]];
    return CRC;
}

unsigned char crc8(const char *buf, int len)
{
    unsigned char CRC = _crc_start;
    for (int j=0; j<len; j++)
    {
        CRC ^= buf[j];
        for(int i = 0; i<8; i++)
        {
            if(CRC & 0x80 ) CRC = (CRC << 1) ^ _crc_poly;
            else CRC <<= 1;
        }
        CRC &= 0xff;
    }
    return CRC;
}

/*printf("0x%02X\n", crc8_int(0x57df973b)); //-> 0xC1 if 8 chars, 0xDD if 4 bytes*/

unsigned char crc8_int(unsigned int data)
{
    static char h[9];
    sprintf(h, "%08X", data);
    unsigned char crc = crc8_table_lookup(h, 8);
    return crc;
}
