//
// Created by liu_l on 10/18/2020.
//

#ifndef PUREFLASH_PF_BITMAP_H
#define PUREFLASH_PF_BITMAP_H
#include<stdint.h>
#include <stdlib.h>
#include <cstring>

class PfBitmap {
public:
	int64_t *bits_data;
	int bit_count;
	PfBitmap(int bit_count) {
		this->bit_count = bit_count;
		bits_data = (int64_t *)calloc(bit_count/8/sizeof(int64_t)+1, sizeof(int64_t));
	}
	~PfBitmap() {
		free(bits_data);
		bits_data=NULL;
	}
	inline __attribute__((always_inline)) void set_bit(int index) {
		bits_data[index/(8*sizeof(int64_t))] |= (1 << (index%(8*sizeof(int64_t))));
	}
	inline bool is_empty() {
		int len = (int)(bit_count/8/sizeof(int64_t)+1);
		for(int i=0;i<len;i++) {
			if(bits_data[i] != 0)
				return false;
		}
		return true;
	}
	inline bool is_set(int index) {
		return bits_data[index/(8*sizeof(int64_t))] & (1 << (index%(8*sizeof(int64_t))));
	}
	inline void clear() {
		memset(bits_data, 0, bit_count/8);
	}
};


#endif //PUREFLASH_PF_BITMAP_H
