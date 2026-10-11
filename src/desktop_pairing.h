#ifndef CB_DESKTOP_PAIRING_H
#define CB_DESKTOP_PAIRING_H
/* 桌面入口只接受完整凭据和 IPv4 地址，避免配置进入命令行语法。 */
static int cb_desktop_hex(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}
static int cb_desktop_key_valid(const char *key) {
    unsigned i;
    if (!key) return 0;
    for (i = 0; i < 64; ++i) {
        if (!key[i] || cb_desktop_hex((unsigned char)key[i]) < 0) return 0;
    }
    return key[64] == 0;
}
static int cb_desktop_key_equal(const char *a, const char *b) {
    unsigned i, difference = 0;
    if (!cb_desktop_key_valid(a) || !cb_desktop_key_valid(b)) return 0;
    for (i = 0; i < 64; ++i)
        difference |= (unsigned)(cb_desktop_hex((unsigned char)a[i]) ^ cb_desktop_hex((unsigned char)b[i]));
    return difference == 0;
}
static int cb_desktop_ipv4_valid(const char *address) {
    unsigned part, nonzero = 0;
    if (!address) return 0;
    for (part = 0; part < 4; ++part) {
        unsigned value = 0, digits = 0;
        const char *start = address;
        while (*address >= '0' && *address <= '9') {
            value = value * 10 + (unsigned)(*address++ - '0');
            if (++digits > 3 || value > 255) return 0;
        }
        if (!digits || (digits > 1 && *start == '0')) return 0;
        nonzero |= value;
        if (part < 3) { if (*address++ != '.') return 0; }
    }
    return *address == 0 && nonzero != 0;
}
#endif
