// dma.h - DMA controller header
#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stdbool.h>

// DMA functions
void dma_setup_channel(uint8_t channel, uint32_t addr, uint16_t length, bool is_16bit);
void dma_enable_channel(uint8_t channel);
void dma_disable_channel(uint8_t channel);

#endif // DMA_H