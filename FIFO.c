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

static uint8_t open_handles[FILE_MAX] = {0}; //keeps track of which handles are currently open

//a helper function to determine the amount of free space.
static uint32_t free_space(file_handle_t *handle)
{
    return handle->free_space;
}

static uint32_t used_space(file_handle_t *handle)
{
    return FILE_SIZE - handle->free_space;
}

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
    if(open_handles[id] >= MAX_HANDLES) //already open; could be easily expanded to contain the number of open handles
        return NULL;
    ++open_handles[id];

    file_handle_t * ret = malloc(sizeof (file_handle_t));
    ret->file_id = id;
    ret->start = id * FILE_SIZE + FILE_OFFSET;
    ret->raw_read_chunk_start = 0; //so we can recover the location of the start of the current chunk!
    ret->raw_read_chunk_offset = 0; //start of actual data, relative to the end of the metadata in this chunk
    ret->write_offset = 0;
    ret->destructive_read_offset = 0;
    ret->free_space = FILE_SIZE;
    return ret;
}

// Clean-up handle structure
// When this function returns, the flash state must reflect all pending writes
// in order

void
file_close(file_handle_t * handle) {
    file_sync(handle);
    --open_handles[handle->file_id];
    free(handle);
}

static uint8_t check_read_pointer(file_handle_t *handle)
{
  if(handle->raw_read_chunk_start == handle->write_offset) //we have caught up with read pointer
    return 1; //we are done moving it forward
  //otherwise see if the data is invalid or otherwise needs to be skipped
  uint8_t check = 0;
  flash_read(handle->start + handle->raw_read_chunk_start+1, &check, 1);
  if(check == 0xFE) //a block we can read!
    return 1;
  return 0;
}

static void advance_read_pointer_to_next_chunk(file_handle_t *handle)
{
  //we might be fine where we are.
  //we might need to advance past the end matter in a page
  //we might need to skip one or more invalid chunks.
  //these two steps need to be repeated until one of two conditions arise:
  // We reach a valid, written chunk or
  // We reach the read pointer
  uint8_t done = 0;

  //we begin by advancing from the current chunk.
  uint8_t check = 0;
  flash_read(handle->start + handle->raw_read_chunk_start, &check, 1);
  handle->raw_read_chunk_start += check + 2;
  //check for wrap-around
  if(handle->raw_read_chunk_start >= FILE_SIZE)
    handle->raw_read_chunk_start = 0;

  while(!done)
  {
    done = check_read_pointer(handle);
    if(!done)
    {
      //check to see if the obstruction is invalid data
      check = 0;
      flash_read(handle->start + handle->raw_read_chunk_start, &check, 1);
      if(check != 0xFF) //invalid chunk, move to next chunk
      {
        handle->raw_read_chunk_start += check + 2;
        //check for wrap-around
        if(handle->raw_read_chunk_start >= FILE_SIZE)
          handle->raw_read_chunk_start = 0;
      }
      done = check_read_pointer(handle);
    }
    if(!done)
    {
      //check to see if the obstruction is leftovers at end of page
      uint8_t check = 0;
      flash_read(handle->start + handle->raw_read_chunk_start, &check, 1);
      if(check == 0xFF) //leftovers at end of page
      {
        handle->raw_read_chunk_start += FLASH_PAGE_SIZE - (handle->raw_read_chunk_start % FLASH_PAGE_SIZE);
        //check for wrap-around
        if(handle->raw_read_chunk_start >= FILE_SIZE)
          handle->raw_read_chunk_start = 0;
      }
      done = check_read_pointer(handle);
    }
  }
}

static uint8_t check_destructive_read_pointer(file_handle_t *handle)
{
  if(handle->destructive_read_offset == handle->raw_read_chunk_start) //we have caught up with read pointer
    return 1; //we are done moving it forward
  //otherwise see if the data is invalid or otherwise needs to be skipped
  uint8_t check = 0;
  flash_read(handle->start + handle->destructive_read_offset+1, &check, 1);
  if(check == 0xFE) //a block we can read!
    return 1;
  return 0;
}

static void advance_destructive_read_pointer_to_next_chunk(file_handle_t *handle)
{
  //we might be fine where we are.
  //we might need to advance past the end matter in a page
  //we might need to skip one or more invalid chunks.
  //these two steps need to be repeated until one of two conditions arise:
  // We reach a valid, written chunk or
  // We reach the read pointer
  uint8_t done = 0;

  //we begin by advancing from the current chunk.
  uint8_t size = 0;
  flash_read(handle->start + handle->destructive_read_offset, &size, 1);
  handle->destructive_read_offset += size + 2;
  handle->free_space += size + 2;
  //check for wrap-around
  if(handle->destructive_read_offset >= FILE_SIZE)
    handle->destructive_read_offset = 0;

  while(!done)
  {
    done = check_destructive_read_pointer(handle);
    if(!done)
    {
      //check to see if the obstruction is invalid data
      uint8_t check = 0;
      flash_read(handle->start + handle->destructive_read_offset, &check, 1);
      if(check != 0xFF) //invalid chunk, move to next chunk
      {
        handle->destructive_read_offset += check + 2;
        handle->free_space += check + 2;
        //check for wrap-around
        if(handle->destructive_read_offset >= FILE_SIZE)
          handle->destructive_read_offset = 0;
      }
      done = check_destructive_read_pointer(handle);
    }
    if(!done)
    {
      //check to see if the obstruction is leftovers at end of page
      uint8_t check = 0;
      flash_read(handle->start + handle->destructive_read_offset, &check, 1);
      if(check == 0xFF) //leftovers at end of page
      {
        handle->free_space += FLASH_PAGE_SIZE - (handle->destructive_read_offset % FLASH_PAGE_SIZE);
        handle->destructive_read_offset += FLASH_PAGE_SIZE - (handle->destructive_read_offset % FLASH_PAGE_SIZE);
        //check for wrap-around
        if(handle->destructive_read_offset >= FILE_SIZE)
          handle->destructive_read_offset = 0;
      }
      done = check_destructive_read_pointer(handle);
    }
  }
}

// Delete the first n bytes of file, move file handles to point to same data
// In case of unexpected power down, the state of the flash must at all times
// reflect either the unchanged file, or the file with all N bytes deleted.

size_t
file_consume(file_handle_t * handle, size_t size) {
    size_t i = 0;
    uint8_t valid;
    while(size)
    {
        //if we have reached the read pointer, stop, do nothing.
        if(handle->destructive_read_offset == handle->raw_read_chunk_start)
            return i;

        //get the current chunk size.
        uint8_t chunk_size = 0;
        flash_read(handle->start + handle->destructive_read_offset, &chunk_size, 1);

        //is the current chunk smaller than what was requested? If so, we will be moving to the next chunk.
        if(chunk_size > size) //current chunk is smaller than read size, leave it be and stop here
        {
            //do not update destructive read pointer, as we are not consuming this chunk
            //but do return the proper size!
            return i;
        }

        else //we will consume this entire chunk, and perhaps keep going as there will be more to consume
        {
            uint8_t flag = 0xFC;
            flash_write(handle->start + handle->destructive_read_offset + 1, &flag, 1); //write the "consumed" flag!
            size -= chunk_size;
            i += chunk_size;

            //move to next chunk
            advance_destructive_read_pointer_to_next_chunk(handle);
        }

        //notice that at this point, the read pointer could be at the start of a new page. If so, we should erase the page we just left behind
        //check to see if page needs erasure. We check by seeing if we crossed a page boundary
        //we do this by seeing if the read pointer is at the first byte of a new page
        if(! (handle->destructive_read_offset % FLASH_PAGE_SIZE) )
        {
            //get the start address for the previous page, and erase it.
            uint32_t page_start;
            if(handle->destructive_read_offset) //if on second or subsequent pages
                page_start = handle->destructive_read_offset - FLASH_PAGE_SIZE;
            else //we just wrapped onto the first page, previous page is at the bottom
                page_start = FILE_SIZE - FLASH_PAGE_SIZE;
            //need to check if page needs erasing. We check if the first chunk is flagged consumed.
            //If so, we check that neither the read nor write pointers are on the page
            uint8_t test;
            flash_read(handle->start + page_start + 1, &test, 1); //if the first value we read is 0xFF, no need to erase.
            if(test == 0xFC) //the first chunk has been consumed. Rather than test all chunks, just see if the read and write pointers made it off the page
            {
                //here is where we test the pointer location to avoid erasing
                //a page currently in use
                if( (handle->write_offset < page_start || handle->write_offset >= (page_start + FLASH_PAGE_SIZE))
                   && (handle->raw_read_chunk_start < page_start || handle->raw_read_chunk_start >= (page_start + FLASH_PAGE_SIZE)) )
                   {
                        flash_erase(handle->start + page_start, FLASH_PAGE_SIZE); //don't know that the full size is required for this operation, but just to be sure.
                   }
            }
        }

    }
    return i;
}

// return the number of bytes that would be returned if one were to seek to 0
// then read until end of file

size_t
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
    size_t i = 0;
    while(size)
    {
        //make sure we are not bumping into write pointer!
        if(handle->raw_read_chunk_start == handle->write_offset)
        {
            //it might be that the write pointer is actually BEHIND us. We can test by looking
            //ahead to see if the current chunk is free or written
            uint8_t size = 0;
            flash_read(handle->raw_read_chunk_start, &size, 1);
            if(size == 0xFF) //the write pointer is ahead of us, ditch
                return i;
            //else the write pointer is actually behind us, and we can proceed
        }

        //read in the current chunk size, so we can calculate where the next chunk begins
        uint8_t remaining_chunk_size;
        uint8_t chunk_size;
        flash_read(handle->raw_read_chunk_start, &chunk_size, 1);
        remaining_chunk_size = chunk_size - handle->raw_read_chunk_offset;

        //is the current chunk smaller than what we need? If so, we will be moving to the next chunk.
        if(remaining_chunk_size > size) //chunk is smaller, we will only read what we need
        {
            uint8_t read_amount = flash_read(handle->raw_read_chunk_start + 2 + handle->raw_read_chunk_offset, (void*)(data+i), size);
            size -= read_amount;
            i += read_amount;

            if(read_amount >= remaining_chunk_size) //we have read the entire chunk, set pointers to beginning of next chunk
            {
                advance_read_pointer_to_next_chunk(handle);
                handle->raw_read_chunk_offset = 0;
            }
            else //we have not read the entire chunk;
            {
                handle->raw_read_chunk_offset += read_amount;
            }

            return i;
        }
        else //we need to shift into the next chunk, and keep going
        {
            //read all of the remaining chunk
            uint8_t read_amount = flash_read(handle->raw_read_chunk_start + 2 + handle->raw_read_chunk_offset, (void*)(data+i), remaining_chunk_size);
            size -= read_amount;
            i += read_amount;
            //move to next chunk
            if(read_amount == chunk_size)
            {
                advance_read_pointer_to_next_chunk(handle);
                handle->raw_read_chunk_offset = 0;
            }
            else //didn't actually read an entire chunk for whatever reason
            {
                handle->raw_read_chunk_offset += read_amount;
            }
        }
    }
    return i;
}

// set read and write pointers to offset in file
// Whence is SET_SEEK, SET_END, or something else from stdio.h

void
file_seek(file_handle_t * handle, uint32_t offset, int whence) {

}

// If file full, return number of bytes written
// Filesystem should never fill unless filesize is more than
// half (quarter?) of the allocated flash space

size_t
file_write(file_handle_t *handle, uint8_t* data, size_t size) {

    if(size >= 0xFF) //reject, because this value is used as a flag for unused memory!
        //Also because we can only write one byte at a time atomically, we limit all metadata writes, including the record of the number of bytes written, to one byte.
        return 0;

    if((size+2) > FLASH_PAGE_SIZE) //reject, because we cannot write chunks larger than the page size
        return 0;

    if((size+2) > free_space(handle)) //reject if not enough available space
        return 0;

    uint32_t start = handle->write_offset;

    //here we need to find where we can put this chunk. Because writes cannot span page boundaries, we need to see if there is space on the current page or not
    //first, identify where the next page boundary is
    uint32_t next_page = FLASH_PAGE_SIZE * (start / FLASH_PAGE_SIZE) + FLASH_PAGE_SIZE;
    //TODO make sure we don't bump into read handles!!
    uint32_t page_free_space = next_page - start;
    if(next_page > FILE_SIZE) //need to roll over to first page
    {
        next_page = 0;
        page_free_space = 0; //to prevent a double subtraction below
    }
    if( (start + size + 2) > next_page) //if not enough room in current page for metadata + data
    {
        //decrese free space by the amount of dead space we just created
        handle->free_space -= page_free_space;
        start = next_page; //shift to next page.
        handle->write_offset = start;
    }

    //need to recheck.
    if((size+2) > free_space(handle)) //reject if not enough available space
        return 0;

    //First, write first bit of metadata containing the actual addresses we are attempting to write to
    flash_write(handle->start+start, &size, 1);

    //Now, attempt to commit the data itself
    flash_write(handle->start+start + 2, data, size);

    //If we reach here successfully, the data is written and valid. Mark it so in the metadata
    uint8_t flags = 0xFE;
    flash_write(handle->start+start + 1, &flags, 1);

    handle->write_offset += 2 + size; //skip the metadata and written
    handle->free_space -= 2 + size;

    return size;
}
