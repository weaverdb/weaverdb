# Build Targets
.build-conf: ${BUILD_SUBPROJECTS}
	cd . && ${MAKE} -f Makefile

# Subprojects
.build-subprojects:

# Clean Targets
.clean-conf:
	cd . && ${MAKE} -f Makefile clean

# Subprojects
.clean-subprojects:
