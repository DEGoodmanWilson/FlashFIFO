/************************************
 flash_port.h
 Copyright 2013 D.E. Goodman-Wilson

 This file is part of FlashFIFO.

 FlashFIFO is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 FlashFIFO is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with FlashFIFO.  If not, see <http://www.gnu.org/licenses/>.

*************************************

 This file defines an abstract API that defines the hardware-level flash
 interface. These functions will either need to be implemented in your own code
 or else refactored into the functions you actually have.

 It is an assumption of FlashFIFO that single byte writes to flash are atomic.
 No other assumptions of atomicity are assumed.

************************************/

#ifndef SPI_PORT_H
#define	SPI_PORT_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>

//initialize the flash parameters to match the physical layout
void flash_init(void);

//write a value to flash
int flash_write(uint32_t addr, void* data, size_t n);

//erase a value from flash. Notice that addr and len are basically only
//used to determine which and how many pages to erase, as erase commands
//affect entire pages at a time. So be careful calling it!
void flash_erase(uint32_t addr, size_t len);

//read a value from flash
int flash_read(uint32_t addr, void* data, size_t n);


#ifdef	__cplusplus
}
#endif

#endif	/* SPI_PORT_H */

