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

//and the inverse

static uint32_t used_space(file_handle_t *handle)
{
    return FILE_SIZE - handle->free_space;
}

//thanks to Tom Stephens at HID Global for this cool little snippet.

uint8_t count_ones(uint8_t b)
{
    uint8_t ct = 0;
    for (; b > 0; b &= (uint8_t) (b - 1), ct++);

    return ct;
}

//this is a helper method called by open below

static void find_and_repair_corrupted_pages(file_handle_t *handle)
{
    // The good news is that there can be at most one corrupted page, because we
    // erase pages one at a time.
    // What does a corrupted page look like? One sign is in the first byte.
    // The first byte—the page counter—can only contain numbers in the sequence
    // 0b11111111
    // 0b11111110
    // 0b11111100
    // 0b11111000
    // etc. So if we see a page with a page counter not in this sequence, we know
    // it must be corrupted.
    // But it is possible, of course, for a corrupted page to have a first
    // byte in that sequence. Thus, we can attempt to parse the page; a failure
    // to parse will also indicate a corrupted page.
    for (uint32_t i = 0; i < FILE_SIZE; i += FLASH_PAGE_SIZE)
    {
        uint8_t page_counter;
        uint8_t size, valid, corrupt;
        uint32_t addr;
        flash_read(handle->start + i, &page_counter, PAGE_COUNTER_SIZE);
        switch (page_counter)
        {
        case 0xFF:
        case 0xFE:
        case 0xFC:
        case 0xF8:
        case 0xF0:
        case 0xE0:
        case 0xC0:
        case 0x80:
        case 0x00:
            //do it the hard way
            size = 0;
            valid = 0;
            corrupt = 0;
            addr = i + 1;
            while (!corrupt && (addr < i + FLASH_PAGE_SIZE - 1))
            {
                flash_read(handle->start + addr, &size, 1);
                flash_read(handle->start + addr + 1, &valid, 1);
                //look for a combination that cannot happen
                if (((size == 0xFF) && (valid != 0xFF)) || ((valid != 0xFF) && (valid != 0xFE) && (valid != 0xFC)))
                {
                    corrupt = 1;
                }
                if ((size == 0xFF) && (valid = 0xFF))
                {
                    addr += 2;
                }
                else
                {
                    addr += size + 2;
                }
            }
            if (corrupt)
            {
                flash_erase(handle->start + i, FLASH_PAGE_SIZE);
                i = FILE_SIZE; //break out of for loop, no need to look any further.
            }

            break;
        default:
            //definitely corrupt!
            flash_erase(handle->start + i, FLASH_PAGE_SIZE);
            i = FILE_SIZE; //break out of for loop, no need to look any further.
            break;
        }
    }
}

static void site_write_pointer(file_handle_t* handle)
{
        //Now, let's identify where the write pointer goes.
    //Each page has a byte at the beginning indicating the write sequence;
    //  the number of '1's indicates the write order. Writes start from 0xFE
    //  and continue down to 0x00; The last page written is the one with the
    //  largest value! A value of 0xFF indicates an unused page
    //  Notice that this scheme is bound to fail when files can be larger than
    //  seven blocks. In this case, a two-byte scheme or more is called for.
    //  The modificiations to make that happen shouldn't be hard to make.

    uint8_t first_write_page = 0;
    uint8_t pages_written = 0;
    uint8_t smallest = 0xFF;
    uint8_t i;
    uint8_t counter = 0;
    for (i = 0; i < (FILE_SIZE / FLASH_PAGE_SIZE); ++i) //iterate over pages
    {
        //read first byte
        flash_read(handle->start + (FLASH_PAGE_SIZE * i), &counter, 1);
        if (counter != 0xFF)
        {
            ++pages_written;
            if (counter < smallest)
            {
                smallest = counter;
                first_write_page = i;
            }
        }
    }
    handle->write_offset = first_write_page * FLASH_PAGE_SIZE;
    if (smallest < 0xFF)
    {
        handle->write_count = 8 - count_ones(smallest) + 1;
        if (handle->write_count == 9) handle->write_count = 1;
    }
    if (pages_written)
        handle->free_space = FILE_SIZE - ((pages_written - 1)*(FLASH_PAGE_SIZE - PAGE_COUNTER_SIZE)) - (FILE_SIZE / FLASH_PAGE_SIZE * PAGE_COUNTER_SIZE); //number of bytes written - number of counter bytes

    //now that we have the /page/ let's identify the /chunk/!
    //possibility that this page is entirely free, which we need to consider. Will recognize it because first byte is 0xFF
    //the strategy is to skip chunks until we find one whose size is 0xFF.
    //we do have to worry about flipping pages here, because we need to manage the page counter bytes, of course!
    uint8_t size = 0;
    flash_read(handle->start + handle->write_offset + 1, &size, 1);
    //a free chunk is one in which the size is 0xFF
    if (size == 0xFF) //starting on a fresh page
    {
        //read in the page counter to see if it is free or not.
        uint8_t check = 0;
        flash_read(handle->start + handle->write_offset, &check, PAGE_COUNTER_SIZE);
        if (check == 0xFF) //FREE SPACE! move in.
        {
            uint8_t counter = (0xFF << handle->write_count);
            ++handle->write_count;
            if (handle->write_count == 9) handle->write_count = 1;
            flash_write(handle->start + handle->write_offset, &counter, PAGE_COUNTER_SIZE);
        }
        handle->write_offset += PAGE_COUNTER_SIZE;
    }
    else //need to find our place in this page.
    {
        //skip past page counter
        handle->write_offset += PAGE_COUNTER_SIZE;
        flash_read(handle->start + handle->write_offset, &size, 1);
        while (size != 0xFF)
        {
            //advance a chunk
            handle->write_offset += size + 2;
            handle->free_space -= size + 2;
            //now, we may have crossed a page boundary. If we did, it is because the last page written was /completely/ full.
            // We need to stop advancing at this point, because either a) we have found free space or b) we have bumped up against a not-yet-consumed chunk
            // check which case that is, and if the space is free, then we need to set the page counter
            if (!(handle->write_offset % FLASH_PAGE_SIZE)) //yep, hit the start of a new page!
            {
                //read in the page counter to see if it is free or not.
                uint8_t check = 0;
                flash_read(handle->start + handle->write_offset, &check, PAGE_COUNTER_SIZE);
                if (check == 0xFF) //FREE SPACE! move in.
                {
                    uint8_t counter = (0xFF << handle->write_count);
                    ++handle->write_count;
                    if (handle->write_count == 9) handle->write_count = 1;
                    flash_write(handle->start + handle->write_offset, &counter, PAGE_COUNTER_SIZE);
                    handle->write_offset += PAGE_COUNTER_SIZE;
                }
                //else, do nothing, do not move into the page; it is not ready for writing. Need to linger where we are and wait.
                break; //exit the loop
            }
            flash_read(handle->start + handle->write_offset, &size, 1);
        }
    }
}

static void site_read_pointer(file_handle_t *handle)
{
    //now, somewhat more difficult, we need to locate the destructive read pointer.
    //The general procedure will involve moving /backwards/ from the write pointer.
    // Needless to say, there is no method for moving the pointer backwards chunk-by-chunk.
    // Instead, we need to move backwards a page at a time, then move forwards through that page, keeping
    // track of potential candidate landing chunks. Fun!
    //we begin by starting at the page the write pointer is on.
    handle->destructive_read_offset = FLASH_PAGE_SIZE * (handle->write_offset / FLASH_PAGE_SIZE) + PAGE_COUNTER_SIZE;


    // we need to loop, skipping
    // backwards a page at a time, and looking for either: the last consumed
    // chunk, or an empty page, or we encounter the page the write pointer is on
    uint8_t done = 0;
    uint8_t pages_examined = 0;
    while (!done)
    {
        //examine the page.

        //first, see if write pointer is on this page. since we have already examined this page earlier, we know
        // that at this point, it must be that the write pointer is sitting and waiting on the destructive read pointer to free the page
        // up. So park the destructive read pointer where it is, and break
        if (handle->write_offset + 1 == handle->destructive_read_offset)
            break;

        if ((pages_examined > 0) && (handle->write_offset - handle->destructive_read_offset < FLASH_PAGE_SIZE))
        {
            //here is an oddball case. If we've come full circle—basically, we backed into the write pointer, from whence we started,
            //then it must be the case that the /next/ page (the one we just came from) is where we should be, because it will have been
            //written, but not consumed. This page is going to have been freshly erased, and so is not where we should land.
            //move to the next page
            handle->destructive_read_offset += FLASH_PAGE_SIZE;
            if (handle->destructive_read_offset >= FILE_SIZE)
                handle->destructive_read_offset = 0 + PAGE_COUNTER_SIZE;
            break;
        }

        uint8_t check = 0;
        flash_read(handle->start + handle->destructive_read_offset - PAGE_COUNTER_SIZE, &check, PAGE_COUNTER_SIZE);
        if (check == 0xFF) //this is an empty page. move destructive read pointer to beginning of next page and quit
        {
            //go forward a page
            handle->destructive_read_offset += FLASH_PAGE_SIZE;
            if (handle->destructive_read_offset >= FILE_SIZE)
                handle->destructive_read_offset = 0 + PAGE_COUNTER_SIZE;
            break;
        }

        flash_read(handle->start + handle->destructive_read_offset + 1, &check, 1);
        if (check == 0xFC) //this this chunk is consumed, fast forward over invalid and consumed chunks until we hit a non-consumed valid chunk of the end of the page
        {
            while (1)
            {
                uint8_t c_size = 0;
                uint8_t c_valid = 0;
                flash_read(handle->start + handle->destructive_read_offset, &c_size, 1);
                flash_read(handle->start + handle->destructive_read_offset + 1, &c_valid, 1);
                if (handle->destructive_read_offset == handle->write_offset) //done
                {
                    done = 1; //force our way out of the outer loop
                    break;
                }
                else if (c_size == 0xFF) //empty space at the end of the page; FF to next page and quit
                {
                    //delete the page
                    uint32_t page_start = FLASH_PAGE_SIZE * (handle->destructive_read_offset / FLASH_PAGE_SIZE);
                    flash_erase(handle->start + page_start, FLASH_PAGE_SIZE);
                    handle->destructive_read_offset = FLASH_PAGE_SIZE * (handle->destructive_read_offset / FLASH_PAGE_SIZE) + FLASH_PAGE_SIZE;
                    if (handle->destructive_read_offset == FILE_SIZE)
                        handle->destructive_read_offset = 0;
                    handle->destructive_read_offset += PAGE_COUNTER_SIZE;
                    done = 1; //force our way out of the outer loop
                    break;
                }
                else if (c_valid == 0xFE) //a non-consumed chunk, stop here
                {
                    done = 1; //force our way out of the outer loop
                    break;
                }
                //move to the next chunk
                handle->destructive_read_offset += c_size + 2;
                handle->free_space += c_size + 2;
                if (!(handle->destructive_read_offset % FLASH_PAGE_SIZE)) //end of page
                {
                    //delete the page
                    uint32_t page_start = FLASH_PAGE_SIZE * ((handle->destructive_read_offset - 1) / FLASH_PAGE_SIZE);
                    flash_erase(handle->start + page_start, FLASH_PAGE_SIZE);

                    handle->destructive_read_offset += PAGE_COUNTER_SIZE;
                }
            }
        }

        if (!done) //more pages to look at!
        {
            //go back a page
            if (handle->destructive_read_offset == 1)
                handle->destructive_read_offset = FILE_SIZE - FLASH_PAGE_SIZE + PAGE_COUNTER_SIZE;
            else
                handle->destructive_read_offset -= FLASH_PAGE_SIZE;
        }
        ++pages_examined;
    }

    //Finally, set the read offset to be the destructive read offset
    handle->raw_read_chunk_start = handle->destructive_read_offset;
}


// Initialize anything in the per-handle structure

file_handle_t *
file_open(enum FILE_ID id)
{
    if (open_handles[id] >= MAX_HANDLES) //max handles already open; could be easily expanded to contain the number of open handles
        return NULL;
    ++open_handles[id];

    file_handle_t * ret = malloc(sizeof (file_handle_t));
    ret->file_id = id;
    ret->start = id * FILE_SIZE + FILE_OFFSET;
    ret->raw_read_chunk_start = 1; //so we can recover the location of the start of the current chunk!
    ret->raw_read_chunk_offset = 0; //start of actual data, relative to the end of the metadata in this chunk
    ret->write_offset = 0; //skip counter byte!
    ret->destructive_read_offset = 1;
    ret->write_count = 1;
    ret->free_space = FILE_SIZE - (PAGE_COUNTER_SIZE * FILE_SIZE / FLASH_PAGE_SIZE); //subtracting the number of page count bytes;

    //first things first: let's identify and fix any failed erased pages.
    find_and_repair_corrupted_pages(ret);


    //second, locate the write pointer
    site_write_pointer(ret);

    //finally, locate the read pointer
    site_read_pointer(ret);

    return ret;
}

// Clean-up handle structure
// When this function returns, the flash state must reflect all pending writes
// in order

void
file_close(file_handle_t * handle)
{
    file_sync(handle);
    --open_handles[handle->file_id];
    free(handle);
}

//helper function for advancing the read pointer

static uint8_t check_read_pointer(file_handle_t *handle)
{
    if (handle->raw_read_chunk_start == handle->write_offset) //we have caught up with read pointer
        return 1; //we are done moving it forward
    //otherwise see if the data is invalid or otherwise needs to be skipped
    uint8_t check = 0;
    flash_read(handle->start + handle->raw_read_chunk_start + 1, &check, 1);
    if (check == 0xFE) //a block we can read!
        return 1;
    return 0;
}

//move the read pointer along, updating appropriately

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
    if (handle->raw_read_chunk_start >= FILE_SIZE)
        handle->raw_read_chunk_start = PAGE_COUNTER_SIZE;
    //skip first byte of each page
    if (!(handle->raw_read_chunk_start % FLASH_PAGE_SIZE))
    {
        handle->raw_read_chunk_start += PAGE_COUNTER_SIZE; //skip counter bytes
    }

    while (!done)
    {
        done = check_read_pointer(handle);
        if (!done)
        {
            //check to see if the obstruction is invalid data
            check = 0;
            flash_read(handle->start + handle->raw_read_chunk_start, &check, 1);
            if (check != 0xFF) //invalid chunk, move to next chunk
            {
                handle->raw_read_chunk_start += check + 2;
                //check for wrap-around
                if (handle->raw_read_chunk_start >= FILE_SIZE)
                    handle->raw_read_chunk_start = PAGE_COUNTER_SIZE;
            }
            //skip first byte of each page
            if (!(handle->raw_read_chunk_start % FLASH_PAGE_SIZE))
            {
                handle->raw_read_chunk_start += PAGE_COUNTER_SIZE; //skip counter bytes
            }
            done = check_read_pointer(handle);
        }
        if (!done)
        {
            //check to see if the obstruction is leftovers at end of page
            uint8_t check = 0;
            flash_read(handle->start + handle->raw_read_chunk_start, &check, 1);
            if (check == 0xFF) //leftovers at end of page
            {
                handle->raw_read_chunk_start += FLASH_PAGE_SIZE - (handle->raw_read_chunk_start % FLASH_PAGE_SIZE);
                //check for wrap-around
                if (handle->raw_read_chunk_start >= FILE_SIZE)
                    handle->raw_read_chunk_start = 0;
            }
            //skip first byte of each page
            if (!(handle->raw_read_chunk_start % FLASH_PAGE_SIZE))
            {
                handle->raw_read_chunk_start += PAGE_COUNTER_SIZE; //skip that byte
            }

            done = check_read_pointer(handle);
        }
    }
}

//helper for moving the destructive read pointer forward

static uint8_t check_destructive_read_pointer(file_handle_t *handle)
{
    if (handle->destructive_read_offset == handle->raw_read_chunk_start) //we have caught up with read pointer
        return 1; //we are done moving it forward
    //otherwise see if the data is invalid or otherwise needs to be skipped
    uint8_t check = 0;
    flash_read(handle->start + handle->destructive_read_offset + 1, &check, 1);
    if (check == 0xFE) //a block we can read!
        return 1;
    return 0;
}

//move the destructive read pointer to the next valid chunk

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
    if (handle->destructive_read_offset >= FILE_SIZE)
        handle->destructive_read_offset = 0;
    if (!(handle->destructive_read_offset % FLASH_PAGE_SIZE))
    {
        handle->destructive_read_offset += PAGE_COUNTER_SIZE; //skip that byte
        //Notice that we do not update the free space, since this byte is not available for writing!
    }

    while (!done)
    {
        done = check_destructive_read_pointer(handle);
        if (!done)
        {
            //check to see if the obstruction is invalid data
            uint8_t check = 0;
            flash_read(handle->start + handle->destructive_read_offset, &check, 1);
            if (check != 0xFF) //invalid chunk, move to next chunk
            {
                handle->destructive_read_offset += check + 2;
                handle->free_space += check + 2;
                //check for wrap-around
                if (handle->destructive_read_offset >= FILE_SIZE)
                    handle->destructive_read_offset = 0;
            }
            //skip first byte of each page
            if (!(handle->destructive_read_offset % FLASH_PAGE_SIZE))
            {
                handle->destructive_read_offset += PAGE_COUNTER_SIZE; //skip that byte
                //Notice that we do not update the free space, since this byte is not available for writing!
            }
            done = check_destructive_read_pointer(handle);
        }
        if (!done)
        {
            //check to see if the obstruction is leftovers at end of page
            uint8_t check = 0;
            flash_read(handle->start + handle->destructive_read_offset, &check, 1);
            if (check == 0xFF) //leftovers at end of page
            {
                handle->free_space += FLASH_PAGE_SIZE - (handle->destructive_read_offset % FLASH_PAGE_SIZE);
                handle->destructive_read_offset += FLASH_PAGE_SIZE - (handle->destructive_read_offset % FLASH_PAGE_SIZE);
                //check for wrap-around
                if (handle->destructive_read_offset >= FILE_SIZE)
                    handle->destructive_read_offset = 0;
            }
            //skip first byte of each page
            if (!(handle->destructive_read_offset % FLASH_PAGE_SIZE))
            {
                handle->destructive_read_offset += PAGE_COUNTER_SIZE; //skip that byte
                //Notice that we do not update the free space, since this byte is not available for writing!
            }
            done = check_destructive_read_pointer(handle);
        }
    }
}

// Delete the first n bytes of file, move file handles to point to same data
// In case of unexpected power down, the state of the flash must at all times
// reflect either the unchanged file, or the file with all N bytes deleted.

size_t
file_consume(file_handle_t * handle, size_t size)
{
    size_t i = 0;
    uint8_t valid;
    while (size)
    {
        //if we have reached the read pointer, stop, do nothing.
        if (handle->destructive_read_offset == handle->raw_read_chunk_start)
            return i;

        //get the current chunk size.
        uint8_t chunk_size = 0;
        flash_read(handle->start + handle->destructive_read_offset, &chunk_size, 1);

        //is the current chunk smaller than what was requested? If so, we will be moving to the next chunk.
        if (chunk_size > size) //current chunk is smaller than read size, leave it be and stop here
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
        if (!((handle->destructive_read_offset - PAGE_COUNTER_SIZE) % FLASH_PAGE_SIZE))
        {
            //get the start address for the previous page, and erase it.
            uint32_t page_start;
            if (handle->destructive_read_offset >= FLASH_PAGE_SIZE) //if on second or subsequent pages
                page_start = handle->destructive_read_offset - FLASH_PAGE_SIZE - PAGE_COUNTER_SIZE;
            else //we just wrapped onto the first page, previous page is at the bottom
                page_start = FILE_SIZE - FLASH_PAGE_SIZE;
            //need to check if page needs erasing. We check if the first chunk is flagged consumed.
            //If so, we check that neither the read nor write pointers are on the page
            uint8_t test;
            flash_read(handle->start + page_start + 1 + PAGE_COUNTER_SIZE, &test, 1); //if the first value we read is 0xFF, no need to erase.
            if (test == 0xFC) //the first chunk has been consumed. Rather than test all chunks, just see if the read and write pointers made it off the page
            {
                //here is where we test the pointer location to avoid erasing
                //a page currently in use
                if ((handle->write_offset <= page_start || handle->write_offset >= (page_start + FLASH_PAGE_SIZE)) //yes, <=, because the write pointer might be lingering around the first byte of the page, waiting for us to erase this page.
                        && (handle->raw_read_chunk_start < page_start || handle->raw_read_chunk_start >= (page_start + FLASH_PAGE_SIZE)))
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
file_size(file_handle_t * handle)
{
    return used_space(handle);
}

// Commit all changes to flash.
// If unexpected power down, flash state will reflect entire pending writes in order,
// or no change.
// When this function returns, flash state must reflect all pending writes
//Nothing to do here, as we do not perform lazy writes

void
file_sync(file_handle_t * handle)
{

}

// Returns the number of bytes actually read

size_t
file_read(file_handle_t * handle, uint8_t* data, size_t size)
{
    size_t i = 0;
    while (size)
    {
        //make sure we are not bumping into write pointer!
        if (handle->raw_read_chunk_start == handle->write_offset)
        {
            //it might be that the write pointer is actually BEHIND us. We can test by looking
            //ahead to see if the current chunk is free or written
            uint8_t size = 0;
            flash_read(handle->start + handle->raw_read_chunk_start, &size, 1);
            if (size == 0xFF) //the write pointer is ahead of us, ditch
                return i;
            //else the write pointer is actually behind us, and we can proceed
        }

        //read in the current chunk size, so we can calculate where the next chunk begins
        uint8_t remaining_chunk_size;
        uint8_t chunk_size;
        flash_read(handle->start + handle->raw_read_chunk_start, &chunk_size, 1);
        remaining_chunk_size = chunk_size - handle->raw_read_chunk_offset;

        //is the current chunk smaller than what we need? If so, we will be moving to the next chunk.
        if (remaining_chunk_size > size) //chunk is smaller, we will only read what we need
        {
            uint8_t read_amount = flash_read(handle->start + handle->raw_read_chunk_start + 2 + handle->raw_read_chunk_offset, (void*) (data + i), size);
            size -= read_amount;
            i += read_amount;

            if (read_amount >= remaining_chunk_size) //we have read the entire chunk, set pointers to beginning of next chunk
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
            uint8_t read_amount = flash_read(handle->start + handle->raw_read_chunk_start + 2 + handle->raw_read_chunk_offset, (void*) (data + i), remaining_chunk_size);
            size -= read_amount;
            i += read_amount;
            //move to next chunk
            if (read_amount == chunk_size)
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
//NOT IMPLEMENTED because this does not make sense for a FIFO. I could be
// convinced otherwise.

void
file_seek(file_handle_t * handle, uint32_t offset, int whence)
{

}

static void advance_write_pointer_to_next_page(file_handle_t *handle)
{
    uint32_t remaining = ((FLASH_PAGE_SIZE - handle->write_offset) % FLASH_PAGE_SIZE);
    handle->write_offset += remaining;
    handle->free_space -= remaining;
    //moved into a new page. see if this page is free, and if so mark it and move forward
    //otherwise hang around and wait for page to erase
    if (handle->write_offset > FILE_SIZE)
        handle->write_offset = 0; //wrap around

    uint8_t counter;
    flash_read(handle->start + handle->write_offset, &counter, 1);
    if (counter == 0xFF) //we can move in
    {
        counter = (0xFF << handle->write_count);
        ++handle->write_count;
        if (handle->write_count == 9) handle->write_count = 1;
        flash_write(handle->start + handle->write_offset, &counter, PAGE_COUNTER_SIZE);
        handle->write_offset += PAGE_COUNTER_SIZE; //skip the counter bytes
    }
}

static void advance_write_pointer(file_handle_t *handle)
{
    //first, read size and move ahead
    uint8_t size = 0;
    flash_read(handle->start + handle->write_offset, &size, 1);
    handle->write_offset += size + 2;
    handle->free_space -= size + 2;

    if (handle->write_offset >= FILE_SIZE)
        handle->write_offset = 0;

    //now, we need to check if we just moved onto a new page.
    if (!(handle->write_offset % FLASH_PAGE_SIZE))
    {
        //we did move into a new page. see if this page is free, and if so mark it and move forward
        //otherwise hang around and wait for page to erase
        uint8_t counter;
        flash_read(handle->start + handle->write_offset, &counter, 1);
        if (counter == 0xFF) //we can move in
        {
            counter = (0xFF << handle->write_count);
            ++handle->write_count;
            if (handle->write_count == 9) handle->write_count = 1;
            flash_write(handle->start + handle->write_offset, &counter, PAGE_COUNTER_SIZE);
            handle->write_offset += PAGE_COUNTER_SIZE; //skip the counter bytes
        }
    }
}

// Returns the number of bytes written
// File can fill when the number of destructive reads < number of writes

size_t
file_write(file_handle_t *handle, uint8_t* data, size_t size)
{

    if (!(handle->write_offset % FLASH_PAGE_SIZE)) //we are hanging around at the beginning of a page.
        //We do so because we are waiting for the page to erase. Check to see if it is ready for us
    {
        uint8_t counter = 0;
        flash_read(handle->start + handle->write_offset, &counter, 1);
        if (counter != 0xFF) //still waiting!
            return 0;

        //if we get here it is because the page has since been erased, and we can proceed
        counter = (0xFF << handle->write_count);
        ++handle->write_count;
        if (handle->write_count == 9) handle->write_count = 1;
        flash_write(handle->start + handle->write_offset, &counter, PAGE_COUNTER_SIZE);
        handle->write_offset += PAGE_COUNTER_SIZE;
    }

    if (size >= 0xFF) //reject, because this value is used as a flag for unused memory!
        //Also because we can only write one byte at a time atomically, we limit all metadata writes, including the record of the number of bytes written, to one byte.
        return 0;

    if ((size + 2 + PAGE_COUNTER_SIZE) > FLASH_PAGE_SIZE) //reject, because we cannot write chunks larger than the page size
        return 0;

    if ((size + 2) > free_space(handle)) //reject if not enough available space
        return 0;

    //here we need to find where we can put this chunk. Because writes cannot span page boundaries, we need to see if there is space on the current page or not
    //first, identify where the next page boundary is
    uint32_t next_page = FLASH_PAGE_SIZE * (handle->write_offset / FLASH_PAGE_SIZE) + FLASH_PAGE_SIZE;
    //uint32_t page_free_space = next_page - start;

    if ((handle->write_offset + size + 2) > next_page) //if not enough room in current page for metadata + data
    {
        advance_write_pointer_to_next_page(handle);
        //check if we can still write from where we are
        if (!(handle->write_offset % FLASH_PAGE_SIZE)) //if we are stuck in limbo
            return 0;
    }

    //need to recheck.
    if ((size + 2) > free_space(handle)) //reject if not enough available space
        return 0;

    //First, write first bit of metadata containing the actual addresses we are attempting to write to
    flash_write(handle->start + handle->write_offset, &size, 1);

    //Now, attempt to commit the data itself
    flash_write(handle->start + handle->write_offset + 2, data, size);

    //If we reach here successfully, the data is written and valid. Mark it so in the metadata
    uint8_t flags = 0xFE;
    flash_write(handle->start + handle->write_offset + 1, &flags, 1);

    advance_write_pointer(handle);

    return size;
}
