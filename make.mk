# Copyright (c) 2016,2017  Fabrice Triboix
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

SHELL := /bin/sh

# Check existence of tools
CCACHE := $(shell which ccache 2> /dev/null)
DOXYGEN := $(shell which doxygen 2> /dev/null)
DOT := $(shell which dot 2> /dev/null)

MODULES = $(TOPDIR)/lib/plf/$(PLF) $(TOPDIR)/lib/common \
			$(TOPDIR)/lib/net $(TOPDIR)/lib/skal \
			$(TOPDIR)/lib/bindings/cpp

# Path for make to search for source files
VPATH = $(foreach i,$(MODULES),$(i)/src) $(foreach i,$(MODULES),$(i)/test) \
		$(TOPDIR)/skald/src $(TOPDIR)/skald/test $(TOPDIR)/utils

# Output libraries
LIBSKAL = libskal.a
LIBSKALCPP = libskalcpp.a
OUTPUT_LIBS = $(LIBSKAL) $(LIBSKALCPP)

# Include paths for compilation
INCS = $(foreach i,$(MODULES),-I$(i)/include) \
		$(foreach i,$(MODULES),-I$(i)/src) -I$(TOPDIR)/skald/src

# List public header files
HDRS = $(foreach i,$(MODULES),$(wildcard $(i)/include/*.h))

# List of object files for various targets
LIBSKAL_OBJS = skal-plf.o skal-common.o skal-net.o skal-blob.o skal-alarm.o \
		skal-msg.o skal-queue.o skal-thread.o skal.o
LIBSKALCPP_OBJS = skal-cpp.o
SKALD_OBJS = skald.o main.o
RTTEST_MAIN_OBJ = rttestmain.o
SKAL_TEST_OBJS = test-plf.o test-common.o test-net.o test-blob.o test-alarm.o \
		test-msg.o test-queue.o test-thread.o

# Libraries to link against when building programs
LINKLIBS = -lskal -lcds -lrttest -lrtsys
ifeq ($(V),debug)
LINKLIBS += -lflloc
endif


# Standard targets

all: $(OUTPUT_LIBS) skald skal-post skal_unit_tests writer reader doc

doc: doc/html/index.html


# Rules to build object files, libraries, programs, etc.

define RUN_CC
set -eu; \
cmd="$(CCACHE) $(CC) $(CFLAGS) $(INCS) -o $(1) -c $(2)"; \
if [ $(D) == 1 ]; then \
	echo "$$cmd"; \
else \
	echo "CC    $(1)"; \
fi; \
$$cmd || (echo "Command line was: $$cmd"; exit 1)
endef

define RUN_CXX
set -eu; \
cmd="$(CCACHE) $(CXX) $(CXXFLAGS) $(INCS) -o $(1) -c $(2)"; \
if [ $(D) == 1 ]; then \
	echo "$$cmd"; \
else \
	echo "CXX   $(1)"; \
fi; \
$$cmd || (echo "Command line was: $$cmd"; exit 1)
endef

define RUN_AR
set -eu; \
cmd="$(AR) crs $(1) $(2)"; \
if [ $(D) == 1 ]; then \
	echo "$$cmd"; \
else \
	echo "AR    $(1)"; \
fi; \
$$cmd || (echo "Command line was: $$cmd"; exit 1)
endef

define RUN_LINK
set -eu; \
cmd="$(CC) -L. $(LINKFLAGS) -o $(1) $(2) $(3)"; \
if [ $(D) == 1 ]; then \
	echo "$$cmd"; \
else \
	echo "LINK  $(1)"; \
fi; \
$$cmd || (echo "Command line was: $$cmd"; exit 1)
endef

$(LIBSKAL): $(LIBSKAL_OBJS)

$(LIBSKALCPP): $(LIBSKALCPP_OBJS)

skald: $(SKALD_OBJS) $(LIBSKAL)
	@$(call RUN_LINK,$@,$(filter %.o,$^),$(LINKLIBS))

skal_unit_tests: $(SKAL_TEST_OBJS) $(RTTEST_MAIN_OBJ) $(LIBSKAL) skald.o
	@$(call RUN_LINK,$@,$(filter %.o,$^),$(LINKLIBS))

skal-post: skal-post.o $(LIBSKAL)
	@$(call RUN_LINK,$@,$(filter %.o,$^),$(LINKLIBS))

writer: writer.o $(LIBSKAL)
	@$(call RUN_LINK,$@,$(filter %.o,$^),$(LINKLIBS))

reader: reader.o $(LIBSKAL)
	@$(call RUN_LINK,$@,$(filter %.o,$^),$(LINKLIBS))


doc/html/index.html: $(HDRS)
ifeq ($(DOXYGEN),)
	@echo "Doxygen not found, documentation will not be built"
else
	@set -eu; \
	if [ -z "$(DOT)" ]; then \
		cp $(TOPDIR)/Doxyfile doxy1; \
	else \
		cat $(TOPDIR)/Doxyfile | sed -e 's/HAVE_DOT.*/HAVE_DOT = YES/' > doxy1;\
	fi; \
	cat doxy1 | sed -e 's:INPUT[	 ]*=.*:INPUT = $^:' > doxy2; \
	cmd="doxygen doxy3"; \
	if [ $(D) == 1 ]; then \
		cp doxy2 doxy3; \
		echo "$$cmd"; \
	else \
		cat doxy2 | sed -e 's/QUIET.*/QUIET = YES/' > doxy3; \
		echo "DOXY  $@"; \
	fi; \
	$$cmd
endif

test: skal_unit_tests writer reader
	@set -eu; \
	if ! which rttest2text.py > /dev/null 2>&1; then \
		echo "ERROR: rttest2text.py not found in PATH"; \
		exit 1; \
	fi; \
	./$< > skal.rtt; \
	find $(TOPDIR) -name 'test-*.c' | xargs rttest2text.py skal.rtt


define INSTALL
set -eu; \
[ -x $(1) ] && mode=755 || mode=644; \
dst=$(2)/`basename $(1)`; \
cmd="mkdir -p `dirname $$dst` && cp -rf $(1) $$dst && chmod $$mode $$dst"; \
if [ $(D) == 1 ]; then \
	echo "$$cmd"; \
else \
	echo "INST  `basename $(1)`"; \
fi; \
eval $$cmd;
endef


install: install_bin install_lib install_inc install_doc

install_bin: skald
	@$(foreach i,$^,$(call INSTALL,$(i),$(BINDIR)))

install_lib: $(OUTPUT_LIBS)
	@$(foreach i,$^,$(call INSTALL,$(i),$(LIBDIR)))

install_inc:
	@$(foreach i,$(HDRS),$(call INSTALL,$(i),$(INCDIR)))

install_doc: doc
	@$(call INSTALL,doc/html,$(DOCDIR))


lib%.a:
	@$(call RUN_AR,$@,$^)

%.o: %.c
	@$(call RUN_CC,$@,$<)

%.o: %.cpp
	@$(call RUN_CXX,$@,$<)

dbg:
	@echo "Platform = $(PLF)"
	@echo "VPATH = $(VPATH)"
	@echo "HDRS = $(HDRS)"
	@echo "PATH = $(PATH)"


# Automatic header dependencies

OBJS = $(LIBSKAL_OBJS) $(SKALD_OBJS) $(RTTEST_MAIN_OBJ) $(SKAL_TEST_OBJS)

-include $(OBJS:.o=.d)

%.d: %.c
	@set -eu; \
	cmd="$(CC) $(CFLAGS) $(INCS) -MT $(@:.d=.o) -MM -MF $@ $<"; \
	if [ $(D) == 1 ]; then \
		echo "$$cmd"; \
	else \
		echo "DEP   $@"; \
	fi; \
	$$cmd

%.d: %.cpp
	@set -eu; \
	cmd="$(CXX) $(CXXFLAGS) $(INCS) -MT $(@:.d=.o) -MM -MF $@ $<"; \
	if [ $(D) == 1 ]; then \
		echo "$$cmd"; \
	else \
		echo "DEPXX $@"; \
	fi; \
	$$cmd
