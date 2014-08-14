#include "gsm_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

static struct cfg_info* cfg_head = NULL;
static int cfg_len = 0;

struct at_cmd_t {
	int id;
	const char* name;
	const char* value;
};

struct at_cmd_t at_cmds[] = {
	{AT_CHECK,  		         "AT_CHECK",          		   "AT"},
	{AT_SET_ECHO_MODE,           "AT_SET_ECHO_MODE",           "ATE0"},
	{AT_SET_CMEE,                "AT_SET_CMEE",                "AT+CMEE=2"},
	{AT_GET_CGMM,                "AT_GET_CGMM",                "AT+CGMM"},
	{AT_GET_CGMI,                "AT_GET_CGMI",                "AT+CGMI"},
	{AT_CHECK_SIGNAL1,           "AT_CHECK_SIGNAL1",           "+CSQ:"},
	{AT_CHECK_SIGNAL2,           "AT_CHECK_SIGNAL2",           "+CSQN:"},
	{AT_GET_SIGNAL1,             "AT_GET_SIGNAL1",             "+CSQ: $COVERAGE,$BER"},
	{AT_GET_SIGNAL2,             "AT_GET_SIGNAL2",             "+CSQN: $COVERAGE,$BER"},
	{AT_ATZ,                     "AT_ATZ",                     "ATZ"},
	{AT_DIAL,                    "AT_DIAL",                    "ATD$D_NUMBER;"},
	{AT_ANSWER,                  "AT_ANSWER",                  "ATA"},
	{AT_HANGUP,                  "AT_HANGUP",                  "ATH"},
	{AT_SEND_SMS_TEXT_MODE,      "AT_SEND_SMS_TEXT_MODE",      "AT+CMGF=1"},
	{AT_SEND_SMS_PDU_MODE,       "AT_SEND_SMS_PDU_MODE",       "AT+CMGF=0"},
	{AT_SEND_PIN,                "AT_SEND_PIN",                "AT+CPIN=\"$PIN_NUMBER\""},
	{AT_CHECK_SMS,               "AT_CHECK_SMS",               "+CMT:"},
	{AT_ERROR,                   "AT_ERROR",                   "ERROR"},
	{AT_NO_CARRIER,              "AT_NO_CARRIER",              "NO CARRIER"},
	{AT_NO_ANSWER,               "AT_NO_ANSWER",               "NO ANSWER"},
	{AT_OK,                      "AT_OK",                      "OK"},
	{AT_UCS2,                    "AT_UCS2",                    "AT+CSCS=\"UCS2\""},
	{AT_GSM,                     "AT_GSM",                     "AT+CSCS=\"GSM\""},
	{AT_SEND_SMS_LEN,            "AT_SEND_SMS_LEN",            "AT+CMGS=$SMS_LEN"},
	{AT_SEND_SMS_DES,            "AT_SEND_SMS_DES",            "AT+CMGS=\"$SMS_DES\""},
	{AT_CMS_ERROR,               "AT_CMS_ERROR",               "+CMS ERROR"},
	{AT_CME_ERROR,               "AT_CME_ERROR",               "+CME ERROR"},
	{AT_SEND_SMS_SUCCESS,        "AT_SEND_SMS_SUCCESS",        "+CMGS:"},
	{AT_CREG,                    "AT_CREG",                    "+CREG:"},
	{AT_CREG0,                   "AT_CREG0",                   "+CREG: 0"},
	{AT_CREG1,                   "AT_CREG1",                   "+CREG: 1"},
	{AT_CREG2,                   "AT_CREG2",                   "+CREG: 2"},
	{AT_CREG3,                   "AT_CREG3",                   "+CREG: 3"},
	{AT_CREG4,                   "AT_CREG4",                   "+CREG: 4"},
	{AT_CREG5,                   "AT_CREG5",                   "+CREG: 5"},
	{AT_CREG10,                  "AT_CREG10",                  "+CREG: 1,0"},
	{AT_CREG11,                  "AT_CREG11",                  "+CREG: 1,1"},
	{AT_CREG12,                  "AT_CREG12",                  "+CREG: 1,2"},
	{AT_CREG13,                  "AT_CREG13",                  "+CREG: 1,3"},
	{AT_CREG14,                  "AT_CREG14",                  "+CREG: 1,4"},
	{AT_CREG15,                  "AT_CREG15",                  "+CREG: 1,5"},
	{AT_GET_MANUFACTURER,        "AT_GET_MANUFACTURER",        "AT+CGMI"},
	{AT_GET_SMSC,                "AT_GET_SMSC",                "AT+CSCA?"},
	{AT_GET_VERSION,             "AT_GET_VERSION",             "AT+CGMR"},
	{AT_GET_IMEI,                "AT_GET_IMEI",                "AT+CGSN"},
	{AT_ASK_PIN,                 "AT_ASK_PIN",                 "AT+CPIN?"},
	{AT_PIN_READY,               "AT_PIN_READY",               "+CPIN: READY"},
#ifdef AUTO_SIM_CHECK
	{AT_SIM_DECTECT,             "AT_SIM_DECTECT",             "AT+QSIMDET=1,1"},/*for auto check sim status, Makes Add 2012-7-12 16:08*/
	{AT_PIN_NOT_READY,           "AT_PIN_NOT_READY",           "+CPIN: NOT READY"},/*for auto check sim status, Makes Add 2012-7-12 16:08*/
#endif
	{AT_PIN_SIM,                 "AT_PIN_SIM",                 "+CPIN: SIM PIN"},
	{AT_IMSI,                    "AT_IMSI",                    "AT+CIMI"},
	{AT_SIM_NO_INSERTED,         "AT_SIM_NO_INSERTED",         "+CME ERROR: SIM not inserted"},
	{AT_MOC_ENABLED,             "AT_MOC_ENABLED",             "AT+QMOSTAT=1"},
	{AT_SET_SIDE_TONE,           "AT_SET_SIDE_TONE",           "AT+QSIDET=0"},
	{AT_CLIP_ENABLED,            "AT_CLIP_ENABLED",            "AT+CLIP=1"},
	{AT_DTR_WAKEUP,              "AT_DTR_WAKEUP",              "AT"},
	{AT_RSSI_ENABLED,            "AT_RSSI_ENABLED",            "AT+CREG=1"},
	{AT_SET_NET_URC,             "AT_SET_NET_URC",             "AT+QEXTUNSOL=\"SQ\",1"},
	{AT_ASK_NET,                 "AT_ASK_NET",                 "AT+CREG?"},
	{AT_NET_OK,                  "AT_NET_OK",                  "AT+COPS=3,0"},
	{AT_SMS_SET_CHARSET,         "AT_SMS_SET_CHARSET",         "AT+CNMI=2,2,0,0,0"},
	{AT_ASK_NET_NAME,            "AT_ASK_NET_NAME",            "AT+COPS?"},
	{AT_CHECK_NET,               "AT_CHECK_NET",               "+COPS:"},
	{AT_NET_NAME,                "AT_NET_NAME",                "AT+CSQ"},
	{AT_INCOMING_CALL,           "AT_INCOMING_CALL",           "+CLIP:"},
	{AT_GET_CID,                 "AT_GET_CID",                 "+CLIP: \"$CID_NUMBER\""},
	{AT_RING,                    "AT_RING",                    "RING"},
#ifdef AUTO_SIM_CHECK
	{AT_CALL_READY,              "AT_CALL_READY",              "Call Ready"},/*for auto check sim status, Makes Add 2012-7-12 16:08*/
#endif
	{AT_CALL_INIT,               "AT_CALL_INIT",               "AT+QAUDCH=1"},
	{AT_CALL_PROCEEDING,         "AT_CALL_PROCEEDING",         "AT+CLVL?"},
	{AT_CALL_STATUS,         	 "AT_CALL_STATUS",         	   "AT+CLCC"},
	{AT_MO_CONNECTED,            "AT_MO_CONNECTED",            "MO CONNECTED"},
	{AT_NO_DIALTONE,             "AT_NO_DIALTONE",             "NO DIALTONE"},
	{AT_BUSY,                    "AT_BUSY",                    "BUSY"},	
	{AT_CMIC,                    "AT_CMIC",                    "AT+QMIC=0,$MICROPHONE_LEVEL"},	/*Makes Add 2012-4-5 17:44*/
	{AT_CLVL,                    "AT_CLVL",                    "AT+CLVL=$VOLUME_LEVEL"},	/*Makes Add 2012-4-5 17:44*/
	{AT_MODE,                    "AT_MODE",                    "ATX4"},	/*Makes Add 2012-4-10 15:50*/
	{AT_CMUX,                    "AT_CMUX",                    "AT+CMUX=0"},	/*Makes Add 2012-4-10 15:50*/
	{AT_SEND_USSD,               "AT_SEND_USSD",               "AT+CUSD=1,\"$USSD_CODE\""},
	{AT_CHECK_USSD,              "AT_CHECK_USSD",              "+CUSD:"},
	{AT_VTS,                     "AT_VTS",                     "AT+VTS=$DTMF"}
};

static void trim( char *str )
{
	char *copied, *tail = NULL;
	if ( str == NULL )
		return;

	for( copied = str; *str; str++ ) {
		if ( *str != ' ' && *str != '\t' ) {
			*copied++ = *str;
			tail = copied;
		} else {
			if ( tail )
				*copied++ = *str;
		}
	}

	if ( tail )
		*tail = 0;
	else
		*copied = 0;

	return;
}


static int is_config(const char* file)
{
	int len;

	if(file == NULL) return 0;
	
	len = strlen(file);
	
	if( len < sizeof(".conf")/sizeof(char) ) return 0;
	
	if(strcmp(file+len+1-sizeof(".conf"),".conf") != 0) return 0;
	
	
	return 1;
}

static FILE* load_file(char* file_name)
{
	FILE* fd;
	if((fd=fopen(file_name,"r")) == NULL) {
		printf("Can't open %s\n",file_name);
	}
		
	return fd;
}

static int close_file(FILE* fd)
{
	if( fd == NULL )
		return 0;
	
	fclose(fd);
	return 1;
}


static int parse_file(struct cfg_info* cfg, FILE* fd)
{
#define READ_SIZE 1024
	char buf[READ_SIZE];
	char name[512];
	char value[512];
	int split;
	int len;
	int i;
	int j;
	int out;

	if( cfg == NULL ||  fd == NULL )
		return 0;
	
	while(fgets(buf,READ_SIZE,fd)) {
		split = -1;
		j = -2;
		out = 0;
		len = strlen(buf);
		for( i=0; i<len; i++ ) {
			switch( buf[i] ) {
				case '#':
				case '\r':
				case '\n':
					out=1;
					break;
				case '=':
					j=i;
					break;
				case '>':
					if( j+1 == i ) split = i;
					break;
			}
			if(out)
				break;
		}
		if( split != -1) {
			strncpy(name,buf,split-1);
			name[split-1] = '\0';
			trim(name);
			int id;
			if( (id=get_at_cmds_id(name)) != -1) {
				strncpy(value,buf+split+1,i-split-1);
				value[i-split-1] = '\0';
				trim(value);
				if( cfg->at_cmds[id] != NULL ) {
					free(cfg->at_cmds[id]);
					cfg->at_cmds[id] = NULL;
				}
				cfg->at_cmds[id] = (char*)malloc(strlen(value)+1);
				strcpy(cfg->at_cmds[id],value);	
			} else if( strcmp(name,"MODULE") == 0 ) {
				strncpy(value,buf+split+1,i-split-1);
				value[i-split-1] = '\0';
				trim(value);
				strcpy(cfg->module_name,value);
			}
		}
	}
	
	return 1;
}

static const char* get_defvalue(int id)
{
	int i;
	
	if( id<0 || id>=AT_CMDS_SUM )
		return NULL;
	
	for( i=0; i<AT_CMDS_SUM; i++ ) {
		if( id == at_cmds[i].id )
			return at_cmds[i].value;
	}
	
	return NULL;
}

char* str_check_symbol(const char* src, char* buf, int len)
{
	if(src == NULL ||
       buf == NULL ||
       len <= 0 ) {
        return NULL;
    }
	
	const char* pSrc;
	char* pBuf;
	
	for(pSrc = src,pBuf=buf; *pSrc && (pBuf-buf)<len-1; pSrc++,pBuf++) {
		if( *pSrc == '%' ) {
			*pBuf++ = '%';
            *pBuf = '%';
		} else {
            *pBuf = *pSrc;
        }
	}
    
    *pBuf ='\0';
    
    return buf;
}

char *str_replace(const char *src, char* buf, int len, const char *oldstr, const char *newstr)
{
    char *needle;
    char *tmp;
    const char *replaced;
    
    if(src == NULL ||
       buf == NULL ||
       len <= 0    ||
       oldstr == NULL ||
       newstr == NULL) {
        return NULL;
    }
    
    if (strlen(oldstr) == strlen(newstr) && strcmp(oldstr, newstr) == 0) { 
        return NULL;
    }

	strncpy(buf,src,len);
    replaced = src;
    while ((needle = strstr(replaced, oldstr))) {
        tmp = (char*)malloc(strlen(replaced) + (strlen(newstr) - strlen(oldstr)) +1);
        strncpy(tmp, replaced, needle-replaced);
        tmp[needle-replaced] = '\0';
        strcat(tmp, newstr);
        strcat(tmp, needle+strlen(oldstr)); 
        strncpy(buf,tmp,len);
        replaced = buf;
        free(tmp); 
    }
    
    return buf;
} 

int get_at_cmds_id(char* name)
{
	int i;
	for(i=0; i<AT_CMDS_SUM; i++) {
		if( strcmp(name,at_cmds[i].name) == 0 )
			return at_cmds[i].id;
	}
	
	return -1;
}

const char* get_at(int module_id, int cmds_id)
{
	if( cmds_id >= AT_CMDS_SUM ) return NULL;
	
	if( module_id > cfg_len || module_id < 0 )
		return get_defvalue(cmds_id);
	
	if( NULL == cfg_head )
		return get_defvalue(cmds_id);
	
	if( NULL == cfg_head[module_id].at_cmds[cmds_id] ) 
		return get_defvalue(cmds_id);

	return cfg_head[module_id].at_cmds[cmds_id];
}

static int get_coverage(int module_id, char* h, int cmds_id)
{
	char buf[1024];
	int coverage=-1, ber=-1;
	
	if(!str_check_symbol(get_at(module_id, cmds_id),buf,sizeof(buf)))
		return -1;
	
	if(!str_replace(buf,buf,sizeof(buf),"$COVERAGE","%d"))
		return -1;
	
	if(!str_replace(buf,buf,sizeof(buf),"$BER","%d"))
		return -1;
			
	sscanf(h, buf, &coverage, &ber);
	
	return coverage;
}

int get_coverage1(int module_id, char* h)
{
	return get_coverage(module_id,h, AT_GET_SIGNAL1);
}

int get_coverage2(int module_id, char* h)
{
	return get_coverage(module_id,h, AT_GET_SIGNAL2);
}

char* get_dial_str(int module_id, const char* number, char* buf, int len)
{
	char temp[1024];
	
	buf[0] = '\0';
	
	if(!str_check_symbol(get_at(module_id, AT_DIAL),temp,sizeof(temp)))
		return NULL;
	
	if(!str_replace(temp,temp,sizeof(temp),"$D_NUMBER","%s"))
		return NULL;
	
	snprintf(buf,len,temp,number);
	
	return buf;
}

char* get_ussd_str(int module_id, const char* ussd_code, char* buf, int len)
{
	char temp[1024];
	
	buf[0] = '\0';
	
	if(!str_check_symbol(get_at(module_id, AT_SEND_USSD),temp,sizeof(temp)))
		return NULL;
	
	if(!str_replace(temp,temp,sizeof(temp),"$USSD_CODE","%s"))
		return NULL;
	
	snprintf(buf,len,temp,ussd_code);
	
	return buf;
}

char* get_pin_str(int module_id, const char* pin, char* buf, int len)
{
	char temp[1024];
	
	buf[0] = '\0';
		
	if(!str_check_symbol(get_at(module_id, AT_SEND_PIN),temp,sizeof(temp)))
		return NULL;
	
	if(!str_replace(temp,temp,sizeof(temp),"$PIN_NUMBER","%s"))
		return NULL;
	
	snprintf(buf,len,temp,pin);
	
	return buf;
}

char* get_dtmf_str(int module_id, char digit, char* buf, int len)
{
	char temp[1024];
	
	buf[0] = '\0';
	
	if(!str_check_symbol(get_at(module_id, AT_VTS),temp,sizeof(temp)))
		return NULL;
	
	if(!str_replace(temp,temp,sizeof(temp),"$DTMF","%c"))
		return NULL;
	
	snprintf(buf,len,temp,digit);
	
	return buf;
}

char* get_sms_len(int module_id, int sms_len, char* buf, int len)
{
	char temp[1024];
	
	buf[0] = '\0';
	
	if(!str_check_symbol(get_at(module_id, AT_SEND_SMS_LEN),temp,sizeof(temp)))
		return NULL;
	
	if(!str_replace(temp,temp,sizeof(temp),"$SMS_LEN","%d"))
		return NULL;
	
	snprintf(buf,len,temp,sms_len);
	
	return buf;
}

char* get_sms_des(int module_id, const char* des, char* buf, int len)
{
	char temp[1024];
	
	buf[0] = '\0';
		
	if(!str_check_symbol(get_at(module_id, AT_SEND_SMS_DES),temp,sizeof(temp)))
		return NULL;
	
	if(!str_replace(temp,temp,sizeof(temp),"$SMS_DES","%s"))
		return NULL;
	
	snprintf(buf,len,temp,des);
	
	return buf;
}

char* get_cid(int module_id, const char* h, char* buf, int len)
{
	char temp[1024];
	
	if(!str_check_symbol(get_at(module_id, AT_GET_CID),temp,sizeof(temp)))
		return NULL;
	
	if(!str_replace(temp,temp,sizeof(temp),"$CID_NUMBER","%64s"))
		return NULL;

	char *endflag = NULL;
	if(sscanf(h, temp, buf)) {
		endflag = strchr(buf, '"');
		if (endflag)
			*endflag = '\0';
	}
	
	return buf;
}

static void init_cfg(struct cfg_info* cfg,char *file_path)
{
	int i;
		
	if( NULL == cfg )
		return;
	memset(cfg->file_path,0,sizeof(cfg->file_path));
	memset(cfg->module_name,0,sizeof(cfg->module_name));
	memcpy(cfg->file_path,file_path,sizeof(cfg->file_path));
	for(i=0; i<AT_CMDS_SUM; i++) {
		cfg->at_cmds[i] = NULL;
	}
}

int init_cfg_file(void)
{
#define CFGS_DIR "/etc/openvox/opvxg4xx/modules"
	struct dirent* dirp;
	DIR *dp;
	struct stat statbuf;
	char file_name[256];
	
	struct file_list {
		char file_name[256];
		struct file_list *next;
	} *list_head = NULL;
	int list_len = 0;
	
	if((dp=opendir(CFGS_DIR)) == NULL)
		return 0;

	while (NULL != (dirp=readdir(dp))) {
		
		if( strcmp(dirp->d_name,".")==0 || strcmp(dirp->d_name,"..")==0 )
			continue;
		
		snprintf(file_name,sizeof(file_name),"%s/%s",CFGS_DIR,dirp->d_name);
		
		lstat(file_name, &statbuf);
		if(!S_ISREG(statbuf.st_mode))
			continue;

		if(is_config(file_name)) {
			struct file_list* cfg_file = (struct file_list*)malloc(sizeof(struct file_list));
			strncpy(cfg_file->file_name,file_name,sizeof(file_name));
			cfg_file->next = NULL;
			
			if( list_head ) {
				struct file_list* l;
				l = list_head;
				while( l->next != NULL ) {
					l = l->next;
				}
				l->next = cfg_file;
			} else {
				list_head = cfg_file;
			}
			
			++list_len;
		}
	}
	
	closedir(dp);
	
	if( list_head ) {
		struct file_list* l;
		struct file_list* tmp;
		int i = 0;
		
		cfg_head = (struct cfg_info*)malloc(sizeof(struct cfg_info)*list_len);
		cfg_len = list_len;
		
		l = list_head;
		while( l != NULL && i<cfg_len) {
			FILE* fd;
			fd = load_file(l->file_name);
			init_cfg(&cfg_head[i],l->file_name);
			parse_file(&cfg_head[i],fd);
			close_file(fd);
			++i;

			tmp = l;
			l = l->next;
			free(tmp);
		}
	}
	return 1;
}

int destroy_cfg_file(void)
{
	int i,j;
	if( cfg_head == NULL )
		return 0;
		
	for( i=0; i< cfg_len; i++ ) {
		for(j=0; j<AT_CMDS_SUM; j++) {
			free(cfg_head[i].at_cmds[j]);
			cfg_head[i].at_cmds[j] = NULL;
		}
	}
	
	free(cfg_head);
	cfg_head = NULL;
	
	return 1;
}

void gsm_set_module_id(int *const module_id, const char *name)
{
	int i;
	for( i=0; i<cfg_len; i++ ) {
		if( 0 == strcmp(cfg_head[i].module_name,name) ) {
			*module_id = i;
		}
	}
}

const char* gsm_get_module_name(int module_id)
{
	if( module_id < 0 || module_id > cfg_len || NULL == cfg_head) {
		return "Unknow module";
	}
	
	return cfg_head[module_id].module_name;
}

