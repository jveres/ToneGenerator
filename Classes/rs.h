
/* General purpose RS codec, 8-bit symbols */
extern "C"
{
    void encode_rs_char(void *rs,unsigned char *data,unsigned char *parity);
    int decode_rs_char(void *rs,unsigned char *data,int *eras_pos,int no_eras);
    void *init_rs_char(unsigned int symsize,unsigned int gfpoly,unsigned int fcr,unsigned int prim,unsigned int nroots);
    void free_rs_char(void *rs);
    
    unsigned char crc8_int(unsigned int data);
}