#include "net/ApiTypes.h"

namespace {
    int hexVal(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    String decode(const char* s, size_t n) {
        String out;
        out.reserve(n);
        for (size_t i = 0; i < n; i++) {
            const char c = s[i];
            if (c == '+') {
                out += ' ';
            } else if (c == '%' && i + 2 < n) {
                const int hi = hexVal(s[i + 1]);
                const int lo = hexVal(s[i + 2]);
                if (hi < 0 || lo < 0) {
                    out += c;
                    continue;
                }
                out += (char)(hi << 4 | lo);
                i += 2;
            } else {
                out += c;
            }
        }
        return out;
    }
}  // namespace

bool QueryArgs::find(const String& key, String* out) const {
    const char* p = q_;
    while (*p) {
        const char* end = strchr(p, '&');
        if (!end)
            end = p + strlen(p);
        const char*  eq   = (const char*)memchr(p, '=', end - p);
        const char*  kEnd = eq ? eq : end;
        const String k    = decode(p, kEnd - p);
        if (k == key) {
            if (out)
                *out = eq ? decode(eq + 1, end - eq - 1) : String();
            return true;
        }
        p = *end ? end + 1 : end;
    }
    return false;
}

bool QueryArgs::has(const String& key) const {
    return find(key, nullptr);
}

String QueryArgs::get(const String& key) const {
    String v;
    find(key, &v);
    return v;
}
