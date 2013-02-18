/************************************
 test_main.c
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

 This file provides a basic main() function for running the unit tests.
 It is only used for unit testing; it is not part of the final library itself.

************************************/

#include <CppUTest/CommandLineTestRunner.h>

int main(int argc, char** argv)
{
	CommandLineTestRunner::RunAllTests(argc, argv);
}
