# Copyright (c) 2016  Fabrice Triboix
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


###  BEGIN CONFIGURATION PARAMETERS  ###

# **VERY IMPORTANT**: This makefile assumes all source files have different
# names. This is because all object files go to a single build directory. Also
# ensure that no directory or file name has blank in them.

# Set D to 1 to increase verbosity
D = 0

# Set V to "debug" to make a debug build
# Supported variants: "release" (default) and "debug"
V = debug

# Set PLF to the platform you want to build to
# This must be one of the platform directory listed under "src/plf"
PLF := $(shell ./autodetectplf.py)

CC = gcc
CXX = g++
AR = ar
DEFS = -D_GNU_SOURCE
CFLAGS = -Wall -Wextra -Werror -Wno-unused-parameter -std=c99 -pthread
CXXFLAGS = -Wall -Wextra -Werror -Wno-unused-parameter -std=c++11 -pthread
LINKFLAGS = -pthread
CXXLIB = -lstdc++

ifneq ($(V),debug)
CFLAGS += -O3
CXXFLAGS += -O3
else
DEFS += -DSKAL_WITH_FLLOC
CFLAGS += -O0 -g
CXXFLAGS += -O0 -g
endif

CFLAGS += $(DEFS)
CXXFLAGS += $(DEFS)

# Paths where to find include files; add more if needed (but without -I)
INC_PATH = $(C_INCLUDE_PATH)

# Paths where to find libraries; add more if needed (but without -L)
LIB_PATH = $(LIBRARY_PATH)

# Default installation PREFIX
PREFIX = /usr/local

# Installation directories
BINDIR = $(PREFIX)/bin
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include
DOCDIR = $(PREFIX)/doc/skal

###  END CONFIGURATION PARAMETERS  ###



###  DO NOT MODIFY ANYTHIG BELOW THIS LINE  ###

BUILDDIR = build/$(PLF)/$(V)
TOPDIR = ../../..

CFLAGS += $(foreach i,$(INC_PATH),-I$(i))
CXXFLAGS += $(foreach i,$(INC_PATH),-I$(i))
LINKFLAGS += $(foreach i,$(LIB_PATH),-L$(i))

# Export all variables
export

# Check there are no duplicate source file names
count1 := $(shell find lib -name '*.c' -o -name '*.cpp' | wc -l)
count2 := $(shell find lib -name '*.c' -o -name '*.cpp' \
		| sed -e 's:.*/::' -e 's:\..*::' | sort | uniq | wc -l)
ifneq ($(count1),$(count2))
$(error Duplicate source file names detected)
endif

all doc test install dbg:
	@mkdir -p $(BUILDDIR) && $(MAKE) -C $(BUILDDIR) -f $(TOPDIR)/make.mk $@

clean:
	rm -rf $(BUILDDIR)

# Helper targets
debug release:
	@$(MAKE) -C . V=$@ all
