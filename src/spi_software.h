#ifndef __SPI_SOFTWARE_H
#define __SPI_SOFTWARE_H

#include <stdint.h> // uint8_t

void spi_software_setup(uint8_t mode);
void spi_software_prepare(void);
void spi_software_transfer(uint8_t receive_data, uint16_t len, uint8_t *data);

#endif // spi_software.h
