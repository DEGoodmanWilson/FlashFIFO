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
TEST(BasicFileReadTest, TestFileReadPtrIncr)
{
    uint8_t i = 0;
    uint8_t data[4] = {0, 0, 0, 0};
    i = file_read( f, data, 4 );
    CHECK_EQUAL(4, f->read_offset);
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
    CHECK_EQUAL(3, f->read_offset);
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
    CHECK_EQUAL(6, f->read_offset);
}

//check that we can read across chunk boundaries when there is an invalid chunk in the middle of the read
TEST(BasicFileReadTest, TestFileReadAcrossMultipleChunksWithInvalidData)
{
    flash_force_fail(1);
    uint8_t a_data[4] = {5, 6, 7, 8}; //write a second chunk to memory. This write will FAIL
    file_write(f, a_data, 4);
    flash_force_succeed();
    uint8_t b_data[4] = {9, 10, 11, 12}; //write a second chunk to memory. This write will SUCCEED
    file_write(f, b_data, 4);


    uint8_t i = 0;
    uint8_t data[6] = {0, 0, 0, 0, 0, 0};
    i = file_read( f, data, 6 ); //this will read 4 bytes from first chunk, and 2 from second chunk
    CHECK_EQUAL(6, i);
    CHECK_EQUAL(1, data[0]);
    CHECK_EQUAL(2, data[1]);
    CHECK_EQUAL(3, data[2]);
    CHECK_EQUAL(4, data[3]);
    CHECK_EQUAL(9, data[4]);
    CHECK_EQUAL(10, data[5]);
    CHECK_EQUAL(6, f->read_offset);
}

//check that, having read four bytes (exactly one chunk in this case) that is is consumed properly
IGNORE_TEST(BasicFileReadTest, TestFileConsumeBasic)
{
    uint8_t i = 0;
    uint8_t data[4] = {0, 0, 0, 0};
    i = file_read(f, data, 4);
    file_consume(f, 4);
    CHECK_EQUAL(4, f->read_offset); //TODO These tests are not the correct tests to perform!
    CHECK_EQUAL(4, f->destructive_read_offset);
}

//CORNER CASES that are not being tested currently, but that need to be

//Reads at the end of a page. Writes will be page-aligned, so there will probably be
//  some dead space at the end of a page. This space needs to be detected and skipped

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