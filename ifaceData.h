
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#ifndef PTHREAD_INCLUDED
 #define  PTHREAD_INCLUDED
 #include <pthread.h>
#endif


#ifndef MYSQL_INCLUDED
 #define  MYSQL_INCLUDED
#include <mysql/mysql.h>
#endif


#define			MAXBUF				200
#define			MAXAVGBUF			10

#define     MAX_DEVICES         10000
#define     MAX_INTERFACES      30000
#define     MAX_SHM_QUEUE_SIZE  2000

#define     MAX_WORKERS       5

#define			TELNET_ACCESS		  1
#define			SSH_ACCESS			  2

// CLI_ACC   access types on CLI
#define			CLI_ACCESS_TELNET		1
#define			CLI_ACCESS_SSH			2

// VENDOR_ID	types 
#define			VENDOR_ID_CISCO			1
#define			VENDOR_ID_AT			2
#define			VENDOR_ID_UBNT			3
#define			VENDOR_ID_CERAGON		4
#define			VENDOR_ID_RAD			5
#define			VENDOR_ID_SERVER		6
#define			VENDOR_ID_DATACOM		7
#define			VENDOR_ID_SAF			8
#define			VENDOR_ID_JUNIPER		9
#define			VENDOR_ID_RAISECOM		10
#define			VENDOR_ID_MIKROTIK		11


// MODEL_ID	types 
#define			CISCO_2921			1
#define			CISCO_2950			2
#define			CISCO_2960			3
#define			CISCO_3550			4
#define			CISCO_3560			5
#define			CISCO_3600			6
#define			CISCO_3925			7
#define			CISCO_6500			8
#define			CISCO_7200			9
#define			CISCO_7600			10
#define			CISCO_AIR1310		11
#define			CISCO_ASA5520		12

#define			AT8000				1
#define			AT8088				2
#define			AT8012				3
#define			AT9408				4

#define			UBNT_NBM5			1
#define			UBNT_NSM5			2
#define			UBNT_RKM5			3
#define			UBNT_UAPINDOOR		4
#define			UBNT_UAPOUTDOOR		5
#define			UBNT_TOUGHSWITCH	6
#define			UBNT_AF25			10
#define			UBNT_AF5			11
#define			UBNT_MCA			12
#define			UBNT_R5AC			13
#define			UBNT_EDGESWITCH		14

#define			CERAGON_IP10E		1

#define			RAD_AIRMUX			1

#define			SERVER_DEBIAN		1

#define			DATACOM_2104		1

#define			SAF_CFIP_PHOENIX	1

#define			JUNIPER_EX4200		1	

#define			RAISECOM_RAX711		1	

#define			MIKROTIK_CRS305_1G_4SPLUS		    1
#define			MIKROTIK_RB211        		      2


// devices table:  'getrunn' field is used to distinguish device brand/model by:

#define			CISCO_TELNET_SUR		1				
#define			CISCO_TELNET_NORTE		2	
#define			CISCO_SSH_NORTE			3				
#define			CISCO_TELNET_CORDOBA	4				
#define			AT8000_NORTE			5				
#define			AT8000_SUR				6				
#define			CISCO_FW				7
#define			CISCO_TACACS			8
#define			AT8000_TACACS			9
#define			CISCO_TACACS_SUR		10
#define			CISCO_SSH_TACACS		11	
#define			UBNT_SSH				12
#define			UBNT_UNIFI_SSH			13
#define			JUNIPER_SSH				14
#define			CERAGON_TELNET			15


#define			ALARM_ERROR			100
#define			ALARM_OK			101

// structure to store interfaces properties
typedef struct interfaceData
  {
  int               deviceId;
  int 				      interfaceId;
  unsigned char			enable;
  unsigned char			status;					// 0: uninitialized, 1: running					
  char				      name[MAXBUF];
  char				      description[MAXBUF];
  char				      peername[MAXBUF];
  struct timeb			last_access;
  long long int			msec, msec_prev;
  long long int			ibytes, ibytes_prev, ibytes_prev_prev ;
  long long int			obytes, obytes_prev, obytes_prev_prev ;
  long int			    oerrors, ierrors;
  double			      ibw, obw;				// input and output bandwidth (bps)
  double			      ibw_a, obw_a;		    // bw measure with 'moving average' ej:  0.2*current + 0.8*previous	
  double			      ibw_b, obw_b;
  double			      ibw_c, obw_c;
  long int			    ibw_buf[MAXAVGBUF], obw_buf[MAXAVGBUF]; 
  char				      file_var_name[MAXBUF];
  unsigned char		 	to_epics;
  int				        alarm_lo;				// alarm threshold (in kbps), minimum acceptable value!
  int				        prio_lo;				// alarm priority for LOW traffic level
  int				        alarm_status;
  short int			    exc_01_ini_h, exc_01_ini_m, exc_01_fin_h, exc_01_fin_m ;	
  short int			    exc_02_ini_h, exc_02_ini_m, exc_02_fin_h, exc_02_fin_m ;	
  char              oidIndex[200];   // OID Index for interface in IF-TABLE
  long long int     cir2;           // commited information rate

  }interfaceData;

// structure to store device properties

typedef struct deviceData
  {
  int 			      deviceId;
  unsigned char 	enable;
  unsigned char 	status;					// 0: uninitialized, 1: running					
  char 		      	access_type;			// 0: none, 1: telnet, 2:ssh
  char 			      name[MAXBUF];
  char 			      ip[MAXBUF];
  char 			      hostname[MAXBUF];
  char 			      ena_prompt[MAXBUF];
  char 			      dis_prompt[MAXBUF];
  time_t      		lastRead;
  time_t          lastPingOK;
  time_t          lastSNMPOK;
  int 			      nInterfaces;
  interfaceData		*ifs;	
  long long int 	ncycle;
  short int 		  getrunn;
  short	int		    cli_acc;
  short	int		    vendor_id;
  short	int		    model_id;
  int             snmp; // 0: no   1,2 snmp version 
  char 			      snmpCommunity[MAXBUF];
  int             snmpVersion;
  int             use64bitsCounters;
  int             snmpConfigured;   // 0: no   1: in process  2: OK 3: ERROR
  int             snmpCaptured;     // 0: no   1: in process  2: OK 3: ERROR

  netsnmp_session         session, *ss;
  netsnmp_pdu             *pdu;
  netsnmp_pdu             *response;
  netsnmp_variable_list   *vars;

  int (*init)( struct deviceData *d);
  int (*authenticate)( struct deviceData *d, int infd, int outfd );
  int (*process)( struct deviceData *d, int infd, int outfd, pid_t child_pid, int dev_id,int iface );
  int (*parse_bw)( struct deviceData *d, int iface, char *b);
  int (*disconnect)( struct deviceData *d, int infd, int outfd );

  }deviceData;

// structure to handle a single  list of devices
typedef struct device_list
  {
  int 				      nDevices;
  deviceData       *d;
  }device_list;

// structure to share Devices information among pool of (forked) workers

typedef struct  devicesShm  {
  pthread_mutex_t   lock;
  deviceData       d[MAX_DEVICES];
  int               nDevices;
  }devicesShm;

// structure to share Interfaces information among pool of (forked) workers

typedef struct  interfacesShm  {
  pthread_mutex_t   lock;
  interfaceData     d[MAX_INTERFACES];
  int               nInterfaces;
  }interfacesShm;

typedef struct  shm_area
    {
    pid_t		  fatherPid;
    pid_t		  sonPid;
    pid_t		  grandsonPid;
    double		ibw,obw;
    double		ibwPrev,obwPrev;
    time_t		lastUpdate;
    int       starting;   // flag to indicate first loop in process

    }shm_area;


extern int      _verbose;

extern int 			_workers;

                                
extern int 			_sample_period;
extern int 			_reconnect_period;

extern int				_speech_prio;	

extern MYSQL       	*_mysql_connection_handler;

extern devicesShm 		*_shmDevicesArea; 
extern interfacesShm	*_shmInterfacesArea; 
extern void 			*_queueInterfaces;	
extern void 			*_queueDevices;	
extern void 			*_queueRedis;	
extern void 			*_queueInfluxDB;	

extern char 			_ICMPSourceInterface[];

extern int 			_deviceToCheck;  // set to verify SNMP info from specific device

extern int 			_useSNMP;

extern int             _verbose;
extern pid_t           _grandsonPID;
extern int             _to_epics;
extern int             _to_files;
extern int             _to_memdb;
extern int             _to_hisdb;
extern int             _send_alarm;
extern char            _process_name[];

extern pthread_mutex_t _threadMutex;
extern char     		  _server[];
extern device_list		_devList;



// util.c
void remove_spaces (char* restrict str_trimmed, const char* restrict str_untrimmed);
int randomDelay(int min, int max);
char *adc_ltrim(char *s);
char *adc_rtrim(char *s);
char *adc_trim(char *s);
int str_firstchar(char *s);
int str_extract(char *s, int n1, int n2, char *ret);
int get_hostname(char *buffer_aux, char *hostname_str);
int read_stream(int fd, char *ptr, int maxbytes);
int wait_for_string(int fd, long int tout, int retries, char *expect, char *buffer, int maxbytes, int delaytime);
int wait_for_string_nonzero(int fd, long int tout, int retries, char *expect, char *buffer, int maxbytes, int delaytime);
int read_wait(int fd, long int tout, int retries, char *buffer, int maxbytes);
int str_normalize(unsigned char *str, int slen);
int send_cmd(int fd, char  *cmd);
int avg_basic(long int * buffer, short ini, short fin, double *avg);
int detect_low_traffic_01(long int * buffer, double *calc);
int detect_normal_traffic_01(deviceData *d, interfaceData *iface);
int in_interval(struct tm *current, short inih, short inim, short finh, short finm);
int str_extract_from(char *s, int c1, char *ret);
int send_control_d(int fd);
void* create_shared_memory(size_t size);

// db.c
int db_connect();
int db_disconnect();
int to_db_mem (interfaceData *d);
int dbread (devicesShm *shmDev, interfacesShm *shmInt);
int dbreadOneDevice (devicesShm *shmDev, interfacesShm *shmInt, int deviceId);
int to_db_hist (deviceData *d);
int delete_from_db_mem ();
int db_keepalive(char *name);
int report_alarm(deviceData *d, interfaceData *iface);
int update_devices_mem(deviceData *d);


char *adc_ltrim(char *s);
char *adc_rtrim(char *s);
char *adc_trim(char *s);

// main.c
int evalAlarm(deviceData *d, interfaceData *iface) ;

// icmp.c
int ping(char *address, int timeOut, long int *roundTripTime);

// snmp.c
int getIndexOfInterfaces( deviceData *d, interfacesShm *shmInt, char *walkFirstOID );
int getIndexOfInterface(long snmpVersion, char *interfaceName, char *walkFirstOID, char *community, char *ipAddress, char *oidIndex );
int getInOutCounters (long snmpVersion, char *community, char *ipAddress, char *inCounterOid, char *outCounterOid, unsigned long long int *inCounter, unsigned long long int *outCounter);
int snmpGetSysDesrc (long snmpVersion, char *community, char *ip, char *result );
int snmpVerifyIfXTable (deviceData *d, unsigned long long int *inCounter );

int 	snmpCheckParameters( deviceData *d, interfacesShm *shmInt );

int snmp_disconnect( deviceData *d, int infd, int outfd );
int snmpCollectBWInfo( deviceData *d, interfaceData *iface);
int snmp_process( deviceData *d, int infd, int outfd, pid_t child_pid, int dev_id, int iface );


// process.c
int verifyDevice (int deviceId);
int retriveBWDataFromFile(  );
int saveToFile( interfaceData *ifs );
int verifyDevice (int deviceId);




//------------------------------------------------------------------------
//------------------------------------------------------------------------
