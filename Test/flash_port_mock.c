/************************************
 flash_port_mock.c
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

 This file implements the flash API defined in flash_port.c. This implementation
 is meant to be a reasonably faithful emulation of how a NOR flash actually works.
 However, this implementation is geared specifically towards testing the FIFO
 classes. To this end, it provides some additional functions for setting up
 test cases such as power failures and the like.

************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "configure.h"
#include "flash_port.h"

//some counters for implementing power failure simulation
uint8_t write_count, fail_after, is_off;

//
uint8_t store[FLASH_CHIP_SIZE]; //the simulated flash itself

void flash_init(void)
{
    write_count = fail_after = is_off = 0;
    for(uint32_t i = 0; i < FLASH_CHIP_SIZE; ++i)
        store[i] = 0xFF;
}


static void store_write(uint32_t addr, void*data, size_t n){
    for(uint32_t i = addr; i < (addr+n); ++i)
    {
        store[i] &= *(uint8_t*)(data+i-addr); //simulate NOR flash!
    }
}

static void store_read(uint32_t addr, void* data, size_t n){
    for(uint32_t i = 0; i < n; ++i)
        *(uint8_t*)(data+i) = store[i+addr];
}

void flash_force_fail(uint8_t count)
{
    fail_after = count;
    write_count = 0;
}

void flash_force_succeed(void)
{
    fail_after = 0;
    write_count = 0;
    is_off = 0;
}

int flash_write(uint32_t addr, void*data, size_t n){
	assert( addr < FLASH_CHIP_SIZE );
	assert( addr + n <= FLASH_CHIP_SIZE );

        if(fail_after)
        {
            if(fail_after == write_count){
		//printf("Simulate unexpected power down!\n");
                is_off = 1;
            }
        }

        write_count++;


        if(is_off) return 0; //powered off, can't write!

	store_write(addr, data, n);
	return n;
}

int flash_read(uint32_t addr, void* data, size_t n){
	assert( addr < FLASH_CHIP_SIZE );
	assert( addr + n <= FLASH_CHIP_SIZE );

	store_read(addr,data,n);
	return n;
}

void flash_erase(uint32_t addr, size_t len){
	assert( addr < FLASH_CHIP_SIZE );
	assert( len + addr <= FLASH_CHIP_SIZE );
	assert( len % FLASH_PAGE_SIZE == 0 ); //TODO are these assertions going to be correct?
	assert( addr % FLASH_PAGE_SIZE == 0 );

	char * buf = malloc(len);
	store_read(addr, buf, len );
	for( int i=0; i<len; i++){
		buf[i] |= rand();
	}
	store_write(addr, buf, len);
	if( is_off ){

		//printf("Simulate unexpected power down!\n"); //TODO fix this functionality
		free(buf);
                return; //exit(0);
	}
	memset(buf, 0xff, len );
	store_write(addr, buf, len );

	free(buf);
}

