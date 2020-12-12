#ifndef TAIR_CONTEST_RECYCLE_H
#define TAIR_CONTEST_RECYCLE_H

#include "Config.hpp"
#include "util.h"

class Recycle {
public:
    void init() {
        // 10M * 4B = 40MB
        int *base = (int *) malloc(16 * 1024 * 1024 * sizeof(int));
        memset(recycles_len, 0, sizeof(int) * BIT_INTERVAL);
        for (int i = 32; i < 64; i += 16) {   // 2
            recycles_len[i] = (1024 + 512) * 386;
        }
        for (int i = 64; i < 192; i += 16) {  // 8
            recycles_len[i] = (1024 + 512) * 512;
        }
        for (int i = 192; i <= 432; i += 16) { // 16
            recycles_len[i] = (1024 + 512) * 64;
        }
        int *offset = base;
        for (int i = 0; i < BIT_INTERVAL; i++) {
            if (recycles_len[i] > 0) {
                recycles[i] = offset;
                offset += recycles_len[i];
            }
        }
        memset(recycle_pos, 0, BIT_INTERVAL * sizeof(int));
    }

    long Get(int pos) {
        if (recycle_pos[pos] == 0) {
            return 0;
        }
        return real_offset(recycles[pos][--recycle_pos[pos]]);
    }

    bool Set(int pos, int node_offset) {
        if (recycle_pos[pos] < recycles_len[pos]) {
            recycles[pos][recycle_pos[pos]++] = node_offset;
            count++;
            return true;
        }
        return false;
    }

    int recycle_pos[BIT_INTERVAL];
    int recycles_len[BIT_INTERVAL];
    int *recycles[BIT_INTERVAL];
    int count = 0;
};


#endif //TAIR_CONTEST_RECYCLE_H