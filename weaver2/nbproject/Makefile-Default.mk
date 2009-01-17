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
CCADMIN=CCadmin
RANLIB=ranlib
CC=cc
CCC=CC
CXX=CC
FC=f95

# Macros
PLATFORM=SunStudio-Solaris-Sparc

# Include project Makefile
include weaver2-Makefile.mk

# Object Directory
OBJECTDIR=build/Default/${PLATFORM}

# Object Files
OBJECTFILES=

# C Compiler Flags
CFLAGS=

# CC Compiler Flags
CCFLAGS=
CXXFLAGS=

# Fortran Compiler Flags
FFLAGS=

# Link Libraries and Options
LDLIBSOPTIONS=

# Build Targets
.build-conf: ${BUILD_SUBPROJECTS}
	cd ../mtpgsql/src && gmake -f Makefile

# Subprojects
.build-subprojects:

# Clean Targets
.clean-conf:
	cd ../mtpgsql/src && gmake -f Makefile clean

# Subprojects
.clean-subprojects:
