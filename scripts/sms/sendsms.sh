#!/bin/bash

#
# Copyright (c) 2005-2012 OpenVox. All Rights Reserved.
#
# Create by:
# Freedom Huang <freedom.huang@openvox.cn>
# Date: 2012/09/29 11:28
# Version: 0.0.1
#
# This program is free software, distributed under the terms of
# the GNU General Public License Version 2. See the LICENSE file
# at the top of the source tree.
#

VERSION=0.0.1

###################################################
# The lines which have * in the following must be set before starting sending SMS!!!
###################################################

# Please UTF-8 codec save this file(*).

# Parameters Settings(*). 
# MESSAGE: 
#        The messages contents you want to send.
# SPANS: auto or <span>,<span> ...
#        You can specify a specific span number or set it to auto, then the system will select all available spans to send. 
#        Separate them by comma or space if have more than one destination numbers.
# NUMBERS: <phone number>,<phone number> ...
#        It's the destination number and seperate them by comma or space if you have more than one destination numbers.
# ATTEMPT: always or 0 or 1 or 2 or 3 or 4 ... 
#        The system will attempt to send the failed messages, you can specify how many times do you want to try. 
# VERBOSE: 0 or 1 or 2 or 3 or 4 ...
#        The verbose level of sending message.
# REPEAT: 1 or 2 or 3 or 4 ...
#        How many times of the same message would you like to send.
##################################################
MESSAGE=""
SPANS=
NUMBERS=
ATTEMPT=always
VERBOSE=3
REPEAT=1

#The second usage is like this:
##################################################
# sendsms.sh MESSAGE="message" SPANS="spans" NUMBERS="numbers" [ATTEMPT="attempt"] [VERBOSE="verbose"] [REPEAT="repeat"]
##################################################


#/etc/asterisk/extensions.conf(*) 
##################################################
# [from-gsm]
# include => from-gsm-send-sms
# ........
# ........
# [from-gsm-send-sms]
# exten => sms_send_ok, 1, System(touch /tmp/S${SMS_SEND_ID})
# exten => sms_send_failed, 1, System(touch /tmp/F${SMS_SEND_ID})
##################################################


#CLI commands setting sample(*) 
##################################################
# gsm set send sms mode pdu
# gsm set send sms codec utf-8
##################################################


##################################################
#Don't edit the following lines from here
##################################################

APP_NAME=${0#*/}

##################################################
# Print usage
##################################################
usage()
{
	echo "Usage:"
	echo 
	echo "$APP_NAME MESSAGE=\"message\" SPANS=\"spans\" NUMBERS=\"numbers\" [ATTEMPT=\"attempt\"] [VERBOSE=\"verbose\"] [REPEAT=\"repeat\"]"
	echo "Defaults: ATTEMPT=always VERBOSE=3 REPEAT=1"
	echo "Version: $VERSION"
	echo "
# MESSAGE: 
#        The messages contents you want to send.
# SPANS: auto or <span>,<span> ...
#        You can specify a specific span number or set it to auto, then the system will select all available spans to send. 
#        Separate them by comma or space if have more than one destination numbers.
# NUMBERS: <phone number>,<phone number> ...
#        It's the destination number and seperate them by comma or space if you have more than one destination numbers.
# ATTEMPT: always or 0 or 1 or 2 or 3 or 4 ... 
#        The system will attempt to send the failed messages, you can specify how many times do you want to try. 
# VERBOSE: 0 or 1 or 2 or 3 or 4 ...
#        The verbose level of sending message.
# REPEAT: 1 or 2 or 3 or 4 ...
#        How many times of the same message would you like to send.

# Please set the following lines in /etc/asterisk/extensions.conf,
# Otherwise, the $APP_NAME will not be working correctly.

#/etc/asterisk/extensions.conf(*)
##################################################
# [from-gsm]
# include => from-gsm-send-sms
# ........
# ........
# [from-gsm-send-sms]
# exten => sms_send_ok, 1, System(touch /tmp/S${SMS_SEND_ID})
# exten => sms_send_failed, 1, System(touch /tmp/F${SMS_SEND_ID})
##################################################
"

	echo "Example: $APP_NAME MESSAGE=\"hello\" SPANS=auto NUMBERS=\"135xxxxxxxx,136xxxxxxxx\""
	echo
}


##################################################
# Print error
##################################################
error()
{
	echo -ne "\033[40;31m"
	echo "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE"
	echo "Error: $1"
	echo "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE"
	echo -ne "\033[40;37m"
}


##################################################
# Print warning
##################################################
warning()
{
	echo -ne "\033[40;33m"
	echo "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"
	echo "Warning: $1"
	echo "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"
	echo -ne "\033[40;37m"
}


##################################################
# Print string of green fonts
##################################################
echo_green()
{
	echo -e "\033[40;32m${1}\033[40;37m"
}


##################################################
# Check argument
##################################################
check_arg()
{
	if [ x"$MESSAGE" = x"" ]; then
		if [ $VERBOSE -ge 1 ];then
			error "Please set your desire MESSAGE first!"
			echo_green "Add MESSAGE=\"message\" as argument or Set MESSAGE in $APP_NAME file"
		fi
		exit 1
	fi

	if [ x"$SPANS" = x"" ]; then
		if [ $VERBOSE -ge 1 ];then
			error "Please set the sending SPAN number first!"
			echo_green "Add SPANS=\"spans\" as argument or Set SPANS in $APP_NAME file"
		fi
		exit 1
	fi

	if [ x"$NUMBERS" = x"" ]; then
		if [ $VERBOSE -ge 1 ];then
			error "Please set the destination NUMBERS first!"
			echo_green "Add NUMBERS=\"numbers\" as argument or Set NUMBERS in $APP_NAME file"
		fi
		exit 1
	fi


	if [ x"$ATTEMPT" = x"" ]; then
		ATTEMPT=always
	fi

	if [ x"$VERBOSE" = x"" ]; then
		VERBOSE=3
	fi

	if [ x"$REPEAT" = x"" ]; then
		REPEAT=1
	fi
}


##################################################
# Check and decide whether asterisk is running or not ?
##################################################
check_asterisk_run()
{
	if [ ! -S /var/run/asterisk/asterisk.ctl ] ;then
		if [ $VERBOSE -ge 1 ];then
			error "Please start asterisk first!"
		fi
		exit 1
	fi
	
	if ! asterisk -rx "gsm show version" > /dev/null 2>&1 ;then
		if [ $VERBOSE -ge 1 ];then
			error "chan_extra.so must be loaded!"
		fi
		exit 1
	fi
}


#################################################
# Check and decide which codes should be used UCS2 or 7bit ?
#################################################
check_encode()
{
	MSG_LEN=${#MESSAGE}
	MSG_BYTES=`expr length "$MESSAGE"`

	if [ x"$MSG_LEN" = x"$MSG_BYTES" ]; then	#Use 7bit encode
		MSG_MAX=160
	else	#Use UCS2 encode
		MSG_MAX=70
	fi
}


#################################################
# Make a temporary file
#################################################
make_tmp_file()
{
	TMP=`date +%Y%m%d%H%M%S%N`
	TMP_FILE="/tmp/tmp_${TMP}"

	while [ -e $TMP_FILE ]; do
		TMP=`date +%Y%m%d%H%M%S%N`
		TMP_FILE="/tmp/tmp_${TMP}"
	done

	touch $TMP_FILE
	echo -n $TMP_FILE
}


#################################################
#To get valid spans before sending SMS
#################################################
get_VALID_SPANS()
{
	VALID_SPANS=""
	TMP_FILE=`make_tmp_file`
	
	asterisk -rx "gsm show spans" > $TMP_FILE
	while read LINE ; do
		if echo $LINE | grep "Up, Active" > /dev/null 2>&1 ;then
			RET=`echo $LINE | awk '{print $3}'`
			RET=${RET%:}
			if [ x"$RET" != x"" ]; then
				VALID_SPANS="$VALID_SPANS $RET"
			fi
		fi
	done < $TMP_FILE

	\rm -f $TMP_FILE


	if [ x"$VALID_SPANS" = x"" ];then
		if [ $VERBOSE -ge 1 ];then
			error "No valid spans"
		fi
		exit 1
	fi
}


#################################################
#arg 1: span Decide the span(s) is(are) valid or not?
#################################################
is_valid_span()
{
	if [ x"$VALID_SPANS" = x"" ];then
		return 1
	fi

	for i in $VALID_SPANS
	do
		if [ x"$i" = x"$1" ];then
			return 0
		fi
	done

	return 1
}


#################################################
# Gain the span(s) you have#
#################################################
get_SEND_SPANS()
{
	SEND_SPANS=""
	case $SPANS in
		^[aA][uU][tT][oO]$)
		SEND_SPANS=$VALID_SPANS
		;;

		*)
		for i in `echo $SPANS | sed "s/,/ /g"`
		do
			if is_valid_span $i; then
				SEND_SPANS="$SEND_SPANS $i"
			fi
		done
		;;
	esac


	if [ x"$SEND_SPANS" = x"" ];then
		if [ $VERBOSE -ge 1 ];then
			error "Please specify your sending span(s)!"
		fi
		exit 1
	fi
}


#################################################
# arg 1: phone number    Check your destination is valid or not?
#################################################
is_valid_phone()
{
	if  echo $1 | grep '^[+0-9][0-9]\+$' > /dev/null 2>&1 ;then
		return 0
	fi

	if [ $VERBOSE -ge 2 ];then
		warning "\"$1\" is an invalid number."
	fi

	return 1
}


#################################################
# arg Gain the destination number(s) you have
#################################################
get_SEND_NUMBERS()
{
	SEND_NUMBERS=""
	TMP=`echo $NUMBERS | sed "s/,/ /g"`
	for i in $TMP
	do
		if is_valid_phone $i; then
			SEND_NUMBERS="$SEND_NUMBERS $i"
		fi
	done
}


init_SPAN_ARRAY()
{
	SPAN_COUNTS=0
	for i in $SEND_SPANS; do
		SPAN_ARRAY[$SPAN_COUNTS]=$i
		SPAN_COUNTS=`expr $SPAN_COUNTS + 1`
	done
}


OK_FLAG_FILE=`make_tmp_file`
FAILED_FLAG_FILE=`make_tmp_file`
COUNTS_FLAG_FILE=`make_tmp_file`

#################################################
# arg 1: span
# arg 2: dest number
# arg 3: message
# arg 4: attempt times
#################################################
send_sms_block()
{
	if [ $VERBOSE -ge 2 ];then
		echo "Start sending \"$3\" to \"$2\" from span \"$1\""  
	fi

	ID=`date +%Y%m%d%H%M%S%N`
	if ! asterisk -rx "gsm send sms \"$1\" \"$2\" \"$3\" $ID" ;then
		echo >> $FAILED_FLAG_FILE
		return
	fi

	while : ; do
		if [ -f /tmp/S${ID} ]; then
			\rm -f /tmp/S${ID}
			echo >> $OK_FLAG_FILE

			if [ $VERBOSE -ge 1 ];then
				echo_green "Sending \"$3\" to \"$2\" from span \"$1\" was successful."
			fi
			return;
		elif [ -f /tmp/F${ID} ]; then
			\rm -f /tmp/F${ID}
			if echo "$4" | grep '^[aA][lL][wW][aA][yY][sS]$' > /dev/null 2>&1 ;then
				if [ $VERBOSE -ge 2 ];then
					echo "Trying to re-send \"$3\" to \"$2\" from span \"$1\""
				fi
				send_sms_block "$1" "$2" "$3" "$4"
				return
			elif echo "$4" | grep '^[1-9][0-9]*$' > /dev/null 2>&1 ;then
				if [ $VERBOSE -ge 2 ];then
					echo "Trying to re-send \"$3\" to \"$2\" from span \"$1\""
				fi
				send_sms_block "$1" "$2" "$3" "`expr $4 - 1`"
				return
			fi

			echo >> $FAILED_FLAG_FILE

			if [ $VERBOSE -ge 1 ];then
				error "Sending \"$3\" to \"$2\" from span \"$1\" was failed!!!"
			fi
			return
		fi
	done
}



#################################################
# arg 1: span
# arg 2: dest number
#################################################
send_smss()
{
	SEND_SPAN=$1
	SEND_NUM=$2
	
	for (( i=0; i<=$MSG_LEN; i+=$MSG_MAX ))
	do
		for (( j=1; j<=$REPEAT; j++ ))
		do
			SEND_MSG=${MESSAGE:$i:$MSG_MAX}
			echo >> $COUNTS_FLAG_FILE
			send_sms_block "$SEND_SPAN" "$SEND_NUM" "$SEND_MSG" "$ATTEMPT" &
		done
	done 
}



#################################################
## 简体中文 繁體中文        Flag utf-8 codec, Don't edit and don't delete.
#################################################


export LC_ALL=en_US.UTF-8	#Must be set

main()
{
	check_arg
	check_asterisk_run
	get_VALID_SPANS
	get_SEND_SPANS
	get_SEND_NUMBERS
	init_SPAN_ARRAY
	check_encode

	j=0
	for i in $SEND_NUMBERS; do
		if [ $j -eq $SPAN_COUNTS ];then
			j=0
		fi
		send_smss ${SPAN_ARRAY[$j]} $i
		j=`expr $j + 1`
	done

	while : ;do
		OK=`cat $OK_FLAG_FILE 2> /dev/null | wc -l`
		FAILED=`cat $FAILED_FLAG_FILE 2> /dev/null | wc -l `
		TMP1=`expr $OK + $FAILED`
		TMP2=`cat $COUNTS_FLAG_FILE 2> /dev/null | wc -l`
		if [ x"$TMP1" = x"$TMP2" ]; then
			break;
		fi
	done

	if [ x"$TMP2" = x"" -o x"$TMP2" = x"0" ];then
		if [ $VERBOSE -ge 1 ];then
			warning "No any SMS to send."
			exit 1
		fi
	fi

	if [ $VERBOSE -ge 3 ];then
		echo "SMS sending was finished."
	fi

	\rm $OK_FLAG_FILE -f
	\rm $FAILED_FLAG_FILE -f
	\rm $COUNTS_FLAG_FILE -f

	exit 0
}

#Parse argument
while [ $# -gt 0 ]; do
	case ${1%%=*} in
		MESSAGE|message)
			MESSAGE=${1#*=}
			;;
		SPANS|spans)
			SPANS=${1#*=}
			;;
		NUMBERS|numbers)
			NUMBERS=${1#*=}
			;;
		ATTEMPT|attempt)
			ATTEMPT=${1#*=}
			;;
		VERBOSE|verbose)
			VERBOSE=${1#*=}
			;;
		REPEAT|repeat)
			REPEAT=${1#*=}
			;;
			*)
			usage
			exit 1
			;;
	esac
	shift
done

main

