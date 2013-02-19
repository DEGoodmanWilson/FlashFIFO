/************************************
 configure.h
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

 This file provides some basic configuration parameters that define the size
 of the flash being used.

************************************/

#ifndef CONFIGURE_H
#define	CONFIGURE_H

//these are basic default values for the flash size that are not at all
//realistic, but useful for testing.
#define FLASH_PAGE_SIZE ( 128 )
#define FLASH_CHIP_SIZE ( 64 * FLASH_PAGE_SIZE )


#endif	/* CONFIGURE_H */

