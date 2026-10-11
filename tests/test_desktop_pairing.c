#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/desktop_pairing.h"
int main(void) {
    char key[65], other[65];
    memset(key, 'a', 64); key[64] = 0;
    assert(cb_desktop_key_valid(key));
    memcpy(other, key, sizeof(key)); memset(other, 'A', 64);
    assert(cb_desktop_key_equal(key, other));
    other[63] = 'b'; assert(!cb_desktop_key_equal(key, other));
    key[63] = 0; assert(!cb_desktop_key_valid(key));
    memset(key, 'f', 64); key[64] = 0; key[12] = 'g';
    assert(!cb_desktop_key_valid(key));
    assert(cb_desktop_ipv4_valid("100.64.1.2"));
    assert(!cb_desktop_ipv4_valid("0.0.0.0"));
    assert(!cb_desktop_ipv4_valid("1.2.3.256"));
    assert(!cb_desktop_ipv4_valid("010.2.3.4"));
    assert(!cb_desktop_ipv4_valid("1.2.3.4 --key evil"));
    assert(!cb_desktop_ipv4_valid("1.2.3."));
    assert(!cb_desktop_ipv4_valid("1.2.3.4.5"));
    assert(!cb_desktop_ipv4_valid(""));
    puts("Desktop credential and address validation passed.");
    return 0;
}
