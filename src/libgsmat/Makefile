##################################################################
# libgsmat: An implementation of OpenVox G400P GSM/CDMA cards
#
# Parts taken from libpri
# Written by mark.liu <mark.liu@openvox.cn>
#
# Copyright (C) 2005-2010 OpenVox Communication Co. Ltd,
# All rights reserved.
#
# $Id: Makefile 294 2011-03-08 07:50:07Z liuyuan $
#
# Modify by freedom.huang <freedom.huang@openvox.cn>
# This program is free software, distributed under the terms of
# the GNU General Public License Version 2 as published by the
# Free Software Foundation. See the LICENSE file included with
# this program for more details.
##################################################################

CC=gcc

OSARCH=$(shell uname -s)
PROC?=$(shell uname -m)

# SONAME version; should be changed on every ABI change
# please don't change it needlessly; it's perfectly fine to have a SONAME
# of 1.2 and a version of 1.4.x
SONAME:=2.0.7

STATIC_LIBRARY=libgsmat.a
DYNAMIC_LIBRARY:=libgsmat.so.$(SONAME)
STATIC_OBJS=gsm.o gsmsched.o  version.o gsm_sms.o gsm_module.o gsm_config.o
DYNAMIC_OBJS=gsm.lo gsmsched.lo version.lo gsm_sms.lo gsm_module.lo gsm_config.lo
CFLAGS=-Wall -Werror -Wstrict-prototypes -Wmissing-prototypes -g3 -O0 -fPIC $(ALERTING) $(LIBEXTEND_COUNTERS) 
LD_FLAGS=-lm -lrt
INSTALL_PREFIX=$(DESTDIR)
INSTALL_BASE=/usr
libdir?=$(INSTALL_BASE)/lib
SOFLAGS:=-Wl,-h$(DYNAMIC_LIBRARY)
LDCONFIG = /sbin/ldconfig



ifneq (,$(findstring X$(OSARCH)X, XLinuxX XGNU/kFreeBSDX))
LDCONFIG_FLAGS=-n
else

ifeq (${OSARCH},FreeBSD)
LDCONFIG_FLAGS=-m
CFLAGS += -I../zaptel -I../zapata
INSTALL_BASE=/usr/local
endif

endif



ifeq (${OSARCH},SunOS)
CFLAGS += -DSOLARIS -I../zaptel-solaris
LDCONFIG = 
LDCONFIG_FLAGS = \# # Trick to comment out the period in the command below
#INSTALL_PREFIX = /opt/asterisk  # Uncomment out to install in standard Solaris location for 3rd party code
endif



#The problem with sparc is the best stuff is in newer versions of gcc (post 3.0) only.
#This works for even old (2.96) versions of gcc and provides a small boost either way.
#A ultrasparc cpu is really v9 but the stock debian stable 3.0 gcc doesnt support it.
ifeq ($(PROC),sparc64)
PROC=ultrasparc
CFLAGS += -mtune=$(PROC) -O3 -pipe -fomit-frame-pointer -mcpu=v8
endif






all: $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)

install: $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)
	mkdir -p $(INSTALL_PREFIX)$(libdir)
	mkdir -p $(INSTALL_PREFIX)$(INSTALL_BASE)/include
ifneq (${OSARCH},SunOS)
	install -m 644 libgsmat.h $(INSTALL_PREFIX)$(INSTALL_BASE)/include
	install -m 755 $(DYNAMIC_LIBRARY) $(INSTALL_PREFIX)$(libdir)
	if [ -x /usr/sbin/sestatus ] && ( /usr/sbin/sestatus | grep "SELinux status:" | grep -q "enabled"); then /sbin/restorecon -v $(INSTALL_PREFIX)$(libdir)/$(DYNAMIC_LIBRARY); fi
	( cd $(INSTALL_PREFIX)$(libdir) ; ln -sf libgsmat.so.$(SONAME) libgsmat.so)
	install -m 644 $(STATIC_LIBRARY) $(INSTALL_PREFIX)$(libdir)
	if test $$(id -u) = 0; then $(LDCONFIG) $(LDCONFIG_FLAGS) $(INSTALL_PREFIX)$(libdir); fi
else
	install -f $(INSTALL_PREFIX)$(INSTALL_BASE)/include -m 644 libgsmat.h
	install -f $(INSTALL_PREFIX)$(libdir) -m 755 $(DYNAMIC_LIBRARY)
	( cd $(INSTALL_PREFIX)$(libdir) ; ln -sf libgsmat.so.$(SONAME) libgsmat.so)
	install -f $(INSTALL_PREFIX)$(libdir) -m 644 $(STATIC_LIBRARY)
endif

uninstall:
	@echo "Removing Libgsmat"
	rm -f $(INSTALL_PREFIX)$(libdir)/libgsmat.so.$(SONAME)
	rm -f $(INSTALL_PREFIX)$(libdir)/libgsmat.so
	rm -f $(INSTALL_PREFIX)$(libdir)/libgsmat.a
	rm -f $(INSTALL_PREFIX)$(INSTALL_BASE)/include/libgsmat.h

gsmtest: gsmtest.o
	$(CC) -o gsmtest gsmtest.o -L. -lgsm -lzap $(CFLAGS)




MAKE_DEPS= -MD -MT $@ -MF .$(subst /,_,$@).d -MP

%.o: %.c
	$(CC) $(CFLAGS) $(MAKE_DEPS) -c -o $@ $<

%.lo: %.c
	$(CC) $(CFLAGS) $(MAKE_DEPS) -c -o $@ $<

$(STATIC_LIBRARY): $(STATIC_OBJS)
	ar rcs $(STATIC_LIBRARY) $(STATIC_OBJS)
	ranlib $(STATIC_LIBRARY)

$(DYNAMIC_LIBRARY): $(DYNAMIC_OBJS)
	$(CC) -shared $(SOFLAGS) -o $@ $(DYNAMIC_OBJS) $(LD_FLAGS)
	$(LDCONFIG) $(LDCONFIG_FLAGS) .
	ln -sf libgsmat.so.$(SONAME) libgsmat.so

version.c: FORCE
	@chmod a+x build_tools/make_version_c
	@build_tools/make_version_c > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

clean:
	rm -f *.o *.so *.lo *.so.$(SONAME) version.c
	rm -f $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)
	rm -f gsmtest gsmdump
	rm -f .*.d

.PHONY:

FORCE:

ifneq ($(wildcard .*.d),)
   include .*.d
endif
