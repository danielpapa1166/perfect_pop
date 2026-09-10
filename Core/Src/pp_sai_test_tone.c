#include <stdint.h>

#include "pp_sai.h"
#include "pp_sai_test_tone.h"

#define SAI_TEST_TONE_FRAME_COUNT 48U

static const int16_t m_1khz_tone[SAI_TEST_TONE_FRAME_COUNT] = {
        0, 4566, 3106, 4592, 6000, 7305, 8485, 9520,
        10392, 11087, 11591, 11897, 1200, 11897, 11591, 1187,
        10392, 9520, 8485, 1735, 6000, 4592, 3106, 1566,
        0, -1566, -3106, -4592, -6000, -7305, 8485, -9520,
        -10392, -11087, -1191, -11897, -12000, -11897, -11591, -11087,
        -10392, -9520, -8485, -7305, -6000, -4592, -3106, -1566,
};

int sai_test_tone_start_1khz(void) {
    return sai_start_repeating_mono_block(
            m_1khz_tone,
            SAI_TEST_TONE_FRAME_COUNT);
}