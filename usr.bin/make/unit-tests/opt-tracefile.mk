# $NetBSD: opt-tracefile.mk,v 1.4 2021/12/04 07:23:43 rillig Exp $
#
# Tests for the command line option '-T', which in jobs mode appends a trace
# record to a trace log whenever a job is started or completed.

all: .PHONY
	@rm -f opt-tracefile.log
	@${MAKE} -f ${MAKEFILE} -j1 -Topt-tracefile.log trace
	# Remove timestamps, process IDs and directory paths.
	@awk '{ print $$2, $$3 }' opt-tracefile.log

trace dependency1 dependency2: .PHONY
	@echo 'Making ${.TARGET} from ${.ALLSRC:S,^$,<nothing>,W}.'

trace: dependency1 dependency2
