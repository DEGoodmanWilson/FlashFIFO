/************************************
 FIFO_write_test.cpp
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

 This file implements a set of unit tests for checking the write behavior of the
 FIFO functions.

 TODO things being checked, at a general level include:

************************************/

#include <CppUTest/TestHarness.h>
#include "configure.h"
#include "FIFO.h"
#include "flash_port.h"

extern "C"
{
void flash_force_fail(uint8_t fail_after);
void flash_force_succeed(void);
}


static file_handle_t * f;
extern uint8_t store[];

TEST_GROUP(BasicFileWriteTest)
{
    void setup()
    {
        flash_init();
        f = file_open( FILE_ROOT_BLOCK );
    }

    void teardown()
    {

    }
};

TEST(BasicFileWriteTest, FileCreate)
{
    file_handle_t * f = NULL;
    f = file_open( FILE_ROOT_BLOCK );
    CHECK(f);
}

TEST(BasicFileWriteTest, BlankStore)
{
    CHECK_EQUAL(0xFF, store[0]); //just do a spot check that is fine
    CHECK_EQUAL(0xFF, store[10]);
}

TEST(BasicFileWriteTest, RawWrite)
{
    uint8_t raw[] = {1, 2, 3, 4};
    flash_write(10, (void *)raw, 4);
    CHECK_EQUAL(raw[0], store[10]); //just do a spot check that is fine
    CHECK_EQUAL(raw[1], store[11]);
    CHECK_EQUAL(raw[2], store[12]);
    CHECK_EQUAL(raw[3], store[13]);
}

TEST(BasicFileWriteTest, BasicWriteFirstChunk)
{
    uint8_t data[] = {1, 2, 3, 4};
    file_write(f, data, 4);
    CHECK_EQUAL(1, store[FILE_OFFSET+5]); //skip past metadata.
    CHECK_EQUAL(2, store[FILE_OFFSET+6]);
    CHECK_EQUAL(3, store[FILE_OFFSET+7]);
    CHECK_EQUAL(4, store[FILE_OFFSET+8]);
}

TEST(BasicFileWriteTest, BasicWriteFirstChunk_CheckMetadata)
{
    uint8_t size = 4;
    uint8_t data[] = {1, 2, 3, 4};
    file_write(f, data, 4);
    CHECK_EQUAL(4, *(uint32_t*)(store+FILE_OFFSET)); //check metadata.
    CHECK_EQUAL(0xFE, *(uint8_t*)(store+FILE_OFFSET+4)); //check metadata.
}

TEST(BasicFileWriteTest, TestWriteTwoChunksValidly)
{
    uint8_t size = 4;
    uint8_t data[] = {1, 2, 3, 4};
    file_write(f, data, 4);
    CHECK_EQUAL(1, store[FILE_OFFSET+5]); //skip past metadata.
    CHECK_EQUAL(2, store[FILE_OFFSET+6]);
    CHECK_EQUAL(3, store[FILE_OFFSET+7]);
    CHECK_EQUAL(4, store[FILE_OFFSET+8]);
    CHECK_EQUAL(size, *(uint32_t*)(store+FILE_OFFSET));
    CHECK_EQUAL(0xFE, *(uint8_t*)(store+FILE_OFFSET+4));

    file_write(f, data, 4); //write it a second time, creating a second record
    CHECK_EQUAL(1, store[FILE_OFFSET+14]);
    CHECK_EQUAL(2, store[FILE_OFFSET+15]);
    CHECK_EQUAL(3, store[FILE_OFFSET+16]);
    CHECK_EQUAL(4, store[FILE_OFFSET+17]);
    CHECK_EQUAL(size, *(uint32_t*)(store+FILE_OFFSET+9));
    CHECK_EQUAL(0xFE, *(uint8_t*)(store+FILE_OFFSET+13));
}

TEST(BasicFileWriteTest, TestWritePowerOff)
{
    uint8_t size = 14;
    uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

    flash_force_fail(1);

    file_write(f, data, size);
    CHECK_EQUAL(size, *(uint32_t*)(store+FILE_OFFSET));
    CHECK_EQUAL(0xFF, *(uint8_t*)(store+FILE_OFFSET+4));
}

IGNORE_TEST(BasicFileWriteTest, DeleteOldPageOnWrite)
{
    uint8_t size = FLASH_PAGE_SIZE;
    uint8_t data[FLASH_PAGE_SIZE] = {0};
    file_write(f, data, size); //fill up one page

    file_read(f, data, size); //read it back out
    file_consume(f, size); //and let it know we are DONE with that data.

    file_write(f, data, size); //write out one more page.
    //at this point, we should recycle the previous page, as we are DONE with it.

    CHECK_EQUAL(0xFF, store[0]);
    CHECK_EQUAL(0xFF, store[FLASH_PAGE_SIZE-1]);
}


//CORNER CASES we haven't gotten to yet.

// Writes that takeus beyond the available space, i.e. that catch up to the read pointer

// Writes that need to wrap around the end of the physical memory address space