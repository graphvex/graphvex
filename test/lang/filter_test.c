/* Owner: lang/filter.h. Numeric token ABI/constructors/getters including high
 * ID16, low payload48, binary32 gain bits, out-of-range radius preservation.
 * Submission validation is exercised by compositor/compositor_test.c. */
#include "lang/filter.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

// Verifies the public filter-language compatibility surface.
int main(void) {
    assert(sizeof(FilterToken) == 8);
    assert(Filter_identity() == 0);
    assert(Filter_id(Filter_identity()) == FILTER_IDENTITY);
    assert(Filter_payload(Filter_identity()) == 0);
    assert(Filter_gain(1) == UINT64_C(0x000100003f800000));
    assert(Filter_gain(2) == UINT64_C(0x0001000040000000));
    assert(Filter_gain(-0.0f) == UINT64_C(0x0001000080000000));
    assert(Filter_id(Filter_gain(NAN)) == FILTER_GAIN);
    assert(Filter_payload(Filter_gain(INFINITY)) == UINT64_C(0x7f800000));
    assert(Filter_payload(Filter_gain(FLT_MAX)) == UINT64_C(0x7f7fffff));
    assert(Filter_scatterBlur(0) == UINT64_C(0x0002000000000000));
    assert(Filter_scatterBlur(16) == UINT64_C(0x0002000000000010));
    assert(Filter_payload(Filter_scatterBlur(UINT32_MAX)) == UINT32_MAX);
    assert(Filter_id(UINT64_MAX) == UINT16_MAX);
    assert(Filter_payload(UINT64_MAX) == FILTER_PAYLOAD_MASK);
    puts("filter ID16/payload48 numeric ABI: PASS");
    return 0;
}
