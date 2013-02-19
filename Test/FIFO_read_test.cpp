/************************************
 FIFO_read_test.cpp
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
 * This file implements a set of unit tests for checking the read behavior of the
 * FIFO functions.
 *
 * Things being checked, at a general level include: reading across chunks,
 * skipping invalid chunks, consuming across chunks (a weird case in its own right)
 * properly erasing pages once fully consumed, that reads never read more than is
 * available
************************************/


#include <CppUTest/TestHarness.h>
#include "FIFO.h"
#include "flash_port.h"

extern "C"
{
void flash_force_fail(uint8_t count);
void flash_force_succeed(void);
}

static file_handle_t * f;
extern uint8_t store[];

TEST_GROUP(BasicFileReadTest)
{
    void setup()
    {
        //each test begins by init'ing the system, and opening a file
        flash_init();
        f = file_open( FILE_ROOT_BLOCK );

        //we also write four bytes of data to the file
        uint8_t data[] = {1, 2, 3, 4};
        file_write(f, data, 4);
    }

    void teardown()
    {
        file_close(f);
    }
};


//This test just makes sure the setup routine above is working as expected
TEST(BasicFileReadTest, CheckInit)
{
    CHECK_EQUAL(1, store[FILE_OFFSET+2]);
    CHECK_EQUAL(2, store[FILE_OFFSET+3]);
    CHECK_EQUAL(3, store[FILE_OFFSET+4]);
    CHECK_EQUAL(4, store[FILE_OFFSET+5]);
    CHECK_EQUAL(0xFF, store[FILE_OFFSET+6]);
}

//test the file_read function to see if it can read the four bytes placed there by setup
TEST(BasicFileReadTest, TestFileRead)
{
    uint8_t i = 0;
    uint8_t data[4] = {0, 0, 0, 0};
    i = file_read( f, data, 4 );
    CHECK_EQUAL(4, i);
    CHECK_EQUAL(1, data[0]);
    CHECK_EQUAL(2, data[1]);
    CHECK_EQUAL(3, data[2]);
    CHECK_EQUAL(4, data[3]);
}

//check that the file handle is being updated correctly by file_read
//having read an entire chunk, the handle should be pointing to the next chunk in line
TEST(BasicFileReadTest, TestFileReadPtrIncr)
{
    uint8_t i = 0;
    uint8_t data[4] = {0, 0, 0, 0};
    i = file_read( f, data, 4 );
    CHECK_EQUAL(6, f->raw_read_chunk_start); //address of the start of the next chunk
    CHECK_EQUAL(0, f->raw_read_chunk_offset); //offset within chunk
}

//check that we can read in sub-chunk intervals by reading 3 bytes from the store
TEST(BasicFileReadTest, TestFileReadLessThanFullChunk)
{
    uint8_t i = 0;
    uint8_t data[4] = {0, 0, 0, 0};
    i = file_read( f, data, 3 );
    CHECK_EQUAL(3, i);
    CHECK_EQUAL(1, data[0]);
    CHECK_EQUAL(2, data[1]);
    CHECK_EQUAL(3, data[2]);
    CHECK_EQUAL(0, data[3]); //should not be read out!
    CHECK_EQUAL(3, f->raw_read_chunk_offset);
}

//check that we can read across chunk boundaries by writing a second chunk, then reading from both in one call
TEST(BasicFileReadTest, TestFileReadAcrossMultipleChunks)
{
    uint8_t w_data[4] = {5, 6, 7, 8}; //write a second chunk to memory
    file_write(f, w_data, 4);


    uint8_t i = 0;
    uint8_t data[6] = {0, 0, 0, 0, 0, 0};
    i = file_read( f, data, 6 ); //this will read 4 bytes from first chunk, and 2 from second chunk
    CHECK_EQUAL(6, i);
    CHECK_EQUAL(1, data[0]);
    CHECK_EQUAL(2, data[1]);
    CHECK_EQUAL(3, data[2]);
    CHECK_EQUAL(4, data[3]);
    CHECK_EQUAL(5, data[4]);
    CHECK_EQUAL(6, data[5]);
    CHECK_EQUAL(6, f->raw_read_chunk_start);
    CHECK_EQUAL(2, f->raw_read_chunk_offset);
}

//check that we can read across chunk boundaries when there is an invalid chunk in the middle of the read
TEST(BasicFileReadTest, TestFileReadAcrossMultipleChunksWithInvalidData)
{
    flash_force_fail(1);
    uint8_t a_data[4] = {5, 6, 7, 8}; //write a second chunk to memory. This write will FAIL
    file_write(f, a_data, 4);
    flash_force_succeed();
    uint8_t b_data[4] = {9, 10, 11, 12}; //write a third chunk to memory. This write will SUCCEED
    file_write(f, b_data, 4);


    uint8_t i = 0;
    uint8_t data[6] = {0, 0, 0, 0, 0, 0};
    i = file_read( f, data, 6 ); //this will read 4 bytes from first chunk, and 2 from third chunk, skipping invalid second chunk
    CHECK_EQUAL(6, i);
    CHECK_EQUAL(1, data[0]);
    CHECK_EQUAL(2, data[1]);
    CHECK_EQUAL(3, data[2]);
    CHECK_EQUAL(4, data[3]);
    CHECK_EQUAL(9, data[4]);
    CHECK_EQUAL(10, data[5]);
    CHECK_EQUAL(2, f->raw_read_chunk_offset);
}

//check that, having read four bytes (exactly one chunk in this case) that is is consumed properly
TEST(BasicFileReadTest, TestFileConsumeBasic)
{
    uint8_t i = 0;
    uint8_t data[4] = {0, 0, 0, 0};
    i = file_read(f, data, 4);
    file_consume(f, 4);
    CHECK_EQUAL(0xFC, store[1]);
    CHECK_EQUAL(6, f->raw_read_chunk_start);
    CHECK_EQUAL(0, f->raw_read_chunk_offset);
    CHECK_EQUAL(6, f->destructive_read_offset);
}

//make sure that if we consume only a part of one chunk, we do not actually consume it
TEST(BasicFileReadTest, TestFileConsumePartialChunks1)
{
    uint8_t data[4];
    file_read(f, data, 4); //read the chunk to advance the read pointer
    file_consume(f, 2); //consume part of one chunk; should refuse to consume
    CHECK_EQUAL(0xFE, store[1]);
    CHECK_EQUAL(6, f->raw_read_chunk_start);
    CHECK_EQUAL(0, f->raw_read_chunk_offset);
    CHECK_EQUAL(0, f->destructive_read_offset); //make sure the destructive read offset doesn't advance!
}


TEST(BasicFileReadTest, TestFileConsumePartialChunks2)
{
    uint8_t data[4] = {0, 0, 0, 0};
    uint8_t data_more[8];
    file_write(f, data, 4);
    file_read(f, data_more, 8); //read both chunks to advance the read pointer
    file_consume(f, 6); //consume one chunk, and consider the next part. Second chunk should not be consumed
    CHECK_EQUAL(0xFE, store[7]);
    CHECK_EQUAL(12, f->raw_read_chunk_start);
    CHECK_EQUAL(0, f->raw_read_chunk_offset);
    CHECK_EQUAL(6, f->destructive_read_offset); //make sure the destructive read offset doesn't advance beyond first chunk
}

//sometimes writes leave some blank space at the end of a page. Make sure we skip that when reading!
TEST(BasicFileReadTest, TestFileReadEndOfIncompletePage)
{
    //we laready have 4 bytes on page one. Let's write a full page, which will begin on page 2. Then see if doing a read catches up properly
    uint8_t size = FLASH_PAGE_SIZE - 2;
    uint8_t data[FLASH_PAGE_SIZE - 2] = {0};

    file_write(f, data, size); //won't fit on page 1, moves ahead to page 2
    file_read(f, data, 4); //read first chunk, read point should advance past dead space to second page.

    CHECK_EQUAL(128, f->raw_read_chunk_start);
}

//Now test the wrap-around functionality.
//A read that begins near the end of the file should wrap around to the beginning, if the write pointer
//is past the beginning.
TEST(BasicFileReadTest, TestReadWrapsAroundEnd)
{
    //first, let's write three pages, filling the file. We will then consume one page, and write one page to cause wrap around.
    //This test presumes that write wrap-around works, and that page-erase works
    uint8_t size = FLASH_PAGE_SIZE-2;
    uint8_t data[FLASH_PAGE_SIZE-2] = {0};

    file_write(f, data, size); //this will actually go onto the second page, because it is too large to fit on first page with the 4 bytes already there.
    file_write(f, data, size); //this will go to third page
    //verify that we have three pages of data
    CHECK_EQUAL(4, store[0]);
    CHECK_EQUAL(size, store[128]);
    CHECK_EQUAL(size, store[256]);

    //consume the data on the first page
    file_read(f, data, 4); //moves read pointer to beginning of second page, 128
    file_consume(f, 4);

    //and write another page
    file_write(f, data, size);

    //now, the write pointer is at the beginning of the second page, as is the read pointer. advance the read pointer all the way around
    uint8_t read = file_read(f, data, size); //moves pointer to beginning of third page, 256
    CHECK_EQUAL(size, read);
    CHECK_EQUAL(256, f->raw_read_chunk_start);
    read = file_read(f, data, size); //moves pointer to wrap around to first page, 0
    CHECK_EQUAL(size, read);
    CHECK_EQUAL(0, f->raw_read_chunk_start);
}


//check that a destructive read greater than the amount currently read doesn't clobber unread data
TEST(BasicFileReadTest, TestFileConsumeBeyondReadPointer1)
{
    file_consume(f, 4);
    CHECK_EQUAL(0xFE, store[1]);
    CHECK_EQUAL(0, f->raw_read_chunk_start);
    CHECK_EQUAL(0, f->raw_read_chunk_offset);
    CHECK_EQUAL(0, f->destructive_read_offset); //make sure the destructive read offset doesn't advance!
}

//Repeat test with a little more data
TEST(BasicFileReadTest, TestFileConsumeBeyondReadPointer2)
{
    uint8_t data[4] = {0};
    uint8_t data_more[6];
    file_write(f, data, 4); //write a second chunk
    file_read(f, data_more, 6); //read all of chunk one, and part of chunk 2
    uint8_t consumed = file_consume(f, 8); //ask to consume two chunks; only first should be consumed
    CHECK_EQUAL(consumed, 4);
    CHECK_EQUAL(0xFC, store[1]); //check first chunk marked as consumed
    CHECK_EQUAL(0xFE, store[7]); //check second chunk NOT marked as consumed
    CHECK_EQUAL(6, f->raw_read_chunk_start);
    CHECK_EQUAL(2, f->raw_read_chunk_offset);
    CHECK_EQUAL(6, f->destructive_read_offset); //make sure the destructive read offset doesn't advance!
}

//check that a read operation does not extend beyond the write pointer
//This should be ensured by the mechanisms that ensure a) read does not exceed write and b) destructive read does not exceed read
TEST(BasicFileReadTest, TestFileConsumeBeyondWritePointer)
{
    //setup writes some data, so write pointer is already at 6.
    //the result is that this is going to look a lot like code above
    uint8_t data[8];
    uint8_t read, consumed;
    read = file_read(f, data, 8); //try to read beyond the write pointer
    consumed = file_consume(f, 8); //and then consume those same data

    CHECK_EQUAL(4, read); //make sure only 4 bytes actually read
    CHECK_EQUAL(4, consumed); //make sure only 4 bytes consumed
    CHECK_EQUAL(0xFF, store[6]); //make sure store beyond write pointer was not touched
}

//check that a read operation wraps around the last page correctly

//check that a destructive read that completes a page wipes the page
TEST(BasicFileReadTest, TestPageConsumptionErasesPage)
{
    uint8_t data[4] = {0};
    //first of all, let's get a couple of page's worth of data in there.
    uint8_t written = 4; //need to account for data already written in setup
    uint8_t chunks = 1;
    while(f->write_offset < FLASH_PAGE_SIZE)
    {
        written += file_write(f, data, 4);
        ++chunks;
    }

    //now, let's read then consume almost two pages worth.
    // NOTICE that the amount of data in one page is AT LEAST 2 bytes less than total page size, because of metadata
    //so we have to rely on the number of chunks in previous bit of code to read out EXACTLY one page
    while(chunks >= 1)
    {
        file_read(f, data, 4);
        file_consume(f, 4);
        --chunks;
    }
    //make sure that first page got erased, but second has not
    CHECK_EQUAL(0xFF, store[0]); //0xFF means it was erased
    CHECK_EQUAL(0x04, store[128]); //first byte of second page
}

//check that a destructive read that completes a page wipes the page
//extension to check when two pages have been consumed.
TEST(BasicFileReadTest, TestPageConsumptionErasesTwoPages)
{
    uint8_t data[4] = {0};
    //first of all, let's get a couple of page's worth of data in there.
    uint8_t written = 4; //need to account for data already written in setup
    uint8_t chunks = 1;
    while(f->write_offset < 2*FLASH_PAGE_SIZE)
    {
        written += file_write(f, data, 4);
        ++chunks;
    }

    //now, let's read then consume almost two pages worth.
    // NOTICE that the amount of data in one page is AT LEAST 2 bytes less than total page size, because of metadata
    //so we have to rely on the number of chunks in previous bit of code to read out EXACTLY one page
    while(chunks >= 1)
    {
        file_read(f, data, 4);
        file_consume(f, 4);
        --chunks;
    }
    //make sure that first and second page got erased, but third has not
    CHECK_EQUAL(0xFF, store[0]); //0xFF means it was erased
    CHECK_EQUAL(0xFF, store[128]); //first byte of second page
    CHECK_EQUAL(0x04, store[256]); //third page should be intact
}

//Need check for power failure during page erasure, to make sure we can recover properly from that.
//will need to write this test after we have code for recovering file handles post power-loss.


//CORNER CASES that are not being tested currently, but that need to be

//Reads that wrap around from the end of the physical memory space to the beginning
//  Since this is being treated as a FIFO, it is possible that a valid read will
//  take us beyond the end of the file. In this case, we need to (possibly skip dead
//  space since this will occur at the end of a page) wrap the read pointer back to the beginning
//  and continue from there

//Reads that are too big! I.e. reads that take us right up to the write pointer. This
//  is an easy case, just not implemented yet.

//And of course, none of the Consume functionality is implemented and very little is tested

//Part of the consume functionality includes erasing a page when we are done with it.
//  What will happen is this: We consume the last chunk on a page; we observe that we are not writing
//  to the page (this should be implicit, as we cannot begin writing to it again until it is erased!)
//  so we erase the page. Page erasue will likely need some flags for testing when a page erase
//  cycle is interrupted by a loss of power. These flags, in turn, will need to be skipped over
//  by normal reads and writes!