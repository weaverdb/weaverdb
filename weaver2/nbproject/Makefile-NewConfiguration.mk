.build-conf:
	@echo Tool collection not found.
	@echo Please specify existing tool collection in project properties
	@exit 1

# Clean Targets
.clean-conf:
	cd . && ${MAKE} -f Makefile clean

# Subprojects
.clean-subprojects:
