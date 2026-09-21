#pragma once

#include <stdint.h>

void MLX90640_I2CInit(void);
int MLX90640_I2CRead(uint8_t slaveAddr, unsigned int startAddress,
                     unsigned int nWordsRead, uint16_t *data);
int MLX90640_I2CWrite(uint8_t slaveAddr, unsigned int writeAddress, uint16_t data);
void MLX90640_I2CFreqSet(int freqKHz);
