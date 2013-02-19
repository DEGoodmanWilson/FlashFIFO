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
 *
 * This file implements a set of unit tests for checking the write behavior of the
 * FIFO functions.
 *
 * TODO things being checked, at a general level include:
 *
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

#define METADATA_SIZE   2
#define DATA_VALID      0xFE
#define DATA_CONSUMED   0xFC
#define DATA_INVALID    0xFF

static file_handle_t * f;
extern uint8_t store[];
enum FILE_ID filename = FILE_FIRMWARE;

TEST_GROUP(BasicFileWriteTest)
{
    //for each test, init the flash, and open a file at address 0
    void setup()
    {
        flash_init(); //initialize flash
        f = file_open( filename );
    }

    void teardown()
    {
        file_close(f); //close the open file
    }
};

//first, check that file_open returns a non-null file handle.
TEST(BasicFileWriteTest, FileCreate)
{
    file_handle_t * f = NULL;
    f = file_open( FILE_CRASH_LOG );
    CHECK(f); //checks for non-NULL value
}

//now, given that FILE_ROOT_BLOCK is already open, make sure we cannot open
//it a second time
TEST(BasicFileWriteTest, FileCreateDuplicate)
{
    file_handle_t * f = NULL;
    f = file_open( filename );
    CHECK_EQUAL(NULL, f);
}

//Check the setup routine to make sure that we get a fresh, blank flash drive
//Just spot check a couple locations in flash
TEST(BasicFileWriteTest, BlankStore)
{
    CHECK_EQUAL(0xFF, store[f->start+0]); //just do a spot check that is fine
    CHECK_EQUAL(0xFF, store[f->start+10]);
}

//Check the low-level flash write routines to ensure they do as we would expect
TEST(BasicFileWriteTest, RawWrite)
{
    uint8_t raw[] = {1, 2, 3, 4};
    flash_write(10, (void *)raw, 4);
    CHECK_EQUAL(raw[0], store[10]);
    CHECK_EQUAL(raw[1], store[11]);
    CHECK_EQUAL(raw[2], store[12]);
    CHECK_EQUAL(raw[3], store[13]);
}

//Write a chunk to flash with file_write, make sure the data gets written properly
TEST(BasicFileWriteTest, BasicWriteFirstChunk)
{
    uint8_t data[] = {1, 2, 3, 4};
    file_write(f, data, 4);
    CHECK_EQUAL(1, store[f->start+METADATA_SIZE]); //skip past metadata.
    CHECK_EQUAL(2, store[f->start+METADATA_SIZE + 1]);
    CHECK_EQUAL(3, store[f->start+METADATA_SIZE + 2]);
    CHECK_EQUAL(4, store[f->start+METADATA_SIZE + 3]);
}

//Write a chunk to flash with file_write, make sure the metadata gets written properly
TEST(BasicFileWriteTest, BasicWriteFirstChunk_CheckMetadata)
{
    uint8_t size = 4;
    uint8_t data[] = {1, 2, 3, 4};
    file_write(f, data, 4);
    CHECK_EQUAL(4, store[f->start]); //check metadata.
    CHECK_EQUAL(DATA_VALID, store[f->start+1]); //check metadata.
}

//Write two chunks in a row, check metadata integrity
TEST(BasicFileWriteTest, TestWriteTwoChunksValidly)
{
    uint8_t size = 4;
    uint8_t data[] = {1, 2, 3, 4};
    file_write(f, data, 4);
    CHECK_EQUAL(1, store[f->start+METADATA_SIZE]); //skip past metadata.
    CHECK_EQUAL(2, store[f->start+METADATA_SIZE + 1]);
    CHECK_EQUAL(3, store[f->start+METADATA_SIZE + 2]);
    CHECK_EQUAL(4, store[f->start+METADATA_SIZE + 3]);
    CHECK_EQUAL(4, store[f->start]); //check metadata.
    CHECK_EQUAL(DATA_VALID, store[f->start+1]); //check metadata.
    uint32_t new_offset = METADATA_SIZE+4;

    file_write(f, data, 4); //write it a second time, creating a second record
    CHECK_EQUAL(1, store[f->start+new_offset+METADATA_SIZE]);
    CHECK_EQUAL(2, store[f->start+new_offset+METADATA_SIZE + 1]);
    CHECK_EQUAL(3, store[f->start+new_offset+METADATA_SIZE + 2]);
    CHECK_EQUAL(4, store[f->start+new_offset+METADATA_SIZE + 3]);
    CHECK_EQUAL(4, store[f->start+new_offset]); //check metadata.
    CHECK_EQUAL(DATA_VALID, store[f->start+new_offset+1]); //check metadata.
}

//Now, simulate a power off event while writing data. Check that the metadata is only
//partially written, and in particular, that the "valid data" byte==DATA_INVALID
TEST(BasicFileWriteTest, TestWritePowerOff)
{
    uint8_t size = 14;
    uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

    flash_force_fail(1);

    file_write(f, data, size);
    CHECK_EQUAL(14, store[f->start]); //check metadata.
    CHECK_EQUAL(DATA_INVALID, store[f->start+1]); //check metadata.
}

//test writes of size 255 should fail. The should fail because 0xFF in the size location of metadata is a flag that there is no chunk from this point forward.
TEST(BasicFileWriteTest, TestWriteTooBig1)
{
    uint8_t size = 255;
    uint8_t data[255] = {0};
    uint32_t prev_write_loc = f->write_offset;
    size = file_write(f, data, size);

    CHECK_EQUAL(0x00, size); //make sure no data was reported as written
    CHECK_EQUAL(prev_write_loc, f->write_offset); //make sure no data was recorded as written
    CHECK_EQUAL(0xFF, store[f->start+METADATA_SIZE]); //make sure no data was actually written
}

//test to make sure that we cannot write a chunk /larger/ than 255, because all metadata is one byte in size to ensure all metadata writes are atomic.
//Just want to be sure that this case is handled correctly, since 256 as a uint8_t is actually 1!
TEST(BasicFileWriteTest, TestWriteTooBig2)
{
    uint16_t size = 0x00FF + 1;
    uint8_t data[0x00FF + 1] = {0};

    uint32_t prev_write_loc = f->write_offset;
    size = file_write(f, data, size);

    CHECK_EQUAL(0x00, size); //make sure no data was reported as written
    CHECK_EQUAL(prev_write_loc, f->write_offset); //make sure no data was recorded as written
    CHECK_EQUAL(0xFF, store[f->start+METADATA_SIZE]); //make sure no data was actually written
}

//All writes should be page-aligned, by which I mean a write must not span a
//page boundary. Test that this is so
TEST(BasicFileWriteTest, TestWriteAcrossPageBoundary1)
{
    //strategy: Write a single byte of data, then a full page's worth.
    //The full page should start on a new page!
    uint8_t i = 1;
    file_write(f, &i, 1); //write one byte

    uint8_t size = FLASH_PAGE_SIZE-METADATA_SIZE;
    uint8_t data[FLASH_PAGE_SIZE-METADATA_SIZE] = {0};

    file_write(f, data, FLASH_PAGE_SIZE-METADATA_SIZE);

    //now, check both the store and the file handle
    CHECK_EQUAL(0xFF, store[f->start+3]); //make sure nothing got written immediately after the one byte write
    CHECK_EQUAL(FLASH_PAGE_SIZE-METADATA_SIZE, store[f->start+FLASH_PAGE_SIZE]); //check first byte of second page, make sure it contains the proper size
    CHECK_EQUAL(0x00, store[f->start+FLASH_PAGE_SIZE + METADATA_SIZE]); //check first byte of written data to see that it was written.
    CHECK_EQUAL(FLASH_PAGE_SIZE*2, f->write_offset); //check the file handle to see that the next write offset is in the correct place.
}

//Any write that is larger than the page size must fail, full stop
TEST(BasicFileWriteTest, TestWriteAcrossPageBoundary2)
{
    uint8_t size = FLASH_PAGE_SIZE-METADATA_SIZE + 1;
    uint8_t data[FLASH_PAGE_SIZE-METADATA_SIZE + 1] = {0};

    uint32_t prev_write_loc = f->write_offset;
    size = file_write(f, data, FLASH_PAGE_SIZE-METADATA_SIZE + 1);

    CHECK_EQUAL(0x00, size); //make sure no data was reported as written
    CHECK_EQUAL(prev_write_loc, f->write_offset); //make sure no data was recorded as written
    CHECK_EQUAL(0xFF, store[f->start+METADATA_SIZE]); //make sure no data was actually written
}

//Writes should fail if the size being written takes us beyond the destructive read pointer,
// i.e. are larger than the available space.
//Since, for this test, the file size is three pages, simply attempt to write four and make sure the fourth fails
TEST(BasicFileWriteTest, TestWriteLargerThanFreeSpace1)
{
    uint8_t size = FLASH_PAGE_SIZE - METADATA_SIZE;
    uint8_t written;
    uint8_t data[FLASH_PAGE_SIZE-METADATA_SIZE] = {0};

    written = file_write(f, data, size); //chunk one, should pass
    CHECK_EQUAL(size, written);
    written = file_write(f, data, size); //chunk two, should pass
    CHECK_EQUAL(size, written);
    written = file_write(f, data, size); //chunk three, should pass
    CHECK_EQUAL(size, written);
    written = file_write(f, data, size); //chunk four, should FAIL, goes beyond current page, and first page is still in use
    CHECK_EQUAL(0x00, written);
}

//This is a FIFO, aka ring buffer. Once writes reach the end of the buffer, if there remains
//free space, the writes should wrap around!
TEST(BasicFileWriteTest, TestWritesThatWrapAround)
{
    uint8_t size = FLASH_PAGE_SIZE-METADATA_SIZE;
    uint8_t data[FLASH_PAGE_SIZE-METADATA_SIZE] = {0};

    //files in this test are 3 pages long. So let's begin by simply writing three pages of data
    file_write(f, data, size);
    file_write(f, data, size);
    file_write(f, data, size);
    //now, free up the first page by consuming it.
    file_read(f, data, size);
    file_consume(f, size);
    //make sure first page is free
    CHECK_EQUAL(0xFF, store[f->start+0]);

    //and write another page. Should go at beginning just fine
    uint8_t written = file_write(f, data, size);
    CHECK_EQUAL(size, written);
    CHECK_EQUAL(size, store[f->start+0]);
}

//Check that writes that reach the end of the allocated space wrap around to the
//first page again correctly

//CORNER CASES we haven't gotten to yet.

// Writes that takeus beyond the available space, i.e. that catch up to the read pointer

// Writes that need to wrap around the end of the physical memory address space