/************************************
 FIFO.c
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

 This file implements the API described in FIFO.h. This API defines a set
 of functions and datatypes for creating a FIFO buffer in SPI NOR flash.
 There are two important details that make a NOR flash FIFO different from
 one implemented in RAM. First, being persistent storage, we want to minimize
 the data loss that will accompany an unexpected power-off . Second, because
 NOR flash must be erased before it can be re-written, and it must be flashed
 a page at a time, the FIFO requires careful page management to avoid
 clobbering good data, while at the same time maximizing utilization of
 the available space. Finally, becase NOR flash has a lifetime measureable
 in minutes if not used properly, the number of page erases is to be
 minimized. This third requirement is largely the responsibility of the
 calling program, but there are things we can do here to ease that process.

************************************/

#include <stdlib.h>
#include <stdint.h>
#include "configure.h"
#include "fifo.h"
#include "flash_port.h"

/*************************

 The memory layout used here is somewhat opaque.
 Each write to an address begins with 4 bytes of size (of the data being written), followed
 by an 8-bit bitfield indicating the write status (0xFF = invalid, 0xFE = valid, 0xFC = consumed)
 So, for example, writing the array {1, 2, 3, 4} to address 0x00 results in
 0x00000004, 0xFE, 0x01, 0x02, 0x03, 0x04 (13 bytes)
 Then, a second write of, e.g. {5, 6, 7} would yield, starting at address 0x0D
 0x00000003, 0xFE, 0x05, 0x06 0x07


 *************************/

// Initialize any static variables

void
fs_init(void) {

}

// Clear (truncate) All files
// Unexpected power down should result only in cleared files, or no change
// When function returns, filesystem should be sync'd

void
fs_format(void) {
    flash_erase(0, FLASH_CHIP_SIZE);
}

// Equivilent to calling sync on all open handles

void
fs_sync(void) {

}

// Initialize anything in the per-handle structure

file_handle_t *
file_open(enum FILE_ID id) {
    file_handle_t * ret = malloc(sizeof (file_handle_t));
    ret->start = id * FILE_SIZE + FILE_OFFSET;
    ret->raw_read_chunk_start = 0; //so we can recover the location of the start of the current chunk!
    ret->read_offset = 0;
    ret->raw_read_chunk_offset = 0; //start of actual data, relative to the end of the metadata in this chunk
    ret->write_offset = 0;
    ret->destructive_read_offset = 0;
    return ret;
}

// Clean-up handle structure
// When this function returns, the flash state must reflect all pending writes
// in order

void
file_close(file_handle_t * handle) {
    file_sync(handle);
    free(handle);
}

// Delete the first n bytes of file, move file handles to point to same data
// In case of unexpected power down, the state of the flash must at all times
// reflect either the unchanged file, or the file with all N bytes deleted.

void
file_consume(file_handle_t * handle, size_t n) {
    handle->destructive_read_offset += n; //TODO check to make sure does not surpass read ptr or write ptr!

    //NOW, let's see if we can recycle the memory we just consumed. IF the consumption
    //takes us across a page boundary AND we are not currently writing within this page, then we
    //can erase it in prep for future writes here. Notice that this will require an update
    //to the metadata table

}

// return the number of bytes that would be returned if one were to seek to 0
// then read until end of file

uint32_t
file_size(file_handle_t * handle) {

}

// Commit all changes to flash.
// If unexpected power down, flash state will reflect entire pending writes in order,
// or no change.
// When this function returns, flash state must reflect all pending writes

void
file_sync(file_handle_t * handle) {

}

// if end-of-file, return the number of bytes read

size_t
file_read(file_handle_t * handle, uint8_t* data, size_t size) {
    //TODO skipping bad chunks!
    //TODO check for catching up to write pointer , memory wrap-around, etc!
    uint32_t i = 0;
    while(size)
    {
        //read in the current chunk size, so we can calculate where the next chunk begins
        uint32_t remaining_chunk_size;

        flash_read(handle->raw_read_chunk_start, &remaining_chunk_size, 4);
        remaining_chunk_size -= handle->raw_read_chunk_offset;

        //make sure that the current chunk is valid. Notice that if raw_read_chunk_offset != 0, we have already validated the current chunk
        if(!handle->raw_read_chunk_offset) //we are at the beginning of a new chunk. Let's validate it
        //TODO NEED TO CHECK THAT WE ARE NOT EXTENDING BEYOND THE END OF THE FILE!!
        {
           uint32_t chunk_size;
           uint8_t valid = 0;
           flash_read(handle->raw_read_chunk_start, &chunk_size, 4);
           flash_read(handle->raw_read_chunk_start + 4, &valid, 1);
           while(0xFE != valid)
           {
               handle->raw_read_chunk_start += chunk_size + 5;

               flash_read(handle->raw_read_chunk_start, &chunk_size, 4);
               flash_read(handle->raw_read_chunk_start + 4, &valid, 1);
           }
        }

        //is the current chunk smaller than what we need? If so, we will be moving to the next chunk.
        if(remaining_chunk_size >= size) //chunk is smaller, we will only read what we need
        {
            uint32_t read_amount = flash_read(handle->raw_read_chunk_start + 5 + handle->raw_read_chunk_offset, (void*)(data+i), size);
            handle->read_offset += read_amount;
            handle->raw_read_chunk_offset += read_amount;
            size -= read_amount;
            i += read_amount;
            return i;
        }
        else //we need to shift into the next chunk, and keep going
        {
            //read all of the remaining chunk
            uint32_t read_amount = flash_read(handle->raw_read_chunk_start + 5 + handle->raw_read_chunk_offset, (void*)(data+i), remaining_chunk_size);
            handle->read_offset += read_amount;
            size -= read_amount;
            i += read_amount;
            //move to next chunk
            handle->raw_read_chunk_start += read_amount + 5;
            handle->raw_read_chunk_offset = 0;
        }
    }
}

// set read and write pointers to offset in file
// Whence is SET_SEEK, SET_END, or something else from stdio.h

void
file_seek(file_handle_t * handle, uint32_t offset, int whence) {

}

// return the position of the read (write?) pointer

uint32_t file_tell(file_handle_t * handle) {
    return handle->read_offset;
}

// If file full, return number of bytes written
// Filesystem should never fill unless filesize is more than
// half (quarter?) of the allocated flash space

size_t
file_write(file_handle_t *handle, uint8_t* data, size_t size) {
    uint32_t start = handle->start + handle->write_offset;

    //First, write first bit of metadata containing the actual addresses we are attempting to write to
    flash_write(start, &size, 4);

    //Now, attempt to commit the data itself
    flash_write(start + 4 + 1, data, size);

    //If we reach here successfully, the data is written and valid. Mark it so in the metadata
    uint8_t flags = 0xFE;
    flash_write(start + 4, &flags, 1);


    handle->write_offset += 4 + 1 + size; //skip the metadata and written
}
