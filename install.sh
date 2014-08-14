# !/bin/bash
#
# install.sh - OpenVox Installation Script for G400P
#
# Written by mark.liu <mark.liu@openvox.cn>
#
# Modified by freedom.huang <freedom.huang@openvox.cn>
#
# Copyright (C) 2005-2012 OpenVox Communication Co. Ltd,
# All rights reserved.
#
# $Id$
#
# This program is free software, distributed under the terms of
# the GNU General Public License Version 2 as published by the
# Free Software Foundation. See the LICENSE file included with
# this program for more details.
# 

# ----------------------------------------------------------------------------
# Clear the screen if it is supported.
# ----------------------------------------------------------------------------
clearscr()
{
	if [ $SETUP_INSTALL_QUICK = "YES" ]; then
		return
	fi

	if test $NONINTERACTIVE; then
		return
	fi

	# check if the terminal environment is set up
	[ "$TERM" ] && clear 2> /dev/null
}


# ----------------------------------------------------------------------------
# check bash
# ----------------------------------------------------------------------------
check_bash ()
{
	BASH_SUPPORT=`echo $BASH_VERSION | cut -d'.' -f1 2> /dev/null`
	test -z $BASH_SUPPORT && echo "Bash not existed!" && exit 1
}


# ----------------------------------------------------------------------------
# Display banner
# ----------------------------------------------------------------------------
banner()
{
	if test -z $NONINTERACTIVE; then
		clearscr
	fi
	
	echo -e "########################################################################"
	echo -e "#                   OpenVox Extra Installation Script                  #"
	echo -e "#                                v$PROD_VERSION                                #"
	echo -e "#                     OpenVox Communication Co.,Ltd                    #"
	echo -e "#        Copyright (c) 2009-2012 OpenVox. All Rights Reserved.         #"
	echo -e "########################################################################"	
	echo ""
	
	return 0
}


# ----------------------------------------------------------------------------
# Save logger info
# ----------------------------------------------------------------------------
logger()
{
	if [ "$2" == "0" ]; then
		:
	else
		echo -ne "$1"
	fi
	if [ "$LOG_ENABLE" == "YES" ]; then
		if [ "$3" == "0" ]; then
			:
		else
			echo -ne "$(LANG=C date) : $1" >> "$INSTALL_LOG"
		fi
	fi
}


# ----------------------------------------------------------------------------
# Display error message.
# ----------------------------------------------------------------------------
error()
{
	echo -ne "Error: $*" >&2

	if [ "$LOG_ENABLE" == "YES" ]; then
		echo -ne "$(LANG=C date) : Error: $*" >> "$INSTALL_LOG"
	fi
}


# ----------------------------------------------------------------------------
# Pause.
# ----------------------------------------------------------------------------
pause()
{
	[ $# -ne 0 ] && sleep $1 >&2 >> /dev/null && return 0
	echo -e "Press [Enter] to continue...\c"
	read tmp
	
	return 0
}

# ----------------------------------------------------------------------------
# Prompt user for input.
# ----------------------------------------------------------------------------
prompt()
{
	if test $NONINTERACTIVE; then
		return 0
	fi

	echo -ne "$*" >&2
	read CMD rest
	return 0
}

# ----------------------------------------------------------------------------
# Get Yes/No
# ----------------------------------------------------------------------------
getyn()
{
	if test $NONINTERACTIVE; then
		return 0
	fi

	while prompt "$* (y/n) "
	do	case $CMD in
			[yY])	return 0
				;;
			[nN])	return 1
				;;
			*)	echo -e "\nPlease answer y or n" >&2
				;;
		esac
	done
}

# ----------------------------------------------------------------------------
# Select an item from the list.
# $SEL: the available chooice
# Return:	0 - selection is in $SEL
#		    1 - quit or empty list
# ----------------------------------------------------------------------------
get_select()
{
	[ $# -eq 0 ] && return 1

	while prompt "Please enter your selection (1..$# or q) ->"
	do	case ${CMD:="0"} in
		[0-9]|[0-9][0-9])
			[ $CMD -lt 1 -o $CMD -gt $# ] && echo -e "\nError: Invalid option, input a value between 1 and $# \n" && continue
			SEL=$CMD
			return 0
			;;
		q|Q)	
			SEL="q"
			return 1
			;;
		esac
	done
}

# ----------------------------------------------------------------------------
# Select an item from the list.
# ----------------------------------------------------------------------------
select_list()
{
	[ $# -eq 0 ] && return 1

	col=`expr $# / 10 + 1`
	cnt=0
	for option in "$@"
	do	
		cnt=`expr $cnt + 1`
		echo -en "\t$cnt) $option"
		[ `expr $cnt % $col` -eq 0 ] && echo ""
	done
	echo -e "\tq) quit\n"
	get_select "$@"
}


show_status()
{
	if [ "$LOG_ENABLE" == "YES" ]; then
		echo "" >> "$INSTALL_LOG"
		echo -ne "$(LANG=C date) : \t\t\t\t\t\t[ $1 ]\n" >> "$INSTALL_LOG"
	fi
	logger "\r\t\t\t\t\t\t\t\t[ $1 ]\n" 1 0
}


error_compile()
{
	echo
	tail -n 50 "$INSTALL_LOG"
	echo "==========================================================================="
	logger "$1"
	show_status Failure
	echo "==========================================================================="
	exit 1
}


backup_file()
{
	\cp -a "$1" "$1.$(date +%F-%k-%M)"
	if [ $? -ne 0 ]; then
		return 1
	fi
}


check_file()
{
	if [ ! -f "$1" ]; then
		echo "==========================================================================="
		error "$1 not found\n"
		echo "==========================================================================="
		return 1
	fi
}

check_dahdi_tool_config_files()
{
	local flag
	flag=0
	
	if [ ! -d "$DAHDI_LINUX_COMPLETE_SOURCE_DIR" ]; then
		error "$DAHDI_LINUX_COMPLETE_SOURCE_DIR not found\n"
		return 1
	fi

	cd "$DAHDI_LINUX_COMPLETE_SOURCE_DIR"
	
	for file in $*
	do
		check_file "$DAHDI_LINUX_COMPLETE_SOURCE_DIR/$file"
		if [ $? -ne 0 ]; then
			flag=1
		fi
	done

	if [ $flag -eq 1 ]; then
		exit 1
	fi

	echo
	echo "==========================================================================="
	logger "Checking dahdi_tool configuration files ..."
	show_status OK
	echo "==========================================================================="
	
	cd "$PROD_HOME"

	pause 1
}

check_asterisk_config_files()
{
	local flag
	flag=0
	
	if [ ! -d "$AST_SOURCE_DIR" ]; then
		error "$AST_SOURCE_DIR not found\n"
		return 1
	fi

	cd "$AST_SOURCE_DIR"
	
	for file in $*
	do
		check_file "$AST_SOURCE_DIR/$file"
		if [ $? -ne 0 ]; then
			flag=1
		fi
	done

	if [ $flag -eq 1 ]; then
		exit 1
	fi

	echo
	echo "==========================================================================="
	logger "Checking asterisk configuration files ..."
	show_status OK
	echo "==========================================================================="
	
	cd "$PROD_HOME"

	pause 1
}

backup_dahdi_tool_config_files()
{
	local flag
	flag=0

	cd "$DAHDI_LINUX_COMPLETE_SOURCE_DIR"

	for file in $*
	do
		backup_file "$file"
		if [ $? -ne 0 ]; then
			flag=1
		fi
	done

	if [ $flag -eq 1 ]; then
		exit 1
	fi

	echo
	echo "==========================================================================="
	logger "Backuping dahdi_tool configuration files ..."
	show_status OK
	echo "==========================================================================="
	
	cd "$PROD_HOME"

	pause 1
}


backup_asterisk_config_files()
{
	local flag
	flag=0

	cd "$AST_SOURCE_DIR"

	for file in $*
	do
		backup_file "$file"
		if [ $? -ne 0 ]; then
			flag=1
		fi
	done

	if [ $flag -eq 1 ]; then
		exit 1
	fi

	echo
	echo "==========================================================================="
	logger "Backuping asterisk configuration files ..."
	show_status OK
	echo "==========================================================================="
	
	cd "$PROD_HOME"

	pause 1
}

check_dahdi_tool_patch_files()
{
	local flag
	flag=0
	
	if [ ! -d "$DAHDI_LINUX_COMPLETE_SOURCE_DIR" ]; then
		error "$DAHDI_LINUX_COMPLETE_SOURCE_DIR not found\n"
		return 1
	fi
	
	if [ ! -d "$PROD_PATCH_DIR/dahdi" ]; then
		error "$PROD_PATCH_DIR/dahdi not found\n"
		return 1
	fi

	cd "$PROD_PATCH_DIR/dahdi"
	dahdi_tool_patch_files="Chandahdi.pm.sed"
	dahdi_tool_patch_files="$dahdi_tool_patch_files ""Chanextra.pm"
	dahdi_tool_patch_files="$dahdi_tool_patch_files ""Span.pm.1.sed Span.pm.2.sed Span.pm.3.sed"
	dahdi_tool_patch_files="$dahdi_tool_patch_files ""dahdi_genconf.sed"
	dahdi_tool_patch_files="$dahdi_tool_patch_files ""Gen.pm.sed"
	dahdi_tool_patch_files="$dahdi_tool_patch_files ""Params.pm.sed"
	dahdi_tool_patch_files="$dahdi_tool_patch_files ""PCI.pm.sed"
	dahdi_tool_patch_files="$dahdi_tool_patch_files ""modules.sample.sed"
	
	for file in $dahdi_tool_patch_files
	do
		check_file "$PROD_PATCH_DIR/dahdi/$file"
		if [ $? -ne 0 ]; then
			flag=1
		fi
	done

	if [ $flag -eq 1 ]; then
		exit 1
	fi

	echo
	echo "==========================================================================="
	logger "Checking dahdi_tool patch files ..."
	show_status OK
	echo "==========================================================================="
	
	cd "$PROD_HOME"

	pause 1

}

check_asterisk_patch_files()
{
	local flag
	flag=0
	
	if [ ! -d "$AST_SOURCE_DIR" ]; then
		error "$AST_SOURCE_DIR/asterisk not found\n"
		return 1
	fi
	
	if [ ! -d "$PROD_PATCH_DIR/asterisk" ]; then
		error "$PROD_PATCH_DIR not found\n"
		return 1
	fi

	cd "$PROD_PATCH_DIR/asterisk"
	asterisk_patch_files=".moduleinfo.sed"
	asterisk_patch_files="$asterisk_patch_files ""autoconfig.h.in.sed"
	asterisk_patch_files="$asterisk_patch_files ""configure.1.sed configure.2.sed configure.3.sed configure.4.sed configure.5.sed"
	asterisk_patch_files="$asterisk_patch_files ""configure.ac.1.sed configure.ac.2.sed"
	
	for file in $asterisk_patch_files
	do
		check_file "$PROD_PATCH_DIR/asterisk/$file"
		if [ $? -ne 0 ]; then
			flag=1
		fi
	done

	if [ $flag -eq 1 ]; then
		exit 1
	fi

	echo
	echo "==========================================================================="
	logger "Checking asterisk patch files ..."
	show_status OK
	echo "==========================================================================="
	
	cd "$PROD_HOME"

	pause 1
}


redhat_check_dependencies()
{
	missing_packages=" "
	logger "Checking for C development tools ..."
	eval "rpm -q gcc > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "gcc --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"gcc "
		fi
	fi
		
	logger "Checking for C++ developement tools ..."
	eval "rpm -q gcc-c++ > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else	
		eval "g++ --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"gcc-c++ "
		fi
	fi
	
	logger "Checking for Make utility ..."
	eval "rpm -q make > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "make --version > /dev/null 2>&1"	
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"make "
		fi
	fi
	
	logger "Checking for ncurses library ... "
	eval "rpm -q ncurses > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type clear > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"ncurses "
		fi
	fi
	
	logger "Checking for ncurses-devel library ... "
	eval "rpm -q ncurses-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ ! -f "/usr/include/ncurses.h" ] && [ ! -f "/usr/include/ncurses/ncurses.h" ]; then
			show_status	FAILURE
			missing_packages=$missing_packages"ncurses-devel "
		else
			show_status	OK
		fi
	fi
	
	logger "Checking for Perl developement tools ..."
	eval "rpm -q perl > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "perl --version >/dev/null > 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"perl "
		fi
	fi
	
	logger "Checking for Patch ..."
	eval "rpm -q patch > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "patch --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"patch "
		fi
	fi
	
	logger "Checking for bison..."
	eval "rpm -q bison > /dev/null"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type bison > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"bison "
		fi
	fi
	
	logger "Checking for bison-devel..."
	eval "rpm -q bison-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [  -f /usr/lib/liby.a ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"bison-devel "
		fi
	fi
	
	logger "Checking for openssl..."
	eval "rpm -q openssl > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type openssl > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"openssl "
		fi
	fi
	
	logger "Checking for openssl-devel..."
	eval "rpm -q openssl-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/openssl/ssl.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"openssl-devel "
		fi
	fi
	
	logger "Checking for gnutls-devel..."
	eval "rpm -q gnutls-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/gnutls/gnutls.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"gnutls-devel "
		fi
	fi

	logger "Checking for zlib..."
	eval "rpm -q zlib > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/lib/libz.so.1 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"zlib "
		fi
	fi

	logger "Checking for zlib-devel..."
	eval "rpm -q zlib-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/zlib.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"zlib-devel "
		fi
	fi

	logger "Checking for kernel development packages..."
	eval "rpm -q kernel-devel-$(uname -r) > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		show_status	FAILURE
		missing_packages=$missing_packages"kernel-devel-$(uname -r) "
	fi
	
	logger "Checking for libxml2-devel..."
	eval "rpm -q libxml2-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		show_status	FAILURE
		missing_packages=$missing_packages"libxml2-devel "
	fi

	logger "Checking for wget..."
	eval "rpm -q wget > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		show_status	FAILURE
		missing_packages=$missing_packages"wget "
	fi
	
	echo
	if [  "$missing_packages" !=  " " ]; then
		echo "WARNING: You are missing some prerequisites"
		logger "Missing Packages $missing_packages\n"
		for package in $missing_packages
		do
			case $package in
				gcc)
					echo -e "\n C Compiler (gcc)."
					echo -e "    Required for compiling packages."
					echo -e "    Install gcc package (e.g yum install gcc)."
				;;
				g++)
					echo -e "\n C++ Compiler (g++)."
					echo -e "    Required for compiling packages."
					echo -e "    Install gcc-c++ package (e.g yum install gcc-c++)."
				;;
				make)
					echo -e "\n make utility."
					echo -e "    Required for compiling packages."
					echo -e "    Install make package (e.g yum install make)."
				;;
				bash)
					echo -e "\n Bash v2 or greater."
					echo -e "    Required for installation and configuration scripts."
				;;
				ncurses)
					echo -e "\n ncurses library."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses development package (e.g yum install ncurses)."
				;;
				ncurses-devel)
					echo -e "\n ncurses-devel library."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses development package (e.g yum install ncurses-devel)."
				;;
				perl)
					echo -e "\n Perl development tools."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses Perl package (e.g yum install perl)."
				;;
				patch)
					echo -e "\n Patch ."
					echo -e "    Required for compiling packages."
					echo -e "    Install Patch package (e.g yum install patch)."
				;;
				bison)
					echo -e "\n Bison."
					echo -e "    Required for compiling packages."
					echo -e "    Install bison package (e.g yum install bison)."
				;;
				bison-devel)
					echo -e "\n Bison library."
					echo -e "    Required for compiling packages."
					echo -e "    Install bison-devel package (e.g yum install bison-devel)."
				;;
				openssl)
					echo -e "\n OpenSSL."
					echo -e "    Required for compiling packages."
					echo -e "    Install openssl package (e.g yum install openssl)."
				;;
				openssl-devel)
					echo -e "\n OpenSSL library."
					echo -e "    Required for compiling packages."
					echo -e "    Install openssl-devel package (e.g yum install openssl-devel)."
				;;
				gnutls-devel)
					echo -e "\n Gnutls library."
					echo -e "    Required for compiling packages."
					echo -e "    Install gnutls-devel package (e.g yum install gnutls-devel)."
				;;
				zlib)
					echo -e "\n Zlib library."
					echo -e "    Required for compiling packages."
					echo -e "    Install gnutls-devel package (e.g yum install zlib)."
				;;
				zlib-devel)
					echo -e "\n Zlib development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install zlib-devel package (e.g yum install zlib-devel)."
				;;
				kernel-devel-$(uname -r))
					echo -e "\n Kernel development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install kernel-devel-$(uname -r) package (e.g yum install kernel-devel-$(uname -r))."
				;;
				libxml2-devel)
					echo -e "\n libxml2 development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install libxml2-devel package (e.g yum install libxml2-devel)."
				;;
				wget)
					echo -e "\n wget tools."
					echo -e "    Required for compiling packages."
					echo -e "    Install wget package (e.g yum install wget)."
				;;

			esac
		done

		echo
		getyn "Would you like to install the missing packages"
		if [ $? -eq 0 ]; then
			for package in $missing_packages
			do
				echo "yum install -y $package"
				yum install -y $package
			done
		fi
	fi

	#Freedom del 2012-10-12 10:01 
	#pause
	
	return 0
}


suse_check_dependencies()
{
	missing_packages=" "
	logger "Checking for C development tools ..."
	eval "rpm -q gcc > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "gcc --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"gcc "
		fi
	fi
		
	logger "Checking for C++ developement tools ..."
	eval "rpm -q gcc-c++ > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "g++ --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"gcc-c++ "
		fi
	fi
	
	logger "Checking for Make utility ..."
	eval "rpm -q make > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "make --version > /dev/null 2>&1"	
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"make "
		fi
	fi
	
	logger "Checking for ncurses library ... "
	eval "rpm -q ncurses > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type clear > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"ncurses "
		fi
	fi
	
	logger "Checking for ncurses-devel library ... "
	eval "rpm -q ncurses-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ ! -f "/usr/include/ncurses.h" ] && [ ! -f "/usr/include/ncurses/ncurses.h" ]; then
			show_status	FAILURE
			missing_packages=$missing_packages"ncurses-devel "
		else
			show_status	OK
		fi
	fi
	
	logger "Checking for Perl developement tools ..."
	eval "rpm -q perl > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "perl --version >/dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"perl "
		fi
	fi
	
	logger "Checking for Patch ..."
	eval "rpm -q patch > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "patch --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"patch "
		fi
	fi
	
	logger "Checking for bison..."
	eval "rpm -q bison > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type bison > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"bison "
		fi
	fi
	
	logger "Checking for bison-devel..."
	eval "rpm -q bison-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [  -f /usr/lib/liby.a ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"bison-devel "
		fi
	fi
	
	logger "Checking for openssl..."
	eval "rpm -q openssl > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type openssl > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"openssl "
		fi
	fi
	
	logger "Checking for openssl-devel..."
	eval "rpm -q openssl-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/openssl/ssl.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"openssl-devel "
		fi
	fi
	
	logger "Checking for gnutls-devel..."
	eval "rpm -q gnutls-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/gnutls/gnutls.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"gnutls-devel "
		fi
	fi

	logger "Checking for zlib..."
	eval "rpm -q zlib > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/lib/libz.so.1 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"zlib "
		fi
	fi

	logger "Checking for zlib-devel..."
	eval "rpm -q zlib-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/zlib.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"zlib-devel "
		fi
	fi

	logger "Checking for kernel development packages..."
	eval "rpm -q kernel-devel-$(uname -r) > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		show_status	FAILURE
		missing_packages=$missing_packages"kernel-devel-$(uname -r) "
	fi
	
	logger "Checking for libxml2-devel..."
	eval "rpm -q libxml2-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		show_status	FAILURE
		missing_packages=$missing_packages"libxml2-devel "
	fi
	
	echo
	if [  "$missing_packages" !=  " " ]; then
		echo "WARNING: You are missing some prerequisites"
		logger "Missing Packages $missing_packages\n"
		for package in $missing_packages
		do
			case $package in
				gcc)
					echo -e "\n C Compiler (gcc)."
					echo -e "    Required for compiling packages."
					echo -e "    Install gcc package (e.g zypper install gcc)."
				;;
				g++)
					echo -e "\n C++ Compiler (g++)."
					echo -e "    Required for compiling packages."
					echo -e "    Install gcc-c++ package (e.g zypper install gcc-c++)."
				;;
				make)
					echo -e "\n make utility."
					echo -e "    Required for compiling packages."
					echo -e "    Install make package (e.g zypper install make)."
				;;
				bash)
					echo -e "\n Bash v2 or greater."
					echo -e "    Required for installation and configuration scripts."
				;;
				ncurses)
					echo -e "\n ncurses library."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses development package (e.g zypper install ncurses)."
				;;
				ncurses-devel)
					echo -e "\n ncurses-devel library."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses development package (e.g zypper install ncurses-devel)."
				;;
				perl)
					echo -e "\n Perl development tools."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses Perl package (e.g zypper install perl)."
				;;
				patch)
					echo -e "\n Patch ."
					echo -e "    Required for compiling packages."
					echo -e "    Install Patch package (e.g zypper install patch)."
				;;
				bison)
					echo -e "\n Bison."
					echo -e "    Required for compiling packages."
					echo -e "    Install bison package (e.g zypper install bison)."
				;;
				bison-devel)
					echo -e "\n Bison library."
					echo -e "    Required for compiling packages."
					echo -e "    Install bison-devel package (e.g zypper install bison-devel)."
				;;
				openssl)
					echo -e "\n OpenSSL."
					echo -e "    Required for compiling packages."
					echo -e "    Install openssl package (e.g zypper install openssl)."
				;;
				openssl-devel)
					echo -e "\n OpenSSL library."
					echo -e "    Required for compiling packages."
					echo -e "    Install openssl-devel package (e.g zypper install openssl-devel)."
				;;
				gnutls-devel)
					echo -e "\n Gnutls library."
					echo -e "    Required for compiling packages."
					echo -e "    Install gnutls-devel package (e.g zypper install gnutls-devel)."
				;;
				zlib)
					echo -e "\n Zlib library."
					echo -e "    Required for compiling packages."
					echo -e "    Install gnutls-devel package (e.g zypper install zlib)."
				;;
				zlib-devel)
					echo -e "\n Zlib development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install zlib-devel package (e.g zypper install zlib-devel)."
				;;
				kernel-devel-$(uname -r))
					echo -e "\n Kernel development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install kernel-devel-$(uname -r) package (e.g zypper install kernel-devel-$(uname -r))."
				;;
				libxml2-devel)
					echo -e "\n libxml2 development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install libxml2-devel package (e.g yum zypper libxml2-devel)."
				;;

			esac
		done

		echo
		getyn "Would you like to install the missing packages"
		if [ $? -eq 0 ]; then
			for package in $missing_packages
			do
				echo "zypper install -y $package"
				zypper install -y $package
			done
		fi
	fi

	#Freedom del 2012-10-12 10:01 
	#pause
	
	return 0
}


debian_check_dependencies()
{
	missing_packages=" "
	logger "Checking for C development tools ..."
	eval "dpkg-query -s gcc > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "gcc --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"gcc "
		fi
	fi
		
	logger "Checking for C++ developement tools ..."
	eval "dpkg-query -s g++ > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else	
		eval "g++ --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"g++ "
		fi
	fi
	
	logger "Checking for Make utility ..."
	eval "dpkg-query -s make > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "make --version > /dev/null 2>&1"	
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"make "
		fi
	fi
	
	logger "Checking for ncurses library ... "
	eval "dpkg-query -s ncurses > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type clear > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"ncurses "
		fi
	fi
	
	logger "Checking for libncurses-dev library ... "
	eval "dpkg-query -s libncurses-dev > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ ! -f "/usr/include/ncurses.h" ] && [ ! -f "/usr/include/ncurses/ncurses.h" ]; then
			show_status	FAILURE
			missing_packages=$missing_packages"libncurses-dev "
		else
			show_status	OK
		fi
	fi
	
	logger "Checking for Perl developement tools ..."
	eval "dpkg-query -s perl > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "perl --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"perl "
		fi
	fi
	
	logger "Checking for Patch ..."
	eval "dpkg-query -s patch > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "patch --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"patch "
		fi
	fi
	
	logger "Checking for bison..."
	eval "dpkg-query -s bison > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type bison > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"bison "
		fi
	fi
	
	logger "Checking for bison-devel..."
	eval "dpkg-query -s bison-devel > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [  -f /usr/lib/liby.a ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"bison-devel "
		fi
	fi
	
	logger "Checking for openssl..."
	eval "dpkg-query -s openssl > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type openssl > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"openssl "
		fi
	fi
	
	logger "Checking for libssl-dev..."
	eval "dpkg-query -s libssl-dev > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/openssl/ssl.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"libssl-dev "
		fi
	fi
	
	logger "Checking for libgnutls-dev..."
	eval "dpkg-query -s libgnutls-dev > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/gnutls/gnutls.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"libgnutls-dev "
		fi
	fi

	logger "Checking for zlib1g..."
	eval "dpkg-query -s zlib1g > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/lib/libz.so.1 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"zlib1g "
		fi
	fi

	logger "Checking for zlib1g-dev..."
	eval "dpkg-query -s zlib1g-dev > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/zlib.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"zlib1g-dev "
		fi
	fi

	logger "Checking for kernel development packages..."
	eval "dpkg-query -s linux-headers-$(uname -r) > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		show_status	FAILURE
		missing_packages=$missing_packages"linux-headers-$(uname -r) "
	fi
	
	logger "Checking for libxml2-dev..."
	eval "dpkg-query -s libxml2-dev > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		show_status	FAILURE
		missing_packages=$missing_packages"libxml2-dev "
	fi
	
	echo
	if [  "$missing_packages" !=  " " ]; then
		echo "WARNING: You are missing some prerequisites"
		logger "Missing Packages $missing_packages\n"
		for package in $missing_packages
		do
			case $package in
				gcc)
					echo -e "\n C Compiler (gcc)."
					echo -e "    Required for compiling packages."
					echo -e "    Install gcc package (e.g apt-get install gcc)."
				;;
				g++)
					echo -e "\n C++ Compiler (g++)."
					echo -e "    Required for compiling packages."
					echo -e "    Install gcc-c++ package (e.g apt-get install gcc-c++)."
				;;
				make)
					echo -e "\n make utility."
					echo -e "    Required for compiling packages."
					echo -e "    Install make package (e.g apt-get install make)."
				;;
				bash)
					echo -e "\n Bash v2 or greater."
					echo -e "    Required for installation and configuration scripts."
				;;
				ncurses)
					echo -e "\n ncurses library."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses development package (e.g apt-get install ncurses)."
				;;
				libncurses-dev)
					echo -e "\n libncurses-dev library."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses development package (e.g apt-get install libncurses-dev)."
				;;
				perl)
					echo -e "\n Perl development tools."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses Perl package (e.g apt-get install perl)."
				;;
				patch)
					echo -e "\n Patch ."
					echo -e "    Required for compiling packages."
					echo -e "    Install Patch package (e.g apt-get install patch)."
				;;
				bison)
					echo -e "\n Bison."
					echo -e "    Required for compiling packages."
					echo -e "    Install bison package (e.g apt-get install bison)."
				;;
				bison-devel)
					echo -e "\n Bison library."
					echo -e "    Required for compiling packages."
					echo -e "    Install bison-devel package (e.g apt-get install bison-devel)."
				;;
				openssl)
					echo -e "\n OpenSSL."
					echo -e "    Required for compiling packages."
					echo -e "    Install openssl package (e.g apt-get install openssl)."
				;;
				libssl-dev)
					echo -e "\n OpenSSL library."
					echo -e "    Required for compiling packages."
					echo -e "    Install openssl-devel package (e.g apt-get install libssl-dev)."
				;;
				libgnutls-dev)
					echo -e "\n Gnutls library."
					echo -e "    Required for compiling packages."
					echo -e "    Install gnutls-devel package (e.g apt-get install libgnutls-dev)."
				;;
				zlib1g)
					echo -e "\n Zlib1g library."
					echo -e "    Required for compiling packages."
					echo -e "    Install gnutls-devel package (e.g apt-get install zlib1g)."
				;;
				zlib1g-dev)
					echo -e "\n Zlib development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install zlib-devel package (e.g apt-get install zlib1g-dev)."
				;;
				linux-headers-$(uname -r))
					echo -e "\n Kernel development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install kernel-devel-$(uname -r) package (e.g apt-get install linux-headers-$(uname -r))."
				;;
				libxml2-dev)
					echo -e "\n libxml2 development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install libxml2-devel package (e.g apt-get install libxml2-dev)."
				;;

			esac
		done

		echo
		getyn "Would you like to install the missing packages"
		if [ $? -eq 0 ]; then
			for package in $missing_packages
			do
				echo "apt-get install -y $package"
				apt-get install -y $package
			done
		fi
	fi

	#Freedom del 2012-10-12 10:01 
	#pause
	
	return 0
}


ubuntu_check_dependencies()
{
	missing_packages=" "
	logger "Checking for C development tools ..."
	eval "dpkg-query -s gcc > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "gcc --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"gcc "
		fi
	fi
		
	logger "Checking for C++ developement tools ..."
	eval "dpkg-query -s g++ > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else	
		eval "g++ --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"g++ "
		fi
	fi
	
	logger "Checking for Make utility ..."
	eval "dpkg-query -s make > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "make --version > /dev/null 2>&1"	
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"make "
		fi
	fi
	
	logger "Checking for ncurses library ... "
	eval "dpkg-query -s ncurses > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type clear > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"ncurses "
		fi
	fi
	
	logger "Checking for libncurses5-dev library ... "
	eval "dpkg-query -s libncurses5-dev > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ ! -f "/usr/include/ncurses.h" ] && [ ! -f "/usr/include/ncurses/ncurses.h" ]; then
			show_status	FAILURE
			missing_packages=$missing_packages"libncurses5-dev "
		else
			show_status	OK
		fi
	fi
	
	logger "Checking for Perl developement tools ..."
	eval "dpkg-query -s perl > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "perl --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"perl "
		fi
	fi
	
	logger "Checking for Patch ..."
	eval "dpkg-query -s patch > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "patch --version > /dev/null 2>&1"
		if [ $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"patch "
		fi
	fi
	
	logger "Checking for bison..."
	eval "dpkg-query -s bison > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type bison > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"bison "
		fi
	fi
	
	logger "Checking for openssl..."
	eval "dpkg-query -s openssl > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		eval "type openssl > /dev/null 2>&1"
		if [  $? -eq 0 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"openssl "
		fi
	fi
	
	logger "Checking for libssl-dev..."
	eval "dpkg-query -s libssl-dev > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/openssl/ssl.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"libssl-dev "
		fi
	fi
	
	logger "Checking for libgnutls-dev..."
	eval "dpkg-query -s libgnutls-dev > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/gnutls/gnutls.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"libgnutls-dev "
		fi
	fi

	logger "Checking for zlib1g..."
	eval "dpkg-query -s zlib1g > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/lib/libz.so.1 ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"zlib1g "
		fi
	fi

	logger "Checking for zlib1g-dev..."
	eval "dpkg-query -s zlib1g-dev > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		if [ -f /usr/include/zlib.h ]; then
			show_status	OK
		else
			show_status	FAILURE
			missing_packages=$missing_packages"zlib1g-dev "
		fi
	fi

	logger "Checking for kernel development packages..."
	eval "dpkg-query -s linux-headers-$(uname -r) > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		show_status	FAILURE
		missing_packages=$missing_packages"linux-headers-$(uname -r) "
	fi
	
	logger "Checking for libxml2-dev..."
	eval "dpkg-query -s libxml2-dev > /dev/null 2>&1"
	if [ $? -eq 0 ]; then
		show_status	OK
	else
		show_status	FAILURE
		missing_packages=$missing_packages"libxml2-dev "
	fi
	
	echo
	if [  "$missing_packages" !=  " " ]; then
		echo "WARNING: You are missing some prerequisites"
		logger "Missing Packages $missing_packages\n"
		for package in $missing_packages
		do
			case $package in
				gcc)
					echo -e "\n C Compiler (gcc)."
					echo -e "    Required for compiling packages."
					echo -e "    Install gcc package (e.g apt-get install gcc)."
				;;
				g++)
					echo -e "\n C++ Compiler (g++)."
					echo -e "    Required for compiling packages."
					echo -e "    Install gcc-c++ package (e.g apt-get install gcc-c++)."
				;;
				make)
					echo -e "\n make utility."
					echo -e "    Required for compiling packages."
					echo -e "    Install make package (e.g apt-get install make)."
				;;
				bash)
					echo -e "\n Bash v2 or greater."
					echo -e "    Required for installation and configuration scripts."
				;;
				ncurses)
					echo -e "\n ncurses library."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses development package (e.g apt-get install ncurses)."
				;;
				libncurses5-dev)
					echo -e "\n libncurses5 library."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses development package (e.g apt-get install libncurses5-dev)."
				;;
				perl)
					echo -e "\n Perl development tools."
					echo -e "    Required for compiling packages."
					echo -e "    Install ncurses Perl package (e.g apt-get install perl)."
				;;
				patch)
					echo -e "\n Patch ."
					echo -e "    Required for compiling packages."
					echo -e "    Install Patch package (e.g apt-get install patch)."
				;;
				bison)
					echo -e "\n Bison."
					echo -e "    Required for compiling packages."
					echo -e "    Install bison package (e.g apt-get install bison)."
				;;
				openssl)
					echo -e "\n OpenSSL."
					echo -e "    Required for compiling packages."
					echo -e "    Install openssl package (e.g apt-get install openssl)."
				;;
				libssl-dev)
					echo -e "\n OpenSSL library."
					echo -e "    Required for compiling packages."
					echo -e "    Install openssl-devel package (e.g apt-get install libssl-dev)."
				;;
				libgnutls-dev)
					echo -e "\n Gnutls library."
					echo -e "    Required for compiling packages."
					echo -e "    Install gnutls-devel package (e.g apt-get install libgnutls-dev)."
				;;
				zlib1g)
					echo -e "\n Zlib1g library."
					echo -e "    Required for compiling packages."
					echo -e "    Install gnutls-devel package (e.g apt-get install zlib1g)."
				;;
				zlib1g-dev)
					echo -e "\n Zlib development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install zlib-devel package (e.g apt-get install zlib1g-dev)."
				;;
				linux-headers-$(uname -r))
					echo -e "\n Kernel development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install kernel-devel-$(uname -r) package (e.g apt-get install linux-headers-$(uname -r))."
				;;
				libxml2-dev)
					echo -e "\n libxml2 development packages."
					echo -e "    Required for compiling packages."
					echo -e "    Install libxml2-devel package (e.g apt-get install libxml2-dev)."
				;;

			esac
		done

		echo
		getyn "Would you like to install the missing packages"
		if [ $? -eq 0 ]; then
			for package in $missing_packages
			do
				echo "apt-get install -y $package"
				apt-get install -y $package
			done
		fi
	fi

	#Freedom del 2012-10-12 10:01 
	#pause
	
	return 0
}


# ----------------------------------------------------------------------------
# Looking for dahdi directory
# ----------------------------------------------------------------------------
find_dahdi_dirs()
{
	local dahdi_linux_dirs
	local dahdi_linux_array	
	local dahdi_linux_complete_dirs
	local dahdi_linux_complete_array	
	local dahdi_tools_dirs
	local dahdi_tools_array	

	dahdi_linux_complete_dirs=$1
	
	clearscr
	banner	

	SETUP_INSTALL_QUICK="NO"
	
	# find dahdi-linux-complete in /usr/src
	echo 
	logger "Looking for dahdi-linux-complete directory in /usr/src ...\n"
	echo 
	if [ "$dahdi_linux_complete_dirs" == "" ]; then
		dahdi_linux_complete_dirs=`find /usr/src/ -maxdepth 2 -type d -name 'dahdi-linux-complete*' | xargs`
		if [ -d "$DAHDI_LINUX_COMPLETE_DFLT_INSTALL_DIR" ]; then
			dahdi_linux_complete_dirs="$DAHDI_LINUX_COMPLETE_DFLT_INSTALL_DIR ""$dahdi_linux_complete_dirs"
		fi
	fi
	
	# if dahdi-linux-complete not found, find dahdi-linux and dahdi-tools in /usr/src
#	if [ "$dahdi_linux_complete_dirs" == "" ]; then	
#		# find dahdi-linux in /usr/src
#		echo 
#		echo "Looking for dahdi-linux directory in /usr/src ..."
#		echo 
#		if [ "$dahdi_linux_dirs" == "" ]; then
#			dahdi_linux_dirs=`find /usr/src/ -maxdepth 2 -type d -name 'dahdi-linux*' | xargs`
#			if [ -d "$DAHDI_LINUX_DFLT_INSTALL_DIR" ]; then
#				dahdi_linux_dirs="$DAHDI_LINUX_DFLT_INSTALL_DIR ""$dahdi_linux_dirs"
#			fi
#		fi
#	 
#		# find dahdi-tools in /usr/src		
#		echo 
#		echo "Looking for dahdi-linux-complete directory in /usr/src ..."
#		echo 
#		if [ "$dahdi_tools_dirs" == "" ]; then
#			dahdi_tools_dirs=`find /usr/src/ -maxdepth 2 -type d -name 'dahdi-tools*' | xargs`
#			if [ -d "$DAHDI_TOOLS_DFLT_INSTALL_DIR" ]; then
#				dahdi_tools_dirs="$DAHDI_TOOLS_DFLT_INSTALL_DIR ""$dahdi_tools_dirs"
#			fi
#		fi
#	fi
	
	# traversal dahdi-linux-complete dirs	
	cnt=1
	for dir in $dahdi_linux_complete_dirs
	do
		# check the directory
		if [ ! -d "$dir" ]; then
			continue
		fi

		if  [ ! -f "$dir/linux/include/dahdi/kernel.h" ]; then
			continue
		fi

#			if [  -h "$dir" ]; then
#				continue
#			fi

		logger "$cnt : $dir\n"
		dahdi_linux_complete_array[$cnt]=$dir
		cnt=$((cnt+1)) 
	done

	# if not found in /usr/src
	if [ "$cnt" -eq 1 ]; then
		echo 
		logger "No dahdi-linux-complete dirs found in /usr/src/\n"
		logger "Please download the dahdi-linux-complete or enter the new dahdi-linux-complete path\n"
		echo
	fi

	echo "------------------------------------------"
	echo "n    : Download and install dahdi-linux-complete-2.6.1+2.6.1.tar.gz [Default installation]"
	echo "m    : Enter dahdi-linux-complete dir path manually"
	echo "d    : Download the latest version of dahdi-linux-complete"
	echo "q    : Skip the step"
	echo "(ctrl-c to Exit)"
	echo -n "Please select working dahdi-linux-complete directory [1-$((cnt-1)), n, m, d, q]: "

	read response
	logger "Please select working dahdi-linux-complete directory [1-$((cnt-1)), n, m, d, q]: $response\n" 0
	
	if [ ! "$response" ]; then
		echo
		error "Invalid Response $response\n"
		echo 
		find_dahdi_dirs "$dahdi_linux_complete_dirs"
		return 0
	fi

	if [ "$response" == "n" ]; then
		cd /usr/src
		DAHDI_LINUX_COMPLETE_SOURCE_DIR="dahdi-linux-complete-2.6.1+2.6.1"
		wget -c "http://downloads.asterisk.org/pub/telephony/dahdi-linux-complete/releases/$DAHDI_LINUX_COMPLETE_SOURCE_DIR"".tar.gz"
		if [ $? -ne 0 ]; then
			error "Downloading $DAHDI_LINUX_COMPLETE_SOURCE_DIR Failure\n"
			DAHDI_LINUX_COMPLETE_SOURCE_DIR=""
		else
			logger "Downloading $DAHDI_LINUX_COMPLETE_SOURCE_DIR OK\n"
			tar -xzvf "$DAHDI_LINUX_COMPLETE_SOURCE_DIR"".tar.gz"
			DAHDI_LINUX_COMPLETE_SOURCE_DIR="/usr/src/$DAHDI_LINUX_COMPLETE_SOURCE_DIR"									
		fi
		cd $PROD_HOME				
	elif [ "$response" == "m" ]; then
		echo
		echo "-> If you have been installed the DAHDI_LINUX_COMPLETE , please input the directory of DAHDI_LINUX_COMPLETE.[eg: /usr/src/$DAHDI_LINUX_COMPLETE_DFLT_INSTALL_DIR]"
		echo "-> If you want to choose dahdi version by yourself, please input the dahdi version.[eg: dahdi-linux-complete-2.5.0+2.5.0"
		echo "-> If the dahdi-linux-complete not found which your enter, the program will download it from http://downloads.asterisk.org/pub/telephony/dahdi-linux-complete/"
		echo -n "#> "
		read response		
		if [ $response == "" ]; then
			DAHDI_LINUX_COMPLETE_SOURCE_DIR=$DAHDI_LINUX_COMPLETE_DFLT_INSTALL_DIR
		else
			DAHDI_LINUX_COMPLETE_SOURCE_DIR=$response
			logger "Please enter dahdi-linux-complete dir path: $DAHDI_LINUX_COMPLETE_SOURCE_DIR\n"
			if [ ! -d "$DAHDI_LINUX_COMPLETE_SOURCE_DIR" ]; then
				cd /usr/src
				DAHDI_LINUX_COMPLETE_SOURCE_DIR=`basename $DAHDI_LINUX_COMPLETE_SOURCE_DIR`
				DAHDI_LINUX_COMPLETE_SOURCE_DIR=${DAHDI_LINUX_COMPLETE_SOURCE_DIR%.tar.gz}
				if [ ! -f "$DAHDI_LINUX_COMPLETE_SOURCE_DIR.tar.gz" ]; then
					logger "Downloading $DAHDI_LINUX_COMPLETE_SOURCE_DIR from http://downloads.asterisk.org/pub/telephony/dahdi-linux-complete/releases/$DAHDI_LINUX_COMPLETE_SOURCE_DIR.tar.gz\n"
					wget -c "http://downloads.asterisk.org/pub/telephony/dahdi-linux-complete/releases/$DAHDI_LINUX_COMPLETE_SOURCE_DIR"".tar.gz"
					if [ $? -ne 0 ]; then
						error "Downloading $DAHDI_LINUX_COMPLETE_SOURCE_DIR Failure\n"
						DAHDI_LINUX_COMPLETE_SOURCE_DIR=""
					else
						logger "Downloading $DAHDI_LINUX_COMPLETE_SOURCE_DIR OK\n"
						tar -xzvf "$DAHDI_LINUX_COMPLETE_SOURCE_DIR"".tar.gz"
						DAHDI_LINUX_COMPLETE_SOURCE_DIR="/usr/src/$DAHDI_LINUX_COMPLETE_SOURCE_DIR"									
					fi
				else
					tar -xzvf "$DAHDI_LINUX_COMPLETE_SOURCE_DIR"".tar.gz"
					DAHDI_LINUX_COMPLETE_SOURCE_DIR="/usr/src/$DAHDI_LINUX_COMPLETE_SOURCE_DIR"												
				fi

				cd $PROD_HOME				
#				find_dahdi_dirs
				return 0
			fi
		fi	
	elif [ "$response" == "d" ]; then
		cd /usr/src
		wget -c http://downloads.asterisk.org/pub/telephony/dahdi-linux-complete/dahdi-linux-complete-current.tar.gz
		if [ $? -ne 0 ]; then
			error "Downloading dahdi-linux-complete-current.tar.gz Failure\n"
			DAHDI_LINUX_COMPLETE_SOURCE_DIR=""
		else
			logger "Downloading dahdi-linux-complete-current.tar.gz OK\n"
			tar -xzvf dahdi-linux-complete-current.tar.gz
			DAHDI_LINUX_COMPLETE_SOURCE_DIR=`tar -tf dahdi-linux-complete-current.tar.gz | xargs | cut -d' ' -f1`
			DAHDI_LINUX_COMPLETE_SOURCE_DIR="/usr/src/""${DAHDI_LINUX_COMPLETE_SOURCE_DIR}"			
		fi
		cd $PROD_HOME
	elif [ "$response" = "q" ]; then
		DAHDI_LINUX_COMPLETE_SOURCE_DIR=""
		DHAID_AUTO_INSTALL="NO"
		logger "Skipped dahdi-linux-complete installation\n"
		return 0
	elif [ "$response" -gt 0 -a "$response" -lt $cnt ]; then
		DAHDI_LINUX_COMPLETE_SOURCE_DIR=${dahdi_linux_complete_array[$response]} 
	else
		echo
		error "Invalid Response $response\n"
		echo 
		find_dahdi_dirs "$dahdi_linux_complete_dirs"
		return 0
	fi

	DAHDI_KERNEL_H_PATH="${DAHDI_LINUX_COMPLETE_SOURCE_DIR}/linux/include/dahdi/kernel.h"
	if  [ ! -f "$DAHDI_KERNEL_H_PATH" ]; then
		clearscr		
		error "kernel.h not found in $DAHDI_LINUX_COMPLETE_SOURCE_DIR\n"	
		SETUP_INSTALL_QUICK="YES"
		find_dahdi_dirs "$dahdi_linux_complete_dirs"
		return 0
	fi
	
#	echo "DAHDI_LINUX_COMPLETE_SOURCE_DIR = $DAHDI_LINUX_COMPLETE_SOURCE_DIR"
#	pause
}


# ----------------------------------------------------------------------------
# Looking for asterisk directory
# ----------------------------------------------------------------------------
find_ast_dirs()
{
	local ast_dirs
	local astdir_array	
	ast_dirs=$1
	
	clearscr
	banner

	SETUP_INSTALL_QUICK="NO"
	
	# find asterisk in /usr/src/
	echo 
	logger "Looking for Asterisk directory in /usr/src ...\n"
	echo 
	if [ "$ast_dirs" == "" ]; then
		astdirs=`find /usr/src/ -maxdepth 2 -type d -name 'asterisk*' | xargs`
		if [ -d "$AST_DFLT_INSTALL_DIR" ]; then
			astdirs="$AST_DFLT_INSTALL_DIR "$astdirs
		fi
	fi

	# traversal asterisk dirs
	cnt=1
	for dir in $astdirs
	do
		# check the directory
		if [ ! -d "$dir" ]; then
			continue
		fi

		if [ ! -f "$dir/include/asterisk.h" ]; then
			continue
		fi

#		if [  -h "$dir" ]; then
#			continue
#		fi

		logger "$cnt : $dir\n"
		astdir_array[$cnt]=$dir
		cnt=$((cnt+1))
	done

	if [ "$cnt" -eq 1 ]; then
		echo 
		logger "No asterisk dirs found in /usr/src/"
		logger "Please download the asterisk or enter the new asterisk path"
		echo
	fi

	echo "------------------------------------------"
	echo "n    : Download and install asterisk-1.8-current.tar.gz [Default installation]"
	echo "m    : Enter Asterisk dir path manually"
	echo "q    : Skip the step"
	echo "(ctl-c to Exit)"
	echo -n "Please select working Asterisk directory [1-$((cnt-1)), n, m, d, q]: "

	read response
	logger "Please select working Asterisk directory [1-$((cnt-1)), n, m, d, q]: $response\n" 0

	if [ ! "$response" ]; then
		echo
		error "Invalid Response $response\n"
		echo 
		find_ast_dirs "$astdirs"
		return 0	
	fi

	if [ "$response" == "n" ]; then
		cd /usr/src
		AST_SOURCE_DIR="asterisk-1.8-current"
		wget -c "http://downloads.asterisk.org/pub/telephony/asterisk/$AST_SOURCE_DIR"".tar.gz"
		if [ $? -ne 0 ]; then
			error "Downloading $AST_SOURCE_DIR Failure\n"
			#Freedom del 2011-12-09 13:26
			#AST_SOURCE_DIR=""
		else
			logger "Downloading $AST_SOURCE_DIR OK\n"
			tar vfxz "$AST_SOURCE_DIR"".tar.gz"
			
			#Freedom del 2011-12-09 13:26
			#AST_SOURCE_DIR="/usr/src/$AST_SOURCE_DIR"			
		fi
		#Freedom Modify 2011-12-09 13:26
		#####################################################
		#cd $PROD_HOME
		find_ast_dirs
		#####################################################		
	elif [ "$response" == "m" ]; then
		echo
		echo "Please enter Asterisk dir path: [Default: $AST_DFLT_INSTALL_DIR]"
		echo "If asterisk not found, download it from http://downloads.asterisk.org/pub/telephony/asterisk"
		echo -n "#> "
		read response		
		if [ "$response" == "" ]; then
			AST_SOURCE_DIR=$AST_DFLT_INSTALL_DIR
		else
			AST_SOURCE_DIR=$response
			if [ ! -d "$AST_SOURCE_DIR" ]; then
				cd /usr/src
				AST_SOURCE_DIR=`basename $AST_SOURCE_DIR`
				AST_SOURCE_DIR=${AST_SOURCE_DIR%.tar.gz}
				if [ ! -f "$AST_SOURCE_DIR.tar.gz" ]; then
					logger "Downloading $AST_SOURCE_DIR from http://downloads.asterisk.org/pub/telephony/asterisk/releases/$AST_SOURCE_DIR.tar.gz\n"
					wget -c "http://downloads.asterisk.org/pub/telephony/asterisk/releases/$AST_SOURCE_DIR"".tar.gz"
					if [ $? -ne 0 ]; then
						error "Downloading $AST_SOURCE_DIR Failure\n"
						AST_SOURCE_DIR=""
					else
						logger "Downloading $AST_SOURCE_DIR OK\n"
						tar -xzvf "$AST_SOURCE_DIR"".tar.gz"
						AST_SOURCE_DIR="/usr/src/$AST_SOURCE_DIR"					
					fi
				else
					tar -xzvf "$AST_SOURCE_DIR"".tar.gz"
					AST_SOURCE_DIR="/usr/src/$AST_SOURCE_DIR"										
				fi
				
				cd $PROD_HOME				
#				find_ast_dirs
				return 0
			fi
		fi
	elif [ "$response" == "q" ]; then
		AST_SOURCE_DIR=""
		exit 0
	elif [ "$response" -gt 0 -a "$response" -lt $cnt ]; then
		AST_SOURCE_DIR=${astdir_array[$response]} 
	else
		echo
		error "Invalid Response $response\n"
		echo 
		find_ast_dirs "$astdirs"
		return 0
	fi

	if [ ! -f "$AST_SOURCE_DIR/include/asterisk.h" ]; then
		clearscr
		error "asterisk.h not found in $AST_SOURCE_DIR\n"
		SETUP_INSTALL_QUICK="YES"
		find_ast_dirs "$astdirs"
		return 0
	fi
	
#	echo "AST_SOURCE_DIR = $AST_SOURCE_DIR"
#	pause
}

patch_dahdi_tool()
{
	if [ $DAHDI_LINUX_COMPLETE_SOURCE_DIR == "" ]; then
		exit 1
	fi

	DAHDI_TOOL_CONFIG_FILES="tools/modules.sample"
	DAHDI_TOOL_CONFIG_FILES="$DAHDI_TOOL_CONFIG_FILES ""tools/xpp/perl_modules/Dahdi/Config/Params.pm"
	DAHDI_TOOL_CONFIG_FILES="$DAHDI_TOOL_CONFIG_FILES ""tools/xpp/perl_modules/Dahdi/Config/Gen.pm"
	DAHDI_TOOL_CONFIG_FILES="$DAHDI_TOOL_CONFIG_FILES ""tools/xpp/dahdi_genconf"
	DAHDI_TOOL_CONFIG_FILES="$DAHDI_TOOL_CONFIG_FILES ""tools/xpp/perl_modules/Dahdi/Span.pm"
	DAHDI_TOOL_CONFIG_FILES="$DAHDI_TOOL_CONFIG_FILES ""tools/xpp/perl_modules/Dahdi/Config/Gen/System.pm"
	DAHDI_TOOL_CONFIG_FILES="$DAHDI_TOOL_CONFIG_FILES ""tools/xpp/perl_modules/Dahdi/Config/Gen/Chandahdi.pm"
#	DAHDI_TOOL_CONFIG_FILES="$DAHDI_TOOL_CONFIG_FILES ""tools/xpp/perl_modules/Dahdi/Config/Gen/Chanextra.pm"
	DAHDI_TOOL_CONFIG_FILES="$DAHDI_TOOL_CONFIG_FILES ""tools/xpp/perl_modules/Dahdi/Hardware/PCI.pm"

	# check dahdi_tool configuration files
	check_dahdi_tool_config_files $DAHDI_TOOL_CONFIG_FILES
	
	# check *.sed file
	check_dahdi_tool_patch_files

	# backup asterisk configuration files
	backup_dahdi_tool_config_files $DAHDI_TOOL_CONFIG_FILES

	#patch /etc/dahdi/modules
	file_modules="$DAHDI_LINUX_COMPLETE_SOURCE_DIR/tools/modules.sample"
	grep -q "^opvxg4xx" $file_modules
	if [ $? -ne 0 ]; then
		file_modules_sample_sed="$PROD_PATCH_DIR/dahdi/modules.sample.sed"
		sed -i "/^xpp_usb/r $file_modules_sample_sed" $file_modules
	fi

	#patch /usr/lib/perl5/site_perl/5.8.8/Dahdi/Config/Params.pm
	file_params_pm="$DAHDI_LINUX_COMPLETE_SOURCE_DIR/tools/xpp/perl_modules/Dahdi/Config/Params.pm"
	grep -q "\bgsm_type\b" $file_params_pm
	if [ $? -ne 0 ]; then
		file_params_pm_sed="$PROD_PATCH_DIR/dahdi/Params.pm.sed"
		sed -i "/^\s\+pri_connection_type/r $file_params_pm_sed" $file_params_pm
	fi
	
	#patch /usr/lib/perl5/site_perl/5.8.8/Dahdi/Config/Gen.pm
	file_gen_pm="$DAHDI_LINUX_COMPLETE_SOURCE_DIR/tools/xpp/perl_modules/Dahdi/Config/Gen.pm"
	grep -q "\bgsm_type\b" $file_gen_pm
	if [ $? -ne 0 ]; then
		file_gen_pm_sed="$PROD_PATCH_DIR/dahdi/Gen.pm.sed"
		sed -i "/^\s\+pri_connection_type/r $file_gen_pm_sed" $file_gen_pm
	fi
	
	#patch /usr/sbin/dahdi_genconf
	file_dahdi_genconf="$DAHDI_LINUX_COMPLETE_SOURCE_DIR/tools/xpp/dahdi_genconf"
	grep -q "push @genlist, 'chanextra'" $file_dahdi_genconf
	if [ $? -ne 0 ]; then
		file_dahdi_genconf_sed="$PROD_PATCH_DIR/dahdi/dahdi_genconf.sed"
		sed -i "/@genlist = ('system', 'chandahdi')/r $file_dahdi_genconf_sed" $file_dahdi_genconf
	fi

	#patch /usr/lib/perl5/site_perl/5.8.8/Dahdi/Span.pm
	file_span_pm="$DAHDI_LINUX_COMPLETE_SOURCE_DIR/tools/xpp/perl_modules/Dahdi/Span.pm"
	grep -q "OpenVox G400P GSM" $file_span_pm
	if [ $? -ne 0 ]; then
		file_span_pm_1_sed="$PROD_PATCH_DIR/dahdi/Span.pm.1.sed"
		sed -i "/our \$DAHDI_PRI_CPE = 'pri_cpe'/r $file_span_pm_1_sed" $file_span_pm
	fi
	grep -q "foreach my \$cardtype (@gsm_strings)" $file_span_pm
	if [ $? -ne 0 ]; then
		file_span_pm_2_sed="$PROD_PATCH_DIR/dahdi/Span.pm.2.sed"
		sed -i "/\$self->{IS_PRI} = 0/r $file_span_pm_2_sed" $file_span_pm
	fi
	grep -q "if(\$chan_fqn =~ m(opvxg4xx" $file_span_pm
	if [ $? -ne 0 ]; then
		file_span_pm_3_sed="$PROD_PATCH_DIR/dahdi/Span.pm.3.sed"
		sed -i "/\$self->{CRC4} = undef/r $file_span_pm_3_sed" $file_span_pm
	fi

	#patch /usr/lib/perl5/site_perl/5.8.8/Dahdi/Config/Gen/system.pm
	file_system_pm="$DAHDI_LINUX_COMPLETE_SOURCE_DIR/tools/xpp/perl_modules/Dahdi/Config/Gen/System.pm"
	grep -q "\$span->is_gsm" $file_system_pm
	if [ $? -ne 0 ]; then
		sed -i "s/\$span->is_bri()/\$span->is_bri()|\$span->is_gsm()/g" $file_system_pm
		sed -i "s/(\$span->is_bri)/(\$span->is_bri|\$span->is_gsm)/g" $file_system_pm
	fi

	#patch /usr/lib/perl5/site_perl/5.8.8/Dahdi/Config/Gen/Chandahdi.pm
	file_chandahdi_pm="$DAHDI_LINUX_COMPLETE_SOURCE_DIR/tools/xpp/perl_modules/Dahdi/Config/Gen/Chandahdi.pm"
	grep -q "if(\$span->is_gsm)" $file_chandahdi_pm
	if [ $? -ne 0 ]; then
		file_chandahdi_pm_sed="$PROD_PATCH_DIR/dahdi/Chandahdi.pm.sed"
		sed -i "/foreach my \$span (@spans)/r $file_chandahdi_pm_sed" $file_chandahdi_pm
	fi
	
	#patch /usr/lib/perl5/site_perl/5.8.8/Dahdi/Config/Gen/chanextra.pm
	\cp "$PROD_PATCH_DIR/dahdi/Chanextra.pm" "$DAHDI_LINUX_COMPLETE_SOURCE_DIR/tools/xpp/perl_modules/Dahdi/Config/Gen/Chanextra.pm"

	#patch /usr/lib/perl5/site_perl/5.8.8/Dahdi/Hardware/PCI.pm
	file_pci_pm="$DAHDI_LINUX_COMPLETE_SOURCE_DIR/tools/xpp/perl_modules/Dahdi/Hardware/PCI.pm"
	grep -q "1b74:0100" $file_pci_pm
	if [ $? -ne 0 ]; then
		file_pci_pm_sed="$PROD_PATCH_DIR/dahdi/PCI.pm.sed"
		sed -i "/my %pci_ids = (/r $file_pci_pm_sed" $file_pci_pm
	fi
	

	return 0
}

install_dahdi()
{
	echo

	DAHDI_KERNEL_H_PATH="${DAHDI_LINUX_COMPLETE_SOURCE_DIR}/linux/include/dahdi/kernel.h"

	if [ ! -d $DAHDI_LINUX_COMPLETE_SOURCE_DIR ]; then
		error "$DAHDI_LINUX_COMPLETE_SOURCE_DIR is not a directory\n"
		exit 1
	fi

	if  [ ! -f $DAHDI_KERNEL_H_PATH ]; then
		error "kernel.h not found in $DAHDI_LINUX_COMPLETE_SOURCE_DIR\n"
		exit 1
	fi

	if [ x"$OPT_NC" != x"true" ]; then	

		cd $DAHDI_LINUX_COMPLETE_SOURCE_DIR
	
		make 2>> "$INSTALL_LOG"	
		if [ $? -ne 0 ]; then
			echo
			error_compile "Installing $DAHDI_LINUX_COMPLETE_SOURCE_DIR: make ..."
		fi

		make install 2>> "$INSTALL_LOG"
		if [ $? -ne 0 ]; then
			echo
			error_compile "Installing $DAHDI_LINUX_COMPLETE_SOURCE_DIR: make install ..."
		fi

	#	make config 2>> "$INSTALL_LOG"
		make config
		if [ $? -ne 0 ]; then
			echo
			error_compile "Installing $DAHDI_LINUX_COMPLETE_SOURCE_DIR: make config ..."
		fi
	fi
	
	echo
	echo "==========================================================================="
	logger "Installing $DAHDI_LINUX_COMPLETE_SOURCE_DIR ..."
	show_status OK
	echo "==========================================================================="

	cd $PROD_HOME

	pause 3			
}


#######################################################################################
# Create by Freedom 2011-09-07 13:19
#######################################################################################
DAHDI_VER_NUM=
get_dahdi_version()
{
	DAHDI_VER_STR=`cat $DAHDI_LINUX_COMPLETE_SOURCE_DIR/.version`

	for (( i=0; i<${#DAHDI_VER_STR}; i++ ))
	do
		tmp=${DAHDI_VER_STR:$i:1}

		if [ $tmp = "+" ]
		then
			break;
		elif [ $tmp = "-" ]
		then
			break;
		fi

		if [ "${tmp##[0-9]*} " = " " ] 
		then
			DAHDI_VER_NUM+=$tmp
		fi
	done

	for (( i=${#DAHDI_VER_NUM}; i<4; i++ ))
	do
		DAHDI_VER_NUM+="0"
	done
}


#######################################################################################
# Create by Freedom 2012-10-09 17:18
#######################################################################################
AST_VER_NUM=
get_ast_version()
{
	AST_VER_NUM=`awk -F. '{printf "%01d%02d%02d", $1, $2, $3}' $AST_SOURCE_DIR/.version`
}


install_opvxg4xx()
{
	echo

	local KBUILD_FILE

	if [ "$DAHDI_LINUX_COMPLETE_SOURCE_DIR" == "" ]; then
		error "dahdi-linux-complete not found in /usr/src\n"
		exit 1
	fi

	if [ ! -d $DAHDI_LINUX_COMPLETE_SOURCE_DIR ]; then
		error "$DAHDI_LINUX_COMPLETE_SOURCE_DIR is not a directory in /usr/src\n"
		exit 1
	fi
	
	KBUILD_FILE="$DAHDI_LINUX_COMPLETE_SOURCE_DIR/linux/drivers/dahdi/Kbuild"
	eval "\cp -rf $OPVXG4XX_DIR $DAHDI_LINUX_COMPLETE_SOURCE_DIR/linux/drivers/dahdi/"

	######################################################################
	# Freedom add 2011-09-07 13:25
	######################################################################
	get_dahdi_version
	if [ "$DAHDI_VER_NUM" == "0000" ] || [ "$DAHDI_VER_NUM" == "" ]; then
		error "get dahdi version error\n"
		exit 1
	fi
	OPVXG4XX_H_FILE="$DAHDI_LINUX_COMPLETE_SOURCE_DIR/linux/drivers/dahdi/opvxg4xx/opvxg4xx.h"
	if [ "$VIRTUAL_TTY" == "YES" ]; then
		sed -i "/#define _OPVXG4XX_H_/a#define DAHDI_VER_NUM $DAHDI_VER_NUM\n#define VIRTUAL_TTY" $OPVXG4XX_H_FILE
	else
		sed -i "/#define _OPVXG4XX_H_/a#define DAHDI_VER_NUM $DAHDI_VER_NUM" $OPVXG4XX_H_FILE
	fi
	#######################################################################
	
	eval "grep -q CONFIG_DAHDI_OPVXG4XX $KBUILD_FILE"
	if [ $? -ne 0 ]; then
		backup_file "$KBUILD_FILE"
		sed -i "/CONFIG_DAHDI_TRANSCODE/a\obj-\$(DAHDI_BUILD_ALL)\$(CONFIG_DAHDI_OPVXG4XX)		+= opvxg4xx/" $KBUILD_FILE
	fi

	echo
	echo "==========================================================================="	
	logger "Installing OpenVox G400P driver ..."
	show_status OK
	echo "==========================================================================="

	cd $PROD_HOME
	
	pause 3	
}


install_libgsmat()
{
	echo

	cd $LIBGSMAT_DIR

	LIBGSMAT_H_FILE_PATH="$LIBGSMAT_DIR/libgsmat.h"

	grep '#define VIRTUAL_TTY' $LIBGSMAT_H_FILE_PATH > /dev/null 2>&1
	if [ $? -ne 0 ];  then
		if [ "$VIRTUAL_TTY" == "YES" ]; then
			sed -i "/#define _LIBGSMAT_H/a#define VIRTUAL_TTY" $LIBGSMAT_H_FILE_PATH
		fi
	else
		if [ "$VIRTUAL_TTY" == "NO" ]; then
			sed -i "/#define VIRTUAL_TTY/d" $LIBGSMAT_H_FILE_PATH
		fi
	fi

	if [ x"$OPT_NC" != x"true" ]; then

		make clean 2>> "$INSTALL_LOG"
		if [ $? -ne 0 ]; then
			echo
			error_compile "Installing OpenVox G400P libgsmat library: make clean ..."
		fi

		make 2>> "$INSTALL_LOG"
		if [ $? -ne 0 ]; then
			echo
			error_compile "Installing OpenVox G400P libgsmat library: make ..."
		fi

		make install 2>> "$INSTALL_LOG"
		if [ $? -ne 0 ]; then
			echo
			error_compile "Installing OpenVox G400P libgsmat library: make install ..."
		fi

		##################################################################################
		#Freedom  add 2011-11-04 13:47 If 64bit OS,link libgsmat.so to /usr/lib64
		##################################################################################
		if [ -d "/usr/lib64" ]; then 
			if [ ` getconf LONG_BIT` = "64" ];then
				if [ -f "/usr/lib/libgsmat.so" ]; then
					\cp -rf /usr/lib/libgsmat.* /usr/lib64/
				fi
			fi
		fi
		##################################################################################
		#Freedom add 2011-11-17 copy module's configure file 
		##################################################################################
		mkdir -p /etc/openvox/opvxg4xx/modules
		\cp -r $PROD_HOME/src/libgsmat/config_files/*.conf /etc/openvox/opvxg4xx/modules
		##################################################################################
	fi
	
	echo
	echo "==========================================================================="
	logger "Installing OpenVox G400P libgsmat library ..."
	show_status OK
	echo "==========================================================================="

	cd $PROD_HOME	

	pause 1
}

patch_asterisk_configure()
{		
	if [ $AST_SOURCE_DIR == "" ]; then
		return 1
	fi

	if [ ! -f "$AST_SOURCE_DIR/configure" ]; then
		error "No configure file in $AST_SOURCE_DIR\n"
		return 1
	fi

	ASTERISK_CONFIG_FILES="configure"
	ASTERISK_CONFIG_FILES="$ASTERISK_CONFIG_FILES ""include/asterisk/autoconfig.h.in"
	ASTERISK_CONFIG_FILES="$ASTERISK_CONFIG_FILES ""configure.ac"
	ASTERISK_CONFIG_FILES="$ASTERISK_CONFIG_FILES ""makeopts.in"
	ASTERISK_CONFIG_FILES="$ASTERISK_CONFIG_FILES ""build_tools/menuselect-deps.in"

	# check asterisk configuration files
	check_asterisk_config_files $ASTERISK_CONFIG_FILES
	
	# check *.sed file
	check_asterisk_patch_files
	
	eval "grep -q GSMAT_LIB $AST_SOURCE_DIR/configure"
	if [ $? -eq 0 ]; then

		return 0
	fi

	# backup asterisk configuration files
	backup_asterisk_config_files $ASTERISK_CONFIG_FILES
	
	# add gsmat in configure 
	file_configure="$AST_SOURCE_DIR/configure"

	file_configure_1_sed="$PROD_PATCH_DIR/asterisk/configure.1.sed"
#  Freedom Modify 2011-08-24 11:51
############################################################################################
#	sed -i "/^PBX_PRI_INBANDDISCONNECT$/r $file_configure_1_sed" $file_configure
	sed -i "/^ac_subst_vars=*/r $file_configure_1_sed" $file_configure
############################################################################################

	file_configure_2_sed="$PROD_PATCH_DIR/asterisk/configure.2.sed"
	linenumber=`sed -n '/PRI_DESCRIP=\"ISDN PRI\"/=' $file_configure`
	if [ "$linenumber" == "" ]; then
		error "/if test \"${with_resample+set}\" = set; then/= no found\n"
		exit 1
	fi
	linenumber=$(($linenumber-1))
	sed -i "$linenumber r $file_configure_2_sed" $file_configure

	file_configure_3_sed="$PROD_PATCH_DIR/asterisk/configure.3.sed"
# Freedom Modify 2011-08-26 10:02
############################################################################################
#	linenumber=`sed -n '/if test \"x${PBX_RESAMPLE}\"/=' $file_configure`
	AST_SOURCE_DIR_TMP=`basename $AST_SOURCE_DIR`
	if [ "${AST_SOURCE_DIR_TMP:11:1}" == "4" ]; then
		linenumber=`sed -n '/if test \"${PBX_ZAPTEL}\"/=' $file_configure`
	else
		linenumber=`sed -n '/if test \"x${PBX_RESAMPLE}\"/=' $file_configure`
	fi
############################################################################################

	if [ "$linenumber" == "" ]; then
		error "PRI_INBANDDISCONNECT_DESCRIP=\"ISDN PRI set_inbanddisconnect\" no found\n"
		exit 1
	fi
	linenumber=$(($linenumber-1))
	sed -i "$linenumber r $file_configure_3_sed" $file_configure

	file_configure_4_sed="$PROD_PATCH_DIR/asterisk/configure.4.sed"
	linenumber=$(sed -n '/PBX_PRI!\$PBX_PRI\$ac_delim/=' $file_configure) 
	
	if [ "$linenumber" != "" ]; then
		sed -i "/PBX_PRI!\$PBX_PRI\$ac_delim/r $file_configure_4_sed" $file_configure

		linelist=$(sed -n -e '1{/if\ test\ `sed -n \"/p}' -e '/if\ test\ `sed -n \"/=' $file_configure)
		line_arr=($linelist)
		result=-1
		for (( i=0;  i<${#line_arr[@]};  i=i+1 ))  #${#line_arr[@]} is the number of line_arr elements  
		do
		  if [ "${line_arr[$i]}" -gt "$linenumber" ]; then
			 result=${line_arr[$i]}
			 break;
		  fi
		done
		content=$(sed -n ''$result'p' $file_configure)  #### if test `sed -n "s/.*$ac_delim\$/X/p" conf$$subs.sed | grep -c X` = 101; then
		oldno=$(echo $content | grep -Eo '[0-9]+') 
		newno=`expr $oldno + 4`
		sed -i "$result s/$oldno/$newno/" $file_configure
	fi

	file_configure_5_sed="$PROD_PATCH_DIR/asterisk/configure.5.sed"
	sed -i "/--with-pri=PATH/r $file_configure_5_sed" $file_configure


	# add gsmat in autoconfig.h.in 
	file_autocfg="$AST_SOURCE_DIR/include/asterisk/autoconfig.h.in"
	file_autocfg_sed="$PROD_PATCH_DIR/asterisk/autoconfig.h.in.sed"
# Freedom Modify 2011-08-29 09:48
############################################################################################
#	sed -i "/#undef HAVE_PRI_INBANDDISCONNECT/r $file_autocfg_sed" $file_autocfg
	sed -i "/#undef HAVE__BOOL/r $file_autocfg_sed" $file_autocfg
############################################################################################


	# add gsmat in configure.ac
	file_ac="$AST_SOURCE_DIR/configure.ac"
	file_ac_1_sed="$PROD_PATCH_DIR/asterisk/configure.ac.1.sed"
	file_ac_2_sed="$PROD_PATCH_DIR/asterisk/configure.ac.2.sed"
	sed -i "/AST_EXT_LIB_SETUP(\[PRI\], \[ISDN PRI\], \[pri\])/r $file_ac_1_sed" $file_ac
	sed -i "/AST_EXT_LIB_CHECK(\[PRI_INBANDDISCONNECT\], \[pri\], \[pri_set_inbanddisconnect\], \[libpri.h\])/r $file_ac_2_sed" $file_ac

	# add gsmat in makeopts.in
	file_opt="$AST_SOURCE_DIR/makeopts.in"
	file_opt_sed="$PROD_PATCH_DIR/asterisk/makeopts.in.sed"
	sed -i "/PRI_LIB=@PRI_LIB@/r $file_opt_sed" $file_opt

	# add gsmat in menuselect-deps.in
	file_menu="$AST_SOURCE_DIR/build_tools/menuselect-deps.in"
	file_menu_sed="$PROD_PATCH_DIR/asterisk/menuselect-deps.in.sed"
	sed -i "/PRI=@PBX_PRI@/r $file_menu_sed" $file_menu

	echo
	
	echo "==========================================================================="
	logger "Patching asterisk configure files ..."
	show_status OK
	echo "==========================================================================="
	
	cd $PROD_HOME

	pause 3
}


install_chan_extra()
{
	echo

	if [ ! -d $CHAN_EXTRA_DIR ]; then
		error "$CHAN_EXTRA_DIR not found\n"
	fi
	
	patch_asterisk_configure
		
	#Freedom Add 2012-01-31 11:10
	###############################################
	touch $CHAN_EXTRA_DIR/src/$CHAN_EXTRA_FILE
	###############################################
	\cp -a $CHAN_EXTRA_DIR/src/$CHAN_EXTRA_FILE $AST_SOURCE_DIR/channels/
	\cp -a $CHAN_EXTRA_DIR/config/chan_extra.conf $AST_SOURCE_DIR/configs/chan_extra.conf.sample
	\cp -a $CHAN_EXTRA_DIR/config/extra-channels.conf $AST_SOURCE_DIR/configs/extra-channels.conf.sample

	######################################################################
	# Freedom add 2012-10-09 17:20
	######################################################################
	get_ast_version
	if [ "$AST_VER_NUM" == "" ]; then
		error "get asterisk version error\n"
		exit 1
	fi
	sed -i "/#include \"asterisk.h\"/a#define ASTERISK_VERSION_NUM $AST_VER_NUM" $AST_SOURCE_DIR/channels/$CHAN_EXTRA_FILE
	#######################################################################

	echo
	echo "==========================================================================="
	logger "Installing OpenVox chan_extra module to $AST_SOURCE_DIR ..."
	show_status OK
	echo "==========================================================================="

	cd $PROD_HOME

	pause 3	
}


install_asterisk()
{
	echo

	cd $AST_SOURCE_DIR

##################################################################################
	#Freedom  add 2011-11-04 13:47 
##################################################################################
	if [ $INSTALL_WAY = "elastix" ]; then
	    if [ -f bootstrap.sh ]; then
		    ./bootstrap.sh 2>> "$INSTALL_LOG"
		    if [ $? -ne 0 ]; then
			    echo
			    error_compile "Installing Asterisk: ./bootstrap ..."
		    fi
	    fi
	fi
##################################################################################

	if [ x"$OPT_NC" != x"true" ]; then
		./configure 2>> "$INSTALL_LOG"
		if [ $? -ne 0 ]; then
			echo
			error_compile "Installing Asterisk: ./configure ..."
		fi

		make 2>> "$INSTALL_LOG"
		if [ $? -ne 0 ]; then
			echo
			error_compile "Installing Asterisk: make ..."
		fi

		make install 2>> "$INSTALL_LOG"
		if [ $? -ne 0 ]; then
			echo
			error_compile "Installing Asterisk: make install ..."
		fi

		##################################################################################
		#Freedom  add 2011-11-04 13:47 If 64bit OS,link chan_extra.so to /usr/lib64/asterisk/modules/chan_extra.so
		##################################################################################
		if [ -d "/usr/lib64/asterisk/modules" ]; then
			if [ ` getconf LONG_BIT` = "64" ];then 
				\cp -rf /usr/lib/asterisk/modules/*.so /usr/lib64/asterisk/modules/
   	 		fi
		fi
		##################################################################################

		mkdir -p /etc/asterisk
		if [ ! -f "/etc/asterisk/chan_extra.conf" ]; then
			\cp -a $CHAN_EXTRA_DIR/config/chan_extra.conf /etc/asterisk
		fi
		if [ ! -f "/etc/asterisk/extra-channels.conf" ]; then
			\cp -a $CHAN_EXTRA_DIR/config/extra-channels.conf /etc/asterisk
		fi
	
		getyn "Would you like to install the asterisk configuration files"
		if [ $? -eq 0 ]; then
			make samples 2>> "$INSTALL_LOG"
			if [ $? -ne 0 ]; then
				echo
				error_compile "Installing Asterisk: make samples ..."
			fi
		fi
	fi
	
	echo
	echo "==========================================================================="
	logger "Installing Asterisk ..."
	show_status OK
	echo "==========================================================================="
	
	cd $PROD_HOME	
}

fun_source_install()
{
#	chmod +x ./source-code_dahdi.sh
#	./source-code_dahdi.sh
	
	if [ x"$OPT_DAHDIDIR" = x"" ]; then
		find_dahdi_dirs
	else
		DAHDI_LINUX_COMPLETE_SOURCE_DIR=$OPT_DAHDIDIR
	fi

	# install dahdi
	if [ "$DHAID_AUTO_INSTALL" == "YES" ]; then
		if [ "$G400P_AUTO_INSTALL" == "YES" ]; then
			# patch dahdi-tool
			patch_dahdi_tool

			# install G400P opvxg4xx driver
			install_opvxg4xx
			LIBGSMAT_AUTO_INSTALL="YES"
			CHAN_EXTRA_AUTO_INSTALL="YES"
		else
			LIBGSMAT_AUTO_INSTALL="NO"
			CHAN_EXTRA_AUTO_INSTALL="NO"
		fi
		install_dahdi
	fi

	# install libgsmat for opvxg4xx	
	if [ "$LIBGSMAT_AUTO_INSTALL" == "YES" ]; then
		install_libgsmat
	fi
	
	if [ x"$OPT_ASTDIR" = x"" ]; then
		find_ast_dirs
	else
		AST_SOURCE_DIR=$OPT_ASTDIR
	fi

	# install asterisk	
	if [ "$AST_AUTO_INSTALL" == "YES" ]; then
		if [ "$CHAN_EXTRA_AUTO_INSTALL" == "YES" ]; then
			install_chan_extra
		fi
		install_asterisk
	fi
}


get_server_environment()
{
	echo "get_server_environment"
}


get_server_version()
{
	SYSTEM_VERSION=`uname -o`	

	TRIXBOX_V_PATH="/etc/trixbox/trixbox-version"

	# get trixbox version
	if [ -f $TRIXBOX_V_PATH ]; then
		ENVIRON_TRIXBOX_VERSION=`cat $TRIXBOX_V_PATH`
		SYSTEM_VERSION="TRIXBOX"
		return 0
	fi

#Freedom del 2011-11-16 17:37
	# get elastix version
#	if [ -f "/root/install.log" ]; then
#		ENVIRON_ELASTIX_VERSION=`cat /root/install.log | grep elastix-1 | cut -d' ' -f2`
#		if [ "$ENVIRON_ELASTIX_VERSION" = "" ]; then 
#		   ENVIRON_ELASTIX_VERSION=`cat /root/install.log | grep elastix-2 | cut -d' ' -f2`
#		fi 
#		ENVIRON_ELASTIX_VERSION=${ENVIRON_ELASTIX_VERSION%.noarch}
#		if [ -n "$ENVIRON_ELASTIX_VERSION" ]; then
#			SYSTEM_VERSION="ELASTIX"		
#		fi
#	fi	

#	logger "system version = $SYSTEM_VERSION $ENVIRON_TRIXBOX_VERSION $ENVIRON_ELASTIX_VERSION\n" 0
	logger "system version = $SYSTEM_VERSION $ENVIRON_TRIXBOX_VERSION \n" 0
		
	return 0
}


ask_vtty_support()
{

	echo "Please Note:"
	echo 
	echo "  If you select 'y'(TTY mode), you will be able to directly operate GSM "
	echo "modem to use some features like GPRS dial-up access, virtualterminal  "
	echo "over minicom, etc. By default, it doesn't recommend. It might cause   "
	echo "some unexpected issues."
	echo
	echo "  If you select 'n', you will have a stable voice, SMS transmission system."
	echo
	echo
	echo
	getyn "Do you need virtual TTY mode feature?"
	if [ $? -eq 0 ]; then
		VIRTUAL_TTY="YES"
	fi

	clearscr
	banner
}

fun_trixbox()
{		
	chmod +x ./trixbox_dahdi.sh

	if [ ! "$ENVIRON_TRIXBOX_VERSION" ]; then
		error "Unknown trixbox version\n"
		exit 1
	fi

	if [ ! "$SYSTEM_VERSION" ]; then
		error "Unknown Operating System\n"
		exit 1
	fi

	if [ "$ENVIRON_TRIXBOX_VERSION" == "2.8.0.1" ]; then
		logger "Trixbox Version = $ENVIRON_TRIXBOX_VERSION \n"
	elif [ "$ENVIRON_TRIXBOX_VERSION" == "2.8.0.3" ]; then
		logger "Trixbox Version = $ENVIRON_TRIXBOX_VERSION \n"

	elif [ "$ENVIRON_TRIXBOX_VERSION" == "2.8.0.4" ]; then
		logger "Trixbox Version = $ENVIRON_TRIXBOX_VERSION \n"

	else
		error "The current version was not support the trixbox-$ENVIRON_TRIXBOX_VERSION\n"
		return 1
	fi

	install_libgsmat	
	./trixbox_dahdi.sh $ENVIRON_TRIXBOX_VERSION $INSTALL_LOG

	echo
	echo "==========================================================================="
	logger "Installing OpenVox G400P driver for $ENVIRON_TRIXBOX_VERSION ..."
	show_status OK
	echo "==========================================================================="

}


fun_elastix()
{
	local ELASTIX_VER=`rpm -q elastix`
	local AST_VER=`rpm -q asterisk`
	local DAHDI_VER=`rpm -q dahdi`
	local ELASTIX_MAIN_VER=${ELASTIX_VER:8:3}

	local AST_SRC_RPM=${AST_VER}.src.rpm
	local DAHDI_SRC_RPM=${DAHDI_VER}.src.rpm

	if [ ${ELASTIX_MAIN_VER:0:1} == "2" ]; then
		ELASTIX_MAIN_VER="2.0"
	fi

	wget -c http://repo.elastix.org/elastix/${ELASTIX_MAIN_VER}/base/SRPMS/${AST_SRC_RPM}
	if [ ! $? -eq 0 ];then
		wget -c http://repo.elastix.org/elastix/${ELASTIX_MAIN_VER}/updates/SRPMS/${AST_SRC_RPM}
		if [ ! $? -eq 0 ];then
			wget -c http://repo.elastix.org/elastix/${ELASTIX_MAIN_VER}/beta/SRPMS/${AST_SRC_RPM}
			if [ ! $? -eq 0 ];then
				echo "Can't download ${AST_SRC_RPM}"
				exit 1
			fi
		fi
	fi

	wget -c http://repo.elastix.org/elastix/${ELASTIX_MAIN_VER}/beta/SRPMS/${DAHDI_SRC_RPM}
	if [ ! $? -eq 0 ];then
		wget -c http://repo.elastix.org/elastix/${ELASTIX_MAIN_VER}/base/SRPMS/${DAHDI_SRC_RPM}
		if [ ! $? -eq 0 ];then
			wget -c http://repo.elastix.org/elastix/${ELASTIX_MAIN_VER}/updates/SRPMS/${DAHDI_SRC_RPM}
			if [ ! $? -eq 0 ];then
				echo "Can't download ${DAHDI_SRC_RPM}"
				exit 1
			fi
		fi
	fi

	mkdir -p /usr/src/redhat/BUILD
	mkdir -p /usr/src/redhat/SOURCES
	mkdir -p /usr/src/redhat/RPMS
	mkdir -p /usr/src/redhat/SPECS
	mkdir -p /usr/src/redhat/SRPMS

	logger "Checking for rpm-build..."
	eval "rpm -q rpm-build > /dev/null 2>&1"
	if [ ! $? -eq 0 ]
	then
		yum install -y rpm-build
	fi

	logger "Checking for subversion..."
	eval "rpm -q subversion > /dev/null 2>&1"
	if [ ! $? -eq 0 ]
	then
		yum install -y subversion
	fi

	rpm -ivh ${AST_SRC_RPM}
	if [ ! $? -eq 0 ]
	then
		exit 1
	fi

	rpmbuild -bp /usr/src/redhat/SPECS/asterisk.spec 2> /tmp/rpmbuild.tmp
	if [ ! $? -eq 0 ]; then
		cat /tmp/rpmbuild.tmp | grep 'is needed by' |
		while read row;do
			PACKAGE_NAME=`echo $row | awk '{ print $1; }'`
			yum install -y $PACKAGE_NAME
		done
		rpmbuild -bp /usr/src/redhat/SPECS/asterisk.spec 2> /tmp/rpmbuild.tmp
		if [ ! $? -eq 0 ]; then
			echo
			cat /tmp/rpmbuild.tmp | grep 'is needed by' |
			while read row;do
				PACKAGE_NAME=`echo $row | awk '{ print $1; }'`
				echo You must manual install $PACKAGE_NAME
			done
			echo
			getyn "Are you ignore?"
			if [ $? -eq 0 ]; then
				rpmbuild --nodeps -bp /usr/src/redhat/SPECS/asterisk.spec
				if [ ! $? -eq 0 ]; then
					echo "rpmbuild asterisk error"
					exit 1
				fi
			else
				echo "Please goto http://repo.elastix.org/elastix find your need package."
				exit 1
			fi
		fi
	fi

	rpm -ivh ${DAHDI_SRC_RPM}
	if [ ! $? -eq 0 ]
	then
		exit 1
	fi

	rpmbuild -bp /usr/src/redhat/SPECS/dahdi.spec 2> /tmp/rpmbuild.tmp
	if [ ! $? -eq 0 ]; then
		cat /tmp/rpmbuild.tmp | grep 'is needed by' |
		while read row;do
			PACKAGE_NAME=`echo $row | awk '{ print $1; }'`
			yum install -y $PACKAGE_NAME
		done
		rpmbuild -bp /usr/src/redhat/SPECS/dahdi.spec 2> /tmp/rpmbuild.tmp
		if [ ! $? -eq 0 ]; then
			echo 
			cat /tmp/rpmbuild.tmp | grep 'is needed by' |
			while read row;do
				PACKAGE_NAME=`echo $row | awk '{ print $1; }'`
				echo You must manual install $PACKAGE_NAME
			done
			echo
			getyn "Are you ignore?"
			if [ $? -eq 0 ]; then
				rpmbuild --nodeps -bp /usr/src/redhat/SPECS/dahdi.spec
				if [ ! $? -eq 0 ]; then
					echo "rpmbuild dahdi error"
					exit 1
				fi
			else
				echo "Please goto http://repo.elastix.org/elastix find your need package." 
				exit 1
			fi
		fi
	fi

	SOURCE_DIR=`ls /usr/src/redhat/BUILD/|grep dahdi`
	DAHDI_LINUX_COMPLETE_SOURCE_DIR=/usr/src/redhat/BUILD/$SOURCE_DIR
	patch_dahdi_tool
	install_opvxg4xx
	install_dahdi

	SOURCE_DIR=`ls /usr/src/redhat/BUILD/|grep asterisk`
	AST_SOURCE_DIR=/usr/src/redhat/BUILD/$SOURCE_DIR

	install_libgsmat
	install_chan_extra
	install_asterisk

	echo
	echo "==========================================================================="
	logger "Installing OpenVox G400P driver for $ENVIRON_ELASTIX_VERSION ..."
	show_status OK
	echo "==========================================================================="
}


# Freedom add 2012-08-15 14:43 for usage
#############################################################################
usage()
{
	echo "Usage: $APP_NAME [OPTION]..."
	echo "Install chan_extra.c to asterisk and opvxg4xx to dahdi"
	echo 
	echo "OPTION:"
	echo "nc   -Only copy file and setting configuration files, no compile asterisk and dahdi"
	echo "astdir=asterisk source directory"
	echo "dahdidir=dahdi source directory"
	echo "way=src|trixbox|elastix  -Install way"
	echo "tty=yes|no  -TTY mode enable or disable"
}
#############################################################################


parse_option()
{
while [ $# -gt 0 ] ;do
	case $1 in
		"--help"|"-?")
			usage
			if [ $# -eq 1 ]; then
				exit 0
			else
				exit 255
			fi
			;;
		"nc")
			OPT_NC=true
			echo "No compile dahdi and asterisk"
			;;
		astdir=*)
			OPT_ASTDIR=${1:7}
			if [ ! -d $OPT_ASTDIR ]; then
				error "Asterisk directory $OPT_ASTDIR no exit"
				exit 255
			fi
			echo "asterisk source directory:$OPT_ASTDIR"
			;;
		dahdidir=*)
			OPT_DAHDIDIR=${1:9}
			if [ ! -d $OPT_DAHDIDIR ]; then
				error "Dahdi directory $OPT_DAHDIDIR no exit"
				exit 255
			fi
			echo "dahdi source directory:$OPT_DAHDIDIR"
			;;
		way=*)
			case ${1:4} in
				"src")
					OPT_WAY=1
					;;
				"tribox")
					OPT_WAY=2;
					;;
				"elastix")
					OPT_WAY=3;
					;;
				*)
					usage
					exit 255
					;;
			esac
			echo "Install way:${1:4}"
			;;
		tty=*)
			case ${1:4} in
				"yes")	
					OPT_TTY="YES"
					;;
				"no")
					OPT_TTY="NO"
					;;
				*)
					usage
					exit 255
					;;
			esac
			echo "tty mode: $OPT_TTY"
			;;
		*)
			usage
			exit 255
			;;
	esac

	shift
done

}


#  Freedom add 2011-12-16 17:40
##############################################################################
OS="redhat"
check_os()
{
	cat /proc/version | grep 'Red Hat' > /dev/null
	if [ $? -eq 0 ]; then
		OS="redhat"
		return
	fi

	cat /proc/version | grep 'SUSE' > /dev/null
	if [ $? -eq 0 ]; then
		OS="suse"
		return
	fi

	cat /proc/version | grep 'Debian' > /dev/null
	if [ $? -eq 0 ]; then
		OS="debian"
		return
	fi

	cat /proc/version | grep 'Ubuntu' > /dev/null
	if [ $? -eq 0 ]; then
		OS="ubuntu"
		return
	fi
}
################################################################################

#PROD_NAME="OPVX_EXTRA"
PATH=$PATH:/bin:/sbin:/usr/bin:/usr/sbin
KERNEL_VERSION=`uname -r`
KERNEL_ARCH=`uname -m`

#  Freedom Modify 2011-08-31 14:51
##################################################################################
#PROD_HOME=`pwd`
#PROD_HOME=$(dirname `which $0`)
#FULL_PATH=`which $0`
#PROD_HOME=${FULL_PATH%/*}            #Freedom Modify 2011-11-14 09:15

FULL_PATH="$0"
PROD_HOME=$(cd ${FULL_PATH%/*}; pwd)          #Freedom Modify 2011-12-20 17:46
###################################################################################

PROD_VERSION='2.0.7'

APP_NAME=${0##*/}


# Freedom Add 2011-11-23 10:48
INSTALL_WAY="source"

# patch, src patch
PROD_PATCH_DIR=$PROD_HOME/patches/
DRIVER_SRC_DIR=$PROD_HOME/src/
LIBGSMAT_DIR=$DRIVER_SRC_DIR/libgsmat/
OPVXG4XX_DIR=$DRIVER_SRC_DIR/opvxg4xx/
OPVXd115_DIR=$DRIVER_SRC_DIR/opvxd115/
OPVXA24XX_DIR=$DRIVER_SRC_DIR/opvxa24xx/
LOG_DIR=$PROD_HOME/log
CHAN_EXTRA_DIR=$DRIVER_SRC_DIR/chan_extra/
CHAN_EXTRA_FILE="chan_extra.c"

# log file
INSTALL_LOG="$LOG_DIR/install.log.$(date +%F-%H-%M)"

ROOT_UID=0
SUPERUSER=NO
LOG_ENABLE=YES

# asterisk and dahdi source code path
AST_DFLT_INSTALL_DIR="/usr/src/asterisk/"
DAHDI_LINUX_DFLT_INSTALL_DIR="/usr/src/dahdi-linux/"
DAHDI_TOOLS_DFLT_INSTALL_DIR="/usr/src/dahdi-tools/"
DAHDI_LINUX_COMPLETE_DFLT_INSTALL_DIR="/usr/src/dahdi-linux-complete"
DAHDI_LINUX_SOURCE_DIR=$DAHDI_LINUX_DFLT_INSTALL_DIR
DAHDI_TOOLS_SOURCE_DIR=$DAHDI_TOOLS_DFLT_INSTALL_DIR
DAHDI_LINUX_COMPLETE_SOURCE_DIR=$DAHDI_LINUX_DFLT_COMPLETE_INSTALL_DIR
AST_SOURCE_DIR=$AST_DFLT_INSTALL_DIR

G400P_AUTO_INSTALL="YES"
DHAID_AUTO_INSTALL="YES"
LIBGSMAT_AUTO_INSTALL="YES"
CHAN_EXTRA_AUTO_INSTALL="YES"
AST_AUTO_INSTALL="YES"

# ENVIRONMENT INFO
ENVIRON_TRIXBOX_VERSION=""
#Freedom del 2011-11-16 17:38
#ENVIRON_ELASTIX_VERSION=""
ENVIRON_LSB_COMPLIANT="YES"
ENVIRON_SYSTEM_RELEASE=""
ENVIRON_SYSTEM_VERSION=""
ENVIRON_SYSTEM_ARCH=""
ENVIRON_SYSTEM_LOCALE=""
ENVIRON_SYSTEM_=""
SYSTEM_VERSION=""

SETUP_INSTALL_QUICK="NO"

VIRTUAL_TTY="NO"

#environment += 'Your System is not lsb compliant\n'
#environment += 'Operating System Release : %s\n' \
#                    'Operating System Version : %s\n' \
#                    'Operating System Architecture : %s\n' \
#                    'Operating System Locale : %s\n'\
#                    'Python Version : %s\n'\
#                    'System Version : %s\n'\

# check superuser
if [ "$UID" -ne "$ROOT_UID" ]
then
	echo "Must be root to run this script."
	exit 1
else
	superuser=YES
fi

# clear log file
cat /dev/null > "$INSTALL_LOG"

parse_option $*

# Freedom add 2011-12-16 17:40
##############################################################################
# Check OS
if [ x"$OPT_NC" != x"true" ]; then
	check_os
fi
##############################################################################

# show banner
banner

# check bash version
check_bash

# check dependences
if [ x"$OPT_NC" != x"true" ]; then
	if [ x"$OS" = x"redhat" ]; then
		redhat_check_dependencies
	elif [ x"$OS" = x"suse" ]; then
		suse_check_dependencies
	elif [ x"$OS" = x"debian" ]; then
		debian_check_dependencies
	elif [ x"$OS" = x"ubuntu" ]; then
		ubuntu_check_dependencies
	else
		redhat_check_dependencies
	fi
fi

# show banner
banner

# get system version
get_server_version


# virtual tty support
if [ x"$OPT_TTY" = x"" ];then
	ask_vtty_support
else
	VIRTUAL_TTY=$OPT_TTY
fi

if [ x"$OPT_WAY" = x"" ]; then
	select_list "Source code install" "Trixbox-dahdi install" "Elastix-dahdi install"
else
	SEL=$OPT_WAY
fi

case $SEL in
	1) 
   	INSTALL_WAY="source"
		fun_source_install
		;;
	2)
   	INSTALL_WAY="trixbox"
		fun_trixbox
		;;
	3) 
   	INSTALL_WAY="elastix"
		fun_elastix
		;;
	q)
		exit 1
esac

