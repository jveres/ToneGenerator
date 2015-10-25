//
// VJ / 2013
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "rs.h"

#define DEBUG
#define METERING_ENABLED

#ifdef DEBUG
#define LOG(_x_) _x_
#else
#define LOG(_x_)
#endif

typedef float Float32;

// freqs
#define FREQ_COUNT 7

// audio samples filtering window size
#define SAMPLING_LENGTH 525 //  44100/525=84 multiple integer

//static const Float32 signal_freqs[FREQ_COUNT] = {18518.0, 18690.0, 18862.0, 19035.0, 19207.0, 19379.0, 19552.0}; // bins: 215, 217, 219, 221, 223, 225, 227 (1 bin width = 86.13Hz)
static const Float32 signal_freqs[FREQ_COUNT] = {18102.0, 18270.0, 18438.0, 18606.0, 18774.0, 18942.0, 19110.0}; // bins: 215, 217, 219, 221, 223, 225, 227 (1 bin width = 84.00Hz)

typedef int FREQ_PAIR[2];

#define CW_DATA_LEN 16

static const FREQ_PAIR CW_ST0 = {0, 6};
static const FREQ_PAIR CW_DATA[CW_DATA_LEN] = {{0, 1}, {5, 6}, {1, 2}, {4, 5}, {2, 3}, {3, 4}, {0, 2}, {4, 6}, {1, 3}, {3, 5}, {2, 4}, {0, 3}, {3, 6}, {1, 4}, {2, 5}, {0, 4}}; // 0..F

#define SIGNAL_FRAMES 4
#define SIGNAL_GENERATOR_LEN (SIGNAL_FRAMES*SAMPLING_LENGTH)

// error correction coding parameters
#define RS_SYMSIZE 4
#define RS_PARITY 4
#define RS_POLY 0x13
#define RS_N ((1 << RS_SYMSIZE) - 1)
#define RS_K (RS_N - RS_PARITY)

typedef struct {
    int length;
    int remaining;
    double carrier;
    double phase;
    int data_pos;
} SIGNAL_GENERATOR;

#define ST_LEN 1
#define CRC_LEN 2
#define ECC_LEN (RS_PARITY + CRC_LEN)
#define DATA_LEN 8

#define FULL_SIGNAL_LEN (2*(ST_LEN+ECC_LEN+DATA_LEN))
#define SIGNAL_TEST_PADDING (SIGNAL_FRAMES)
#define SIGNAL_TEST_FRAME_LEN (SIGNAL_FRAMES*FULL_SIGNAL_LEN+SIGNAL_TEST_PADDING)
#define PAYLOAD_LEN (ECC_LEN+DATA_LEN)

typedef int AUDIO_DATA[FULL_SIGNAL_LEN];

#define MIN_PEAK 0.003
#define MAX_PAYLOAD_DIFF 4
#define MAX_PHASE_CHANGE (FULL_SIGNAL_LEN/2)

#define ST0 0

static const int CW_ST_TEST_LOOKUP[FREQ_COUNT][FREQ_COUNT] = {
    {  -1,  -1,  -1,  -1,  -1,  -1, ST0},
    {  -1,  -1,  -1,  -1,  -1,  -1,  -1},
    {  -1,  -1,  -1,  -1,  -1,  -1,  -1},
    {  -1,  -1,  -1,  -1,  -1,  -1,  -1},
    {  -1,  -1,  -1,  -1,  -1,  -1,  -1},
    {  -1,  -1,  -1,  -1,  -1,  -1,  -1},
    {  -1,  -1,  -1,  -1,  -1,  -1,  -1}
};

static const int CW_DATA_TEST_LOOKUP[FREQ_COUNT][FREQ_COUNT] = {
    {  -1, 0x0, 0x6, 0xb, 0xf,  -1,  -1},
    {  -1,  -1, 0x2, 0x8, 0xd,  -1,  -1},
    {  -1,  -1,  -1, 0x4, 0xa, 0xe,  -1},
    {  -1,  -1,  -1,  -1, 0x5, 0x9, 0xc},
    {  -1,  -1,  -1,  -1,  -1, 0x3, 0x7},
    {  -1,  -1,  -1,  -1,  -1,  -1, 0x1},
    {  -1,  -1,  -1,  -1,  -1,  -1,  -1}
};

class AudioEx {
public:
    Float32 rx_level;
    unsigned int result;
    SIGNAL_GENERATOR signal_generator;
    AudioEx(Float32 sampleRate);
    ~AudioEx();
    void gft(Float32 samples[]);
    void signal_generator_data_from_int(unsigned int value, AUDIO_DATA& data);
    void signal_generator_reset();
    bool signal_generator_data(AUDIO_DATA& data);
private:
    Float32 sample_rate;
    Float32 gft_coeff_cosine[FREQ_COUNT];
    Float32 gft_coeff_sine[FREQ_COUNT];
    Float32 wnd_coeffs[SAMPLING_LENGTH];
    void *rs_codec;
    unsigned int payload_test(int payload[PAYLOAD_LEN]);
    void cw_lookup_test(const int test[2][2], const int lookup[FREQ_COUNT][FREQ_COUNT], int* t1, int* t2, int* t3, int* t4);
    bool scoring_test(const Float32 scoring[4][FULL_SIGNAL_LEN], int payload[PAYLOAD_LEN]);
    bool generate_scoring(int fft_i, const Float32 fft_powers[FREQ_COUNT][SIGNAL_TEST_FRAME_LEN], const int fft_max_powers[2][SIGNAL_TEST_FRAME_LEN], const int fft_phases[FREQ_COUNT][SIGNAL_TEST_FRAME_LEN], Float32 scoring[4][FULL_SIGNAL_LEN]);
    void detect(Float32 mags[FREQ_COUNT], int phases[FREQ_COUNT]);
};
