#
# Gererated Makefile - do not edit!
#
# Edit the Makefile in the project folder instead (../Makefile). Each target
# has a -pre and a -post target defined where you can add custumized code.
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
FC=f77

# Include project Makefile
include Makefile

# Object Directory
OBJECTDIR=build/Release/Sun12-Solaris-Sparc

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
.build-conf: ${BUILD_SUBPROJECTS} dist/Release/Sun12-Solaris-Sparc/testbuild

dist/Release/Sun12-Solaris-Sparc/testbuild: ${OBJECTFILES}
	${MKDIR} -p dist/Release/Sun12-Solaris-Sparc
	${LINK.c} -o dist/Release/Sun12-Solaris-Sparc/testbuild ${OBJECTFILES} ${LDLIBSOPTIONS} 

# Subprojects
.build-subprojects:

# Clean Targets
.clean-conf:
	${RM} -r build/Release
	${RM} dist/Release/Sun12-Solaris-Sparc/testbuild

# Subprojects
.clean-subprojects:
