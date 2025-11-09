#include <runtime/types.h>

extern "C" {
    u64 __udivdi3(u64 n, u64 d) {
        if (d == 0) return 0;
        
        u64 q = 0;
        u64 r = 0;
        
        for (int i = 63; i >= 0; i--) {
            r = r << 1;
            r |= (n >> i) & 1;
            if (r >= d) {
                r -= d;
                q |= (u64)1 << i;
            }
        }
        
        return q;
    }
    
    u64 __umoddi3(u64 n, u64 d) {
        if (d == 0) return 0;
        
        u64 r = 0;
        
        for (int i = 63; i >= 0; i--) {
            r = r << 1;
            r |= (n >> i) & 1;
            if (r >= d) {
                r -= d;
            }
        }
        
        return r;
    }
}
