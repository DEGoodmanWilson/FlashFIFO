#
# Generated Makefile - do not edit!
#
# Edit the Makefile in the project folder instead (../Makefile). Each target
# has a -pre and a -post target defined where you can add customized code.
#
# This makefile implements configuration specific macros and targets.


# Environment
MKDIR=mkdir
CP=cp
GREP=grep
NM=nm
CCADMIN=CCadmin
RANLIB=ranlib
CC=gcc
CCC=g++
CXX=g++
FC=gfortran
AS=as

# Macros
CND_PLATFORM=GNU-MacOSX
CND_DLIB_EXT=dylib
CND_CONF=Debug
CND_DISTDIR=dist
CND_BUILDDIR=build

# Include project Makefile
include Makefile

# Object Directory
OBJECTDIR=${CND_BUILDDIR}/${CND_CONF}/${CND_PLATFORM}

# Object Files
OBJECTFILES= \
	${OBJECTDIR}/Test/FIFO_read_test.o \
	${OBJECTDIR}/Test/FIFO_write_test.o \
	${OBJECTDIR}/Test/flash_port_mock.o \
	${OBJECTDIR}/Test/test_main.o \
	${OBJECTDIR}/FIFO.o \
	${OBJECTDIR}/Test/FIFO_recover_handle_test.o


# C Compiler Flags
CFLAGS=-std=c99

# CC Compiler Flags
CCFLAGS=
CXXFLAGS=

# Fortran Compiler Flags
FFLAGS=

# Assembler Flags
ASFLAGS=

# Link Libraries and Options
LDLIBSOPTIONS=/usr/local/share/CppUTest/lib/libCppUTest.a

# Build Targets
.build-conf: ${BUILD_SUBPROJECTS}
	"${MAKE}"  -f nbproject/Makefile-${CND_CONF}.mk ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/flashfifo

${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/flashfifo: /usr/local/share/CppUTest/lib/libCppUTest.a

${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/flashfifo: ${OBJECTFILES}
	${MKDIR} -p ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}
	${LINK.cc} -o ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/flashfifo ${OBJECTFILES} ${LDLIBSOPTIONS} 

${OBJECTDIR}/Test/FIFO_read_test.o: Test/FIFO_read_test.cpp 
	${MKDIR} -p ${OBJECTDIR}/Test
	${RM} $@.d
	$(COMPILE.cc) -g -I. -I/usr/local/share/CppUTest/include -MMD -MP -MF $@.d -o ${OBJECTDIR}/Test/FIFO_read_test.o Test/FIFO_read_test.cpp

${OBJECTDIR}/Test/FIFO_write_test.o: Test/FIFO_write_test.cpp 
	${MKDIR} -p ${OBJECTDIR}/Test
	${RM} $@.d
	$(COMPILE.cc) -g -I. -I/usr/local/share/CppUTest/include -MMD -MP -MF $@.d -o ${OBJECTDIR}/Test/FIFO_write_test.o Test/FIFO_write_test.cpp

${OBJECTDIR}/Test/flash_port_mock.o: Test/flash_port_mock.c 
	${MKDIR} -p ${OBJECTDIR}/Test
	${RM} $@.d
	$(COMPILE.c) -g -I. -I/usr/local/share/CppUTest/include -MMD -MP -MF $@.d -o ${OBJECTDIR}/Test/flash_port_mock.o Test/flash_port_mock.c

${OBJECTDIR}/Test/test_main.o: Test/test_main.cpp 
	${MKDIR} -p ${OBJECTDIR}/Test
	${RM} $@.d
	$(COMPILE.cc) -g -I. -I/usr/local/share/CppUTest/include -MMD -MP -MF $@.d -o ${OBJECTDIR}/Test/test_main.o Test/test_main.cpp

${OBJECTDIR}/FIFO.o: FIFO.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} $@.d
	$(COMPILE.c) -g -I. -I/usr/local/share/CppUTest/include -MMD -MP -MF $@.d -o ${OBJECTDIR}/FIFO.o FIFO.c

${OBJECTDIR}/Test/FIFO_recover_handle_test.o: Test/FIFO_recover_handle_test.cpp 
	${MKDIR} -p ${OBJECTDIR}/Test
	${RM} $@.d
	$(COMPILE.cc) -g -I. -I/usr/local/share/CppUTest/include -MMD -MP -MF $@.d -o ${OBJECTDIR}/Test/FIFO_recover_handle_test.o Test/FIFO_recover_handle_test.cpp

# Subprojects
.build-subprojects:

# Clean Targets
.clean-conf: ${CLEAN_SUBPROJECTS}
	${RM} -r ${CND_BUILDDIR}/${CND_CONF}
	${RM} ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/flashfifo

# Subprojects
.clean-subprojects:

# Enable dependency checking
.dep.inc: .depcheck-impl

include .dep.inc
