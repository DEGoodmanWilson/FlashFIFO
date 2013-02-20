/************************************
 FIFO.h
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

 This file defines the API for reading and writing to NOR-flash-based FIFOs.

 ************************************/

#ifndef FILESYSTEM_H
#define	FILESYSTEM_H

#include "configure.h"

#ifdef	__cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <stdint.h>
    // For SEEK_SET, SEEK_END, etc.

#define FILE_SIZE (3*FLASH_PAGE_SIZE) //each file is 3 pages; allows for triple buffering
#define FILE_OFFSET 0 //must be a multiple of a page size!

    enum FILE_ID
    {
        FILE_ROOT_BLOCK = 0,
        FILE_FIRMWARE,
        FILE_DRIVE_LOG,
        FILE_DEBUG_LOG,
        FILE_PREFS,
        FILE_ALIVE,
        FILE_SCRATCH,
        FILE_CRASH_LOG,
        FILE_MAX
    };

    //how many handles to a particular file can be given out to user code?
#define MAX_HANDLES 1
#define PAGE_COUNTER_SIZE 1

    typedef struct file_handle_proto_t
    {
        enum FILE_ID file_id;

        uint32_t metadata_raw_start;
        uint32_t metadata_write_offset;

        uint32_t start;
        uint32_t write_offset;

        uint32_t raw_read_chunk_start;
        uint32_t raw_read_chunk_offset;
        uint32_t destructive_read_offset;

        uint32_t free_space;

        uint8_t write_count;
    } file_handle_t;

#define INVALID_FILE_HANDLE   ((file_handle_t*)NULL)

    //write and consume should be atomic
    file_handle_t* file_open(enum FILE_ID id);
    void file_close(file_handle_t* handle);
    void file_truncate(enum FILE_ID id);
    size_t file_consume(file_handle_t * handle, size_t n); //read/delete n bytes off the top of the FIFO
    size_t file_size(file_handle_t* handle);
    void file_sync(file_handle_t* handle);
    size_t file_read(file_handle_t* handle, uint8_t* data, size_t size);
    void file_seek(file_handle_t* handle, uint32_t offset, int whence);
    size_t file_write(file_handle_t* handle, uint8_t* data, size_t size);

#ifdef	__cplusplus
}
#endif

#endif	/* FILESYSTEM_H */

