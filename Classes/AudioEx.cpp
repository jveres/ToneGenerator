//
// VJ / 2013
//

#include "AudioEx.h"
#include <stdint.h>

Float32 Ino(Float32 x)
{
    Float32 d = 0.0, ds = 1.0, s = 1.0;
    do
    {
        d += 2.0;
        ds *= x * x / (d * d);
        s += ds;
    }
    while (ds > s * 1e-6);
    return s;
}

AudioEx::AudioEx(float sampleRate)
{
    // initialize globals
    sample_rate = sampleRate;
    result = 0;
    rx_level = 0.0;
    
    LOG({
        printf("SIGNAL LENGTH: %.02fms (%.0fms)\n", (float)SIGNAL_GENERATOR_LEN * 1000.0f / sample_rate, (float)SIGNAL_GENERATOR_LEN * 1000.0f / sample_rate * FULL_SIGNAL_LEN);
        printf("DFT RESOLUTION: %.02fHz\n", sample_rate/SAMPLING_LENGTH);
    });
    
    // initialize signal generator
    signal_generator_reset();
    
    // freq bins
    for (int i=0; i<FREQ_COUNT; i++)
    {
        gft_coeff_cosine[i] = 2.0 * cosf(2.0 * M_PI * signal_freqs[i] / sample_rate); // real part
        gft_coeff_sine[i] = sinf(2.0 * M_PI * signal_freqs[i] / sample_rate); // imag part
    }
    
    // initialize RS(15, 11) codec
    rs_codec = init_rs_char(RS_SYMSIZE, RS_POLY, 1, 1, RS_PARITY);
    
    // initialize windowing (Kaiser-Bessel) function
    const Float32 alpha = 2.5;
    Float32 den = Ino(M_PI * alpha);
    int n1 = SAMPLING_LENGTH/2;
    int n2 = n1*n1;
    wnd_coeffs[0] = 0.0;
    wnd_coeffs[n1] = 2.0;
    for (int i=1; i<n1; i++)
    {
        Float32 t = Ino(M_PI*alpha*sqrtf(1.0-(Float32)i*i/n2));
        wnd_coeffs[n1+i] = 2.0 * t / den;
        wnd_coeffs[n1-i] = wnd_coeffs[n1+i];
    }
}

int payload_diff(int payload1[], int payload2[])
{
    int ret = 0;
    for (int i=0; i<PAYLOAD_LEN; i++) if (payload1[i] != payload2[i]) ret++;
    return ret;
}

typedef enum {
    DETECT = 0,
    DECODE = 1,
} DETECTOR_STATUS;

#define CSTEP(pos, len) ((pos) % len)

void AudioEx::detect(Float32 mags[FREQ_COUNT], int phases[FREQ_COUNT])
{
    static int fft_frame_i = 0;
    static int fft_test_i = 0;
    
    static Float32 fft_mags[FREQ_COUNT][SIGNAL_FRAMES];
    static Float32 fft_mag_sums[FREQ_COUNT][SIGNAL_FRAMES];
    static Float32 fft_sum_diffs[FREQ_COUNT][SIGNAL_TEST_FRAME_LEN];
    static int fft_phases[FREQ_COUNT][SIGNAL_TEST_FRAME_LEN];
    static Float32 fft_powers[FREQ_COUNT][SIGNAL_TEST_FRAME_LEN];
    static int fft_max_powers[2][SIGNAL_TEST_FRAME_LEN];
    
    static DETECTOR_STATUS status = DETECT;
    
    int p_fft_frame_i = (fft_frame_i > 0 ? fft_frame_i - 1 : SIGNAL_FRAMES - 1);
    
    Float32 max_v = INT32_MIN;
    int max_i = 0;
#ifdef METERING_ENABLED
    Float32 sum_v = 0.0;
#endif
    
    for (int i=0; i<FREQ_COUNT; i++)
    {
        // squared mag
        Float32 fft_mag = mags[i];
        
        // sums of mags
        fft_mag_sums[i][fft_frame_i] = fft_mag_sums[i][p_fft_frame_i] - fft_mags[i][fft_frame_i] + fft_mag;
        fft_mags[i][fft_frame_i] = fft_mag;
        
        // sum diffs
        Float32 fft_sum_diff = fft_mag_sums[i][fft_frame_i] - fft_mag_sums[i][p_fft_frame_i];
        fft_sum_diffs[i][fft_test_i] = fft_sum_diff;
        
        // power = mag + difference of sums
        Float32 fft_power = fft_mag_sums[i][fft_frame_i] + fft_sum_diff;
        
#ifdef METERING_ENABLED
        // diag, rx_level
        sum_v += abs(fft_sum_diff);
#endif
        
        // phase
        fft_phases[i][fft_test_i] = phases[i];
        
        // powers
        fft_powers[i][fft_test_i] = fft_power;
        
        // detect maxima
        if (fft_power > max_v)
        {
            max_v = fft_power;
            max_i = i;
        }
    }
    
#ifdef METERING_ENABLED
    // diag, rx_level
#define MAX_V 25.0
    static Float32 p_rx_level = 0.0;
    rx_level = sum_v > MAX_V ? 1.0 : sum_v/MAX_V;
    // decimate level
    if (rx_level == 1.0 && p_rx_level == 1.0) rx_level -= 0.2;
    p_rx_level = rx_level;
#endif
    
    // save 1st maxima of power sums
    fft_max_powers[0][fft_test_i] = max_i;
    
    // calculate 2nd maxima of power sums
    max_v = INT32_MIN;
    max_i = 0;
    for (int i=0; i<FREQ_COUNT; i++)
    {
        if (i == fft_max_powers[0][fft_test_i]) continue;
        
        if (fft_powers[i][fft_test_i] > max_v)
        {
            max_v = fft_powers[i][fft_test_i];
            max_i = i;
        }
    }
    fft_max_powers[1][fft_test_i] = max_i;
    
    // next frame index
    fft_frame_i = CSTEP(fft_frame_i+1, SIGNAL_FRAMES);
    
    // next test data index
    fft_test_i = CSTEP(fft_test_i+1, SIGNAL_TEST_FRAME_LEN);
    
    // now indexes point to the oldest test data in FIFO buffers
    
    // should we skip sample frames?
    static int f_skip = 0;
    if (f_skip > 0)
    {
        f_skip--;
        return;
    }
    
    // detection state
    if (status == DETECT)
    {
        // check signal start (ST0)
        int n_fft_test_i = CSTEP(fft_test_i+SIGNAL_FRAMES, SIGNAL_TEST_FRAME_LEN);
        
        if (fft_sum_diffs[CW_ST0[0]][fft_test_i] > MIN_PEAK && fft_sum_diffs[CW_ST0[1]][n_fft_test_i] > MIN_PEAK)
        {
            static int st_test[2][2];
            static int t1, t2;
            
            st_test[0][0] = fft_max_powers[0][fft_test_i];
            st_test[1][0] = fft_max_powers[1][fft_test_i];
            st_test[0][1] = fft_max_powers[0][n_fft_test_i];
            st_test[1][1] = fft_max_powers[1][n_fft_test_i];
            
            cw_lookup_test(st_test, CW_ST_TEST_LOOKUP, &t1, &t2, NULL, NULL);
            
            if (t1 == ST0 || t2 == ST0)
            {
                LOG({
                    printf("SIGNAL DETECTED\n");
                });
                
                // decode incoming signal
                status = DECODE;
            }
        }
    }

    // decoding state
    if (status == DECODE)
    {
        // static allocation
        static Float32 scoring[4][FULL_SIGNAL_LEN]; // 1st maxi, 2nd maxi, 1st energy, 2nd energy
        static int payload[PAYLOAD_LEN];
        static int p_payload[PAYLOAD_LEN];
        
        // reset previous payload data
        memset(p_payload, 0, sizeof p_payload);
        
        for (int i=0; i<SIGNAL_TEST_PADDING; i++)
        {
            // index
            int fft_i = CSTEP(fft_test_i+i, SIGNAL_TEST_FRAME_LEN);
            
            // calculate scoring
            if (!generate_scoring(fft_i, fft_powers, fft_max_powers, fft_phases, scoring)) break;
            
            // calculate payload
            if (scoring_test(scoring, payload))
            {
                // double check payload
                if (!result && payload_diff(p_payload, payload) <= MAX_PAYLOAD_DIFF)
                {
                    // test payload
                    result = payload_test(payload);
                    if (result > 0) break; // if success, return with result
                }
                memcpy(p_payload, payload, sizeof payload);
            }
        }
        
        // reset detector
        status = DETECT;
            
        // on successful detection skip to next possible signal
        if (result > 0) f_skip = SIGNAL_TEST_FRAME_LEN;
    }    
}

unsigned int AudioEx::payload_test(int payload[PAYLOAD_LEN])
{
    int i = 0, pos = 0;
    
    // RS code
    int fec[RS_PARITY];
    while (i < RS_PARITY) fec[i++] = payload[pos++];
    
    // CRC
    int crc[CRC_LEN];
    i = 0;
    while (i < CRC_LEN) crc[i++] = payload[pos++];
    
    // DATA
    int data[DATA_LEN];
    i = 0;
    while (i < DATA_LEN) data[i++] = payload[pos++];
    
    // RS known erasures
    int n_erasures = 0;
    int erasures[RS_PARITY];
    memset(erasures, 0, RS_PARITY * sizeof(int));
    
    // fec test code = DATA+CRC+RS (zero padding on 10th position)
    unsigned char test[RS_N];
    memset(test, 0x0, RS_N);
    
#define ADD_ERASURE(x) { erasures[n_erasures++] = x; if (n_erasures == RS_PARITY) return 0; }
    
    for (i=0; i<DATA_LEN; i++)
    {
        if (data[i] == -1) ADD_ERASURE(i)
        else test[i] = (unsigned char)data[i];
    }
    for (i=0; i<CRC_LEN; i++)
    {
        if (crc[i] == -1) ADD_ERASURE(DATA_LEN+i)
        else test[DATA_LEN+i] = (unsigned char)crc[i];
    }
    for (i=0; i<RS_PARITY; i++)
    {
        if (fec[i] == -1) ADD_ERASURE(RS_N-RS_PARITY+i)
        else test[RS_N-RS_PARITY+i] = (unsigned char)fec[i];
    }
    
    // test RS code with known erasures
    int ret = decode_rs_char(rs_codec, test, erasures, n_erasures);
    
    // no error from rs, check crc
    if (ret > -1)
    {
        // calculate crc
        unsigned char crc_msb = test[DATA_LEN];
        unsigned char crc_lsb = test[DATA_LEN+1];
        unsigned char crc = (crc_msb << 4) + crc_lsb;
        
        unsigned char temp[DATA_LEN];
        for (int i=0; i<DATA_LEN; i++) temp[i] = test[i] < 0x0a ? 0x30 + test[i]: 0x37 + test[i];
        
        unsigned int temp_value = strtoul((const char*)temp, NULL, 0x10);
        unsigned char temp_crc = crc8_int(temp_value);
        
        // crc check
        if (temp_value && temp_crc == crc)
        {
            LOG({
                printf("RS(15,11): %i\n", ret);
                printf("VALUE: 0x%08X, CRC: 0x%02X\n", temp_value, crc);
                printf("CRC OK\n");
            });
            
            // return decoded value
            ret = temp_value;
        } else {
            LOG({
                printf("CRC FAILED\n");
            });
            
            // return error
            ret = 0;
        }
    } else {
        LOG({
            printf("RS(15,11) ERROR: %i\n", ret);
        });
        
        // return error
        ret = 0;
    }
    
    return ret;
}

void AudioEx::cw_lookup_test(const int test[2][2], const int lookup[FREQ_COUNT][FREQ_COUNT], int* t1, int* t2, int* t3, int* t4)
{
    // 1st - 1st
    if (t1 != NULL)
    {
        int s1 = test[0][0];
        int s2 = test[0][1];
        *t1 = (s1 >= 0 && s1 < FREQ_COUNT && s2 >= 0 && s2 < FREQ_COUNT) ? lookup[s1][s2] : -1;
    }
    
    // 1st - 2nd
    if (t2 != NULL)
    {
        int s1 = test[0][0];
        int s2 = test[1][1];
        *t2 = (s1 >= 0 && s1 < FREQ_COUNT && s2 >= 0 && s2 < FREQ_COUNT) ? lookup[s1][s2] : -1;
    }
    
    // 2nd - 1st
    if (t3 != NULL)
    {
        int s1 = test[1][0];
        int s2 = test[0][1];
        *t3 = (s1 >= 0 && s1 < FREQ_COUNT && s2 >= 0 && s2 < FREQ_COUNT) ? lookup[s1][s2] : -1;
    }
    
    // 2nd - 2nd
    if (t4 != NULL)
    {
        int s1 = test[1][0];
        int s2 = test[1][1];
        *t4 = (s1 >= 0 && s1 < FREQ_COUNT && s2 >= 0 && s2 < FREQ_COUNT) ? lookup[s1][s2] : -1;
    }
}

#define POPULATE_MAXIS(_x_) maxis[0][0] = (int)scoring[0][_x_]; maxis[0][1] = (int)scoring[0][_x_+1]; maxis[1][0] = (int)scoring[1][_x_]; maxis[1][1] = (int)scoring[1][_x_+1];
#define POPULATE_ENERGIES(_x_) energies[0][0] = scoring[2][_x_]; energies[0][1] = scoring[2][_x_+1]; energies[1][0] = scoring[3][_x_]; energies[1][1] = scoring[3][_x_+1];

bool AudioEx::scoring_test(const Float32 scoring[4][FULL_SIGNAL_LEN], int payload[PAYLOAD_LEN])
{
    static int maxis[2][2];
    static Float32 energies[2][2];
    
    int payload_i = 0;
    int scoring_i = 2; // skip start freqs
    int error_count = 0;
    int t1, t2, t3, t4;
    Float32 e1, e2, e3;
    
    // check & fill payload data
    while (payload_i < PAYLOAD_LEN && error_count <= RS_PARITY)
    {
        // reset payload data
        payload[payload_i] = -1;
        
        // populate payload data
        POPULATE_MAXIS(scoring_i)
        
        // vote payload freqs
        cw_lookup_test(maxis, CW_DATA_TEST_LOOKUP, &t1, &t2, &t3, &t4);
        
        // populate accumulated energies accross frames
        POPULATE_ENERGIES(scoring_i);
        e1 = energies[0][0]+energies[0][1];
        e2 = energies[0][0]+energies[1][1];
        e3 = energies[1][0]+energies[0][1];
        
        // scoring payload
        if (t1 > -1) payload[payload_i] = t1;
        else if (t2 > -1 && t3 == -1) payload[payload_i] = t2;
        else if (t3 > -1 && t2 == -1) payload[payload_i] = t3;
        else if (t2 > -1 && t3 > -1)
        {
            if (e2 >= e3) payload[payload_i] = t2;
            else payload[payload_i] = t3;
        }
        else payload[payload_i] = t4;
        
        if (payload[payload_i] == -1) error_count++;
        
        // next scoring index
        scoring_i+=2;
        
        // next payload index
        payload_i++;
    }
    
    LOG({
        printf("     ");
        for (int i=0; i<PAYLOAD_LEN; i++) printf("%c   ", payload[i] == -1 ? '?' : payload[i] < 0x0a ? 0x30 + payload[i]: 0x37 + payload[i]);
        printf("\n     (ERRORS: %i)\n\n", error_count);
    });
    
    return error_count <= RS_PARITY;
}

bool AudioEx::generate_scoring(int fft_i, const Float32 fft_powers[FREQ_COUNT][SIGNAL_TEST_FRAME_LEN], const int fft_max_powers[2][SIGNAL_TEST_FRAME_LEN], const int fft_phases[FREQ_COUNT][SIGNAL_TEST_FRAME_LEN], Float32 scoring[2][FULL_SIGNAL_LEN])
{
    int scoring_i = 0, phase_change_count = 0;
#ifdef DEBUG
    int _i = fft_i;
#endif
    while (scoring_i < FULL_SIGNAL_LEN)
    {
        // populate scoring data
        
        // check phase change
        if (fft_phases[(int)fft_max_powers[0][fft_i]][fft_i] > 0) phase_change_count++;
        if (phase_change_count > MAX_PHASE_CHANGE) return false;
        
        // maximas
        scoring[0][scoring_i] = fft_max_powers[0][fft_i];
        scoring[1][scoring_i] = fft_max_powers[1][fft_i];
        
        // energies
        scoring[2][scoring_i] = fft_powers[(int)scoring[0][scoring_i]][fft_i];
        scoring[3][scoring_i] = fft_powers[(int)scoring[1][scoring_i]][fft_i];
        
        // step frame pointer
        fft_i = CSTEP(fft_i+SIGNAL_FRAMES, SIGNAL_TEST_FRAME_LEN);
        
        // next payload data
        scoring_i++;
    }
    
    LOG({
        printf("\n");
        
        for (int i=0; i<FULL_SIGNAL_LEN; i++) printf("%c ", (int)scoring[0][i] == -1 ? '?' : (int)scoring[0][i] < 0x0a ? 0x30 + (int)scoring[0][i]: 0x37 + (int)scoring[0][i]);
        printf("\n");
        for (int i=0; i<FULL_SIGNAL_LEN; i++) printf("%c ", (int)scoring[1][i] == -1 ? '?' : (int)scoring[1][i] < 0x0a ? 0x30 + (int)scoring[1][i]: 0x37 + (int)scoring[1][i]);
        printf("\n");
        
        printf("PHASE CHANGE:\n");
        for (int i=0; i<FULL_SIGNAL_LEN; i++) printf("%i ", (int)scoring[0][i] == -1 ? '?' : fft_phases[(int)scoring[0][i]][CSTEP(_i+(i*SIGNAL_FRAMES), SIGNAL_TEST_FRAME_LEN)]);
        printf("\n");
    });
    
    // overlapping tones correction
    for (int i=0; i<FULL_SIGNAL_LEN-1; i+=2)
    {
        if (scoring[0][i] == scoring[0][i+1])
        {
            // exchange freq data
            scoring[0][i+1] = scoring[1][i+1];
            scoring[1][i+1] = scoring[0][i];
            // exchange energy data
            Float32 e = scoring[2][i+1];
            scoring[2][i+1] = scoring[3][i+1];
            scoring[3][i+1] = e;
        }
    }
    
    LOG({
        printf("OVERLAP CORRECTED SIGNAL:\n");
        for (int i=0; i<FULL_SIGNAL_LEN; i++) printf("%c ", (int)scoring[0][i] == -1 ? '?' : (int)scoring[0][i] < 0x0a ? 0x30 + (int)scoring[0][i]: 0x37 + (int)scoring[0][i]);
        printf("\n");
        for (int i=0; i<FULL_SIGNAL_LEN; i++) printf("%c ", (int)scoring[1][i] == -1 ? '?' : (int)scoring[1][i] < 0x0a ? 0x30 + (int)scoring[1][i]: 0x37 + (int)scoring[1][i]);
        printf("\n\n");
    });
    
    return true;
}

void AudioEx::gft(Float32 samples[])
{
    // Kaiser-Bessel windowing filter
    for (int i=0; i<SAMPLING_LENGTH; i++) samples[i] *= wnd_coeffs[i];
    
    // magnitudes^2
    static Float32 gft_mags2[FREQ_COUNT];
    
    // complex data for phase calculation
    static Float32 p_re[FREQ_COUNT];
    static Float32 p_im[FREQ_COUNT];
    static int gft_phases[FREQ_COUNT];
    
    for (int f=0; f<FREQ_COUNT; f++)
    {
        Float32 q1 = 0.0, q2 = 0.0;
        for (int i=0; i<SAMPLING_LENGTH; i++)
        {
            Float32 q0 = gft_coeff_cosine[f] * q1 - q2 + samples[i];
            q2 = q1;
            q1 = q0;
        }
        
        // complex part
        Float32 re = q1 - q2 * 0.5 * gft_coeff_cosine[f];
        Float32 im = q2 * gft_coeff_sine[f];
        
        // magnitude squared
        //gft_mags2[f] = q1 * q1 + q2 * q2 - q1 * q2 * gft_coeff_cosine[f];
        gft_mags2[f] = re*re + im*im;
        
        Float32 d_re = re*p_re[f] + im*p_im[f];
        Float32 d_im = -im*p_re[f] + re*p_im[f];
        
        // save complex values
        p_re[f] = re;
        p_im[f] = im;
        
        
        // estimate phase difference
        /*if(d_re*d_re > d_im*d_im) // phase difference is a multiple of pi
        {
            if(d_re > 0) gft_phases[f] = 0;   // sign -> odd
            else gft_phases[f] = 1;            // sign -> even
        } else
        {
            if(d_im > 0) gft_phases[f] = 2;
            else gft_phases[f] = 3;
        }
        */
        
        if(d_re*d_re > d_im*d_im && d_re < 0) gft_phases[f] = 0;
        else gft_phases[f] = 1;
    }
    
    // reset previous result
    result = 0;
    
    // process result
    detect(gft_mags2, gft_phases);
}

#define H_LEN (DATA_LEN+CRC_LEN+1) // must be less than RS_N

void AudioEx::signal_generator_data_from_int(unsigned int value, AUDIO_DATA& data)
{
    // calculate 8-bit checksum
    unsigned char crc = crc8_int(value);
    unsigned char crc_msb = (crc & 0xf0) >> 4;
    unsigned char crc_lsb = crc & 0x0f;
    // values
    char h[H_LEN];
    snprintf(h, H_LEN, "%08X%01X%01X", value, crc_msb, crc_lsb);
    // START
    int j = 0;
    data[j++] = CW_ST0[0];
    data[j++] = CW_ST0[1];
    // ECC
    // RS(15, 11)
    unsigned char r[RS_N];
    memset(r, 0x0, RS_N);
    for (int i=0; i<H_LEN-1; i++) r[i] = h[i] < 0x41 ? h[i] - 0x30 : h[i] - 0x37;
    encode_rs_char(rs_codec, &r[0], &r[RS_N-RS_PARITY]);
    for (int i=0; i<RS_PARITY; i++)
    {
        data[j++] = CW_DATA[r[RS_N-RS_PARITY+i]][0];
        data[j++] = CW_DATA[r[RS_N-RS_PARITY+i]][1];
    }
    // CRC
    {
        // MSB
        data[j++] = CW_DATA[crc_msb][0];
        data[j++] = CW_DATA[crc_msb][1];
        // LSB
        data[j++] = CW_DATA[crc_lsb][0];
        data[j++] = CW_DATA[crc_lsb][1];
    }
    // DATA
    for (int i=0; i<DATA_LEN; i++)
    {
        data[j++] = CW_DATA[r[i]][0];
        data[j++] = CW_DATA[r[i]][1];
    }
}

void AudioEx::signal_generator_reset()
{
    signal_generator.length = SIGNAL_GENERATOR_LEN;
    signal_generator.carrier = 0.0;
    signal_generator.phase = 0.0;
    signal_generator.remaining = 0;
    signal_generator.data_pos = -1;
}

bool AudioEx::signal_generator_data(AUDIO_DATA& data)
{
    // check remaining samples
    if (signal_generator.remaining > 0)
    {
        LOG({
            printf("[ERROR] bad signal_generator_data() call, data in buffer: %i", signal_generator.remaining);
        });
        return false;
    }
    // load freq data
    if (++signal_generator.data_pos < FULL_SIGNAL_LEN)
    {
        int freq_num = data[signal_generator.data_pos];
        if (freq_num < 0 || freq_num > FREQ_COUNT)
        {
            LOG({
                printf("[ERROR] bad freq number: %i", freq_num);
            });
            return false;
        }
        signal_generator.carrier = signal_freqs[freq_num];
        //signal_generator.phase = (signal_generator.data_pos % 2 ? 1.0 /*sin(M_PI_2)*/ : 0.0);
        signal_generator.remaining = signal_generator.length;
        // DEBUG
        //printf("GEN: freq=%f, phase=%f\n", signal_generator.carrier, signal_generator.phase);
        return true;
    } else {
        // finish
        signal_generator_reset();
        return false;
    }
    LOG({
        printf("[ERROR] bad signal_generator_data() call");
    });
    return false;
}

AudioEx::~AudioEx()
{
    free_rs_char(rs_codec);
}
