/* 
  File:   main.c
  Author:  rfurch
 
  TODO:
  - revisar que pasa si se cambia el tipo de acceso en BD (el proceso podria suicidarse!)  	
  - ver el funcionamiento a largo plazo tanto del lado cliente como servidor (router)
*/


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <time.h>
#include <pty.h>
#include <sys/time.h>
#include <sys/select.h>
#include <stdio.h>
#include <ctype.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <syslog.h>
#include <sys/prctl.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <signal.h>
#include <pty.h>
#include <math.h>
#include <mysql/mysql.h>

#include <sys/mman.h> 

#include <sys/timeb.h>
#include <getopt.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <hiredis.h>

#include "ifaceData.h"                                                                                                     
#include "shm_q.h"

int             _verbose=0;

pid_t           _fatherPID=0;
pid_t           _sonPID=0;
pid_t           _grandsonPID=0;

int 			_workers = MAX_WORKERS;

int             _to_epics=0;		// register into EPICS
int             _to_files=0;		// send data to CSV Files (usually  /data/bw)
int             _to_memdb=0;		// write to DB
int             _to_hisdb=0;        // write to historic DB
int             _send_alarm=0;      // send LOW Traffic Alarms                                    
char            _process_name[100];
int 			_sample_period=30;   // default sample time in seconds
int 			_reconnect_period=10;   // default reconnec time in minutes

int				_speech_prio=8;		// minimum priority value to trigger speech messages

MYSQL       	*_mysql_connection_handler=NULL;

devicesShm 		*_shmDevicesArea = NULL; 
interfacesShm	*_shmInterfacesArea = NULL; 
void 			*_queueInterfaces = NULL;	
void 			*_queueDevices = NULL;	
void 			*_queueRedis = NULL;	
void 			*_queueInfluxDB = NULL;	

char 			_ICMPSourceInterface[200] = "lo";

int 			_deviceToCheck = -1;  // set to verify SNMP info from specific device

int 			_useSNMP = 0;

//------------------------------------------------------------------------                                   
//------------------------------------------------------------------------

void printUsage(char *prgname) //nada
{
printf ("============================================================\n");
printf ("Trend file generator\n\n");
printf ("USAGE: %s [options]\n", prgname);
printf ("Options:\n");
printf ("  -h, --help   Print this help \n");
printf ("  -v   Verbose mode\n");
printf ("  -c DEVICE_ID  Check specific device (for SNMP, vars, etc) and exit. \n");
printf ("  -b INTERFACE_NAME  Source interface for ICMP (default: lo). \n");
printf ("  -e   send data to epics (deactivated by default) \n");
printf ("  -H   send data to historic DB  (deactivated by default) \n");
printf ("  -m   send data to MEMORY DB  (deactivated by default) \n");
printf ("  -f   send data to FILES  (deactivated by default) \n");
printf ("  -a   send alarms  (deactivated by default) \n");
printf ("  -d   database server  (default dbserver01) \n");
printf ("  -s   sample period (default 30 seconds) \n");
printf ("  -S, --snmp  try to capture values via SNMP (Overrides devices table parameter!) \n");
printf ("  -r   reconnect period (default 10 minutes) \n");
printf ("  -p   minimum priority to trigger voice messages (default=8) \n");
printf ("  -w   concurrent workers (default=%i) \n", MAX_WORKERS);
printf ("============================================================\n\n");
fflush(stdout);
}

//------------------------------------------------------------------------

// basic program argument parsing

int parseArguments(int argc, char *argv[]) {

int     		opt=0;
int			 	help_flag = 0;

struct option longopts[] = {
	{ "help", no_argument, &help_flag, 1 },
	{ "snmp", optional_argument, NULL, 'S' },
	{ 0 }
};

while ((opt = getopt_long(argc, argv, "c:s:d:r:w:ehfmvaS", longopts, 0)) != -1)
    {
    switch (opt)
        {
        case 'v':
            _verbose++;
        break;

        case 'e':
            _to_epics=1;
        break;

        case 'w':
            _workers = atoi(optarg);
        break;

        case 'c':
            _deviceToCheck = atoi(optarg);
        break;

        case 'f':
            _to_files=1;
        break;

        case 'm':
            _to_memdb=1;
        break;

        case 'H':
            _to_hisdb=1;
        break;

        case 'a':
            _send_alarm=1;
        break;

        case 'p':
      		if(optarg)
          	_speech_prio=atoi(optarg);
        break;

        case 's':
      		if(optarg)
				_sample_period = (atoi(optarg) >= 1) ? atoi(optarg) : 30;
        break;

        case 'S':
			_useSNMP = 1;
        break;

        case 'r':
      		if(optarg)
				_reconnect_period = (atoi(optarg) >= 5) ? atoi(optarg) : 1;
        break;

        case 'd':
	    if(optarg)
        	strcpy(_server, optarg);
        break;

        case 'b':
	    if(optarg)
        	strcpy(_ICMPSourceInterface, optarg);
        break;

		case 'h':
        default: /* ’?’ */
            printUsage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

if ( _to_epics==0 && _to_files==0 && _to_memdb==0 && _to_hisdb==0 && _deviceToCheck<0 ) {
  printf("\n\n ATENTION:   collected data will not be recorded to DB, MEM, FILE, etc!  \n\n");
  printUsage(argv[0]);
  exit(0);
  } 

sprintf(_process_name, "trafficSNMP");

return 1;
}

//------------------------------------------------------------------------

void mainLoop()  {

int 				i=0; 
interfaceData		ifaceData;
deviceData			devData;
static time_t		old_db_time=0;
time_t				current_time=0;
int 				comm_error=0;

while(1) {

	if (_verbose > 3) 
		printf("\n           mainLoop ... ");

	if (_verbose > 6)
		for (i=0 ; i<_shmDevicesArea->nDevices ; i++)
			printf("\n            device %i snmpConfigured: %i snmpCaptured: %i ", _shmDevicesArea->d[i].deviceId, _shmDevicesArea->d[i].snmpConfigured, _shmDevicesArea->d[i].snmpCaptured);

	// send interface (traffic)  data to DB	
	while (shmQueueGet(_queueInterfaces, &ifaceData) == 1) {
		to_db_mem (&ifaceData);
		}

	// send device data to db (last ping, last capture, etc)
	while (shmQueueGet(_queueDevices, &devData) == 1) {
		//printf("\n  --- saco %li  devid %i ", devData.lastPingOK, devData.deviceId );
		//fflush(stdout);
		update_devices_mem(&devData);
		}


    // preventive disconnection from DB
    if ( ((current_time = time(NULL)) > (old_db_time + 300)) || comm_error ) {
  	  old_db_time = current_time;
	  db_disconnect();
	  db_connect();
	  }

	fflush(stdout);
	sleep (1);
	}
}

//------------------------------------------------------------------------

// worker to send data to InfluxDB 

void workerInfluxDB()  {

interfaceData		ifaceData;
//static time_t		old_db_time=0;
//time_t				current_time=0;

//old_db_time = current_time = time(NULL);

if (_verbose > 1) {
	printf("\n Worker %i (InfluxDB) start!", getpid());
	fflush(stdout);
	}	

while(1) {

	// send interface (traffic)  data to DB	
	while (shmQueueGet(_queueInfluxDB, &ifaceData) == 1) {

	}

	fflush(stdout);
	sleep (1);
	}
}

//------------------------------------------------------------------------

// worker to send data to REDIS (mem cache) 

void workerRedis()  {

interfaceData		ifaceData;
redisContext 		*c = NULL;
redisReply 			*reply = NULL;
static time_t		old_db_time=0;
time_t				current_time=0;
int 				comm_error=0;

old_db_time = current_time = time(NULL);

if (_verbose > 1) {
	printf("\n Worker %i (redis) start!", getpid());
	fflush(stdout);
	}	

// connect to redis. In case of failure we will attempt later
struct timeval timeout = { 1, 500000 }; // 1.5 seconds
c = redisConnectWithTimeout("127.0.0.1", 6379, timeout);

    if (c == NULL || c->err) {
        if (c) {
            printf("REDIS Connection error: %s\n", c->errstr);
            redisFree(c);
			}
		else 
            printf("Connection error: can't allocate redis context\n");
    	}

while(1) {

	// send interface (traffic)  data to DB	
	while (shmQueueGet(_queueRedis, &ifaceData) == 1) {
		//  set in memory HASH 

		// set individual hashes
        redisAppendCommand(c,
        "HSET devices_bw:%06i ifID %i devID %i "
		"name %s descr %b "
        "ibw %.2f obw %.2f ibw_a %.2f obw_a %.2f "
        "ibw_b %.2f obw_b %.2f ibw_c %.2f obw_c %.2f "
        "file: %b "
		"deviceName %b "
		"lastICMP %li lastSNMP %li snmpDeviceOK %i snmpOIDOk %i ",
		"cirCom %lli cirTec %lli",
         ifaceData.interfaceId, ifaceData.interfaceId, ifaceData.deviceId,
		 ifaceData.name, ifaceData.peername, strlen(ifaceData.peername),
         ifaceData.ibw, ifaceData.obw,ifaceData.ibw_a, ifaceData.obw_a,
         ifaceData.ibw_b, ifaceData.obw_b,ifaceData.ibw_c, ifaceData.obw_c,
         ifaceData.file_var_name, strlen(ifaceData.file_var_name),
         ifaceData.deviceName, strlen(ifaceData.deviceName),
		 ifaceData.lastPingOK, ifaceData.lastSNMPOK, ifaceData.snmpDeviceOK, ifaceData.snmpOIDOk,
		 ifaceData.cirCom, ifaceData.cirTec );

		if (redisGetReply(c, (void *) &reply) != REDIS_OK) 
			printf("\n --REDIS ERROR!  (%s) ",  reply->str );			
		
        freeReplyObject(reply);

		// record also in a SET (kind of index)
        reply = redisCommand(c, "SADD devices_bw_list devices_bw:%06i", ifaceData.interfaceId );
        freeReplyObject(reply);
		}

	fflush(stdout);
	sleep (1);

    // preventive disconnection from DB
    if ( ((current_time = time(NULL)) > (old_db_time + 300)) || comm_error ) {
  		old_db_time = current_time;
		// Disconnects and frees the context 
    	redisFree(c);	  

		struct timeval timeout = { 1, 500000 }; // 1.5 seconds
		c = redisConnectWithTimeout("127.0.0.1", 6379, timeout);

			if (c == NULL || c->err) {
				if (c) {
					printf("REDIS Connection error: %s\n", c->errstr);
					redisFree(c);
					}
				else 
					printf("Connection error: can't allocate redis context\n");
			}
		}
	}
}

//------------------------------------------------------------------------

// pre - forked worker to SEND ICMP (bulk, flooding ICMP)

void workerSendICMP()  {

sleep( 5 );  

while (1) {
	sendMultiPing(_shmDevicesArea);
	sleep (5);
	}
}

//------------------------------------------------------------------------

// pre - forked worker to Receive ICMP REPLY and update DEVICES

void workerReceiveICMP()  {

sleep( 3 );  

while (1) {
	receiveMultiPing(_shmDevicesArea, _shmInterfacesArea);
	sleep (5);
	}
}

//------------------------------------------------------------------------

// pre - forked worker to check devices under error conditions 
// common case: those devices thet were off - line at
// process startup time, and became available later... 

void workerCheckOfflineDevices()  {
int 		devIndex = 0;
int 		counter = 0 ;

sleep( 60 );  

while (1) {

	counter = (counter < 1000) ? (counter + 1) : 0;
	
	// check for devices in ERROR condition every 5 minutes
	if ( (counter % 30 ) == 0) {
		pthread_mutex_lock (& (_shmDevicesArea->lock));
		for (devIndex = 0 ; devIndex<_shmDevicesArea->nDevices ; devIndex++ ) {
			if (_shmDevicesArea->d[devIndex].enable > 0 &&  _shmDevicesArea->d[devIndex].snmpConfigured == 3) {
				if ( (time(NULL) - _shmDevicesArea->d[devIndex].lastPingOK) < 100 ) {
					if (_verbose > 1) {
						printf("\n Worker workerCheckOfflineDevices: device (%s, %s) is reachable now! Set state to '0'", _shmDevicesArea->d[devIndex].name, _shmDevicesArea->d[devIndex].ip);
						fflush(stdout);
						}
				_shmDevicesArea->d[devIndex].snmpConfigured = 0;
				}
			}
		}		
		pthread_mutex_unlock (& (_shmDevicesArea->lock));
		}

	// send Interfaces IN ERROR to redis for error debug from WEB every 60 sec
	if ( (counter % 6 ) == 0) {
		int iface=0;

	    for (iface = 0 ; iface < _shmInterfacesArea->nInterfaces ; iface++)   {
    	    if (_shmInterfacesArea->d[iface].enable > 0 && (_shmInterfacesArea->d[iface].snmpOIDOk<1 ||
			_shmInterfacesArea->d[iface].snmpDeviceOK<1 ||
			( (time(NULL) - _shmInterfacesArea->d[iface].lastSNMPOK ) > 100 ) ||
			( (time(NULL) - _shmInterfacesArea->d[iface].lastPingOK ) > 100 ) ) ) {

			if (_verbose > 1) {
				printf("\n Sending data for interface %s to REDIS (IN ERROR):", _shmInterfacesArea->d[iface].peername);
				fflush(stdout);
				}
			// send data to REDIS.  Other process will take care, to avoid blocking...
			if ( shmQueuePut(_queueRedis, (void *)&(_shmInterfacesArea->d[iface])) != 1 )
				fprintf(stderr, "\n ERROR on _queueRedis  ! ");				
				}
			}	
		}			

	sleep (10);
	}
}

//------------------------------------------------------------------------

// pre - forked worker

void workerRun()  {
int 	i=0, iface=0;
int 	devToInitFound=0;
int 	devToMeasureFound=0;
time_t	t=0;
int 	snmpCaptureOk=0;   // flag to detect changes un traffic counters as 'valid reading' 

sleep(10 + rand()%20);  

if (_verbose > 1) {
	printf("\n Worker %i start!", getpid());
	fflush(stdout);
	}	

while (1) {
	// check for devices not yet initialized

	pthread_mutex_lock (& (_shmDevicesArea->lock));
	for (i=0,devToInitFound=-1 ; i<_shmDevicesArea->nDevices ; i++) {
		if (_shmDevicesArea->d[i].enable > 0 &&  _shmDevicesArea->d[i].snmpConfigured == 0) {
			_shmDevicesArea->d[i].snmpConfigured = 1 ; // mark as  'in configuration process' 
			devToInitFound = i;
			break;
			}
		}
	pthread_mutex_unlock (& (_shmDevicesArea->lock));

	if (devToInitFound > -1)	{ // device requires SNMP initialization 

		if (_verbose > 1) {
			printf("\n Worker %i initializing SNMP for device (%s, %s)", getpid(), _shmDevicesArea->d[devToInitFound].name, _shmDevicesArea->d[devToInitFound].ip);
			fflush(stdout);
			}

		if ( snmpCheckParameters( &(_shmDevicesArea->d[devToInitFound]), _shmInterfacesArea ) == 0 ) {    // OK!
			pthread_mutex_lock (& (_shmDevicesArea->lock));
			_shmDevicesArea->d[devToInitFound].snmpConfigured = 2 ; // mark as  'configured' 
			pthread_mutex_unlock (& (_shmDevicesArea->lock));
			if (_verbose > 1) 
				printf("\n Worker %i:  device (%s, %s) snmpCheckParameters OK!", getpid(), _shmDevicesArea->d[devToInitFound].name, _shmDevicesArea->d[devToInitFound].ip);
			}
		else  {
			pthread_mutex_lock (& (_shmDevicesArea->lock));
			_shmDevicesArea->d[devToInitFound].snmpConfigured = 3 ; // mark as 'ERROR' 
			pthread_mutex_unlock (& (_shmDevicesArea->lock));
			printf("\n Worker %i:  device [%i] (%s, %s) snmpCheckParameters ERROR!", getpid(), _shmDevicesArea->d[devToInitFound].deviceId, _shmDevicesArea->d[devToInitFound].name, _shmDevicesArea->d[devToInitFound].ip);
			fflush(stdout);				
			}
		}
	else {   // all devices configured / initialized, we can now get traffic counters

		pthread_mutex_lock (& (_shmDevicesArea->lock));
		t = time(NULL);
		// we check devices: enables, SNMP configured, not already mesured and sample time OK 
		for (i=0,devToMeasureFound=-1 ; i<_shmDevicesArea->nDevices ; i++) {
			if (_shmDevicesArea->d[i].enable > 0 &&  _shmDevicesArea->d[i].snmpConfigured == 2 && _shmDevicesArea->d[i].snmpCaptured == 0) {
				if ( t >= (_shmDevicesArea->d[i].lastRead + _sample_period)) {
					_shmDevicesArea->d[i].snmpCaptured = 1 ; // mark as  'in data gathering process' 
					devToMeasureFound = i;
					break;
					}
				}
			}
		pthread_mutex_unlock (& (_shmDevicesArea->lock));

		if (devToMeasureFound > -1)	{ // device requires SNMP monitoring (data gathering) 

			_shmDevicesArea->d[devToMeasureFound].lastRead = t;

			if ( (time(NULL) - _shmDevicesArea->d[devToMeasureFound].lastPingOK) < 100 ) {

				if (_verbose > 1) {
					printf("\n Worker %i collecting info via SNMP from device (%s, %s)", getpid(), _shmDevicesArea->d[devToMeasureFound].name, _shmDevicesArea->d[devToMeasureFound].ip);
					fflush(stdout);
					}
				
				for (iface = 0, snmpCaptureOk = 0 ; iface < _shmInterfacesArea->nInterfaces ; iface++)   {
					if (_shmInterfacesArea->d[iface].enable > 0 && _shmInterfacesArea->d[iface].snmpOIDOk > 0 && _shmDevicesArea->d[devToMeasureFound].deviceId == _shmInterfacesArea->d[iface].deviceId) {

						if ( snmpCollectBWInfo( &(_shmDevicesArea->d[devToMeasureFound]), &(_shmInterfacesArea->d[iface])) == 0 ) {

							_shmInterfacesArea->d[iface].lastSNMPOK = time(NULL);
							snmpCaptureOk = 1;

							// if this is not the first iteration
							if (_shmInterfacesArea->d[iface].iteration <= 0)
								_shmInterfacesArea->d[iface].iteration = 1;
							else {
								if ( _shmInterfacesArea->d[iface].iteration < 1000)
									_shmInterfacesArea->d[iface].iteration = _shmInterfacesArea->d[iface].iteration + 1 ;

								// sending to files a DB we need to be sure there are traffic measurements		
								if ( _to_files )
									saveToFile( &(_shmInterfacesArea->d[iface]) );

								// if we need to send data to DB, we will use SHM queue (parent will perform DB operation)	
								if ( _to_memdb )
									if ( shmQueuePut(_queueInterfaces, (void *)&(_shmInterfacesArea->d[iface])) != 1 )
										fprintf(stderr, "\n ERROR on _queueInterfaces  ! ");

								// send data to influxDB.  Other process will take care, to avoid blocking...
								if ( shmQueuePut(_queueInfluxDB, (void *)&(_shmInterfacesArea->d[iface])) != 1 ) 	
										fprintf(stderr, "\n ERROR on _queueInfluxDB  ! ");

								// send data to REDIS.  Other process will take care, to avoid blocking...
								if ( shmQueuePut(_queueRedis, (void *)&(_shmInterfacesArea->d[iface])) != 1 )
										fprintf(stderr, "\n ERROR on _queueRedis  ! ");

								// detect traffic counters change of at least ONE interface for this device
								//if (_shmInterfacesArea->d[iface].obytes_prev != _shmInterfacesArea->d[iface].obytes || _shmInterfacesArea->d[iface].ibytes_prev != _shmInterfacesArea->d[iface].ibytes )  {
								//	_shmInterfacesArea->d[iface].lastSNMPOK = time(NULL);
								//	snmpCaptureOk = 1;
									}
								}	
							}
						}

					pthread_mutex_lock (& (_shmDevicesArea->lock));
					_shmDevicesArea->d[devToMeasureFound].snmpCaptured = 0 ; // return to 0 to next capture
					if (snmpCaptureOk > 0) {
						_shmDevicesArea->d[devToMeasureFound].lastSNMPOK = time(NULL);		

						//printf("\n Pongo %li en dev %i", _shmDevicesArea->d[devToMeasureFound].lastPingOK , _shmDevicesArea->d[devToMeasureFound].deviceId)		;
						//fflush(stdout);
						if ( shmQueuePut(_queueDevices, (void *)&(_shmDevicesArea->d[devToMeasureFound])) != 1 )	 
							fprintf(stderr, "\n ERROR on _queueDevices  ! ");
						}
					pthread_mutex_unlock (& (_shmDevicesArea->lock));						
					}
				else {	
					if (_verbose > 1) {
						printf("\n Worker %i device (%s, %s) NOT reachable via ICMP (last ping %li): No data collection...",
						getpid(), _shmDevicesArea->d[devToMeasureFound].name, _shmDevicesArea->d[devToMeasureFound].ip,
						(time(NULL) - _shmDevicesArea->d[devToMeasureFound].lastPingOK));

						fflush(stdout);
						}
					}
				}	 // if  there is a device to measure
			}   // else no devices to configure

		sleep (1);
		}    // endless while
}

//------------------------------------------------------------------------

// launch children (MAX_WORKERS).

int launchWorkers ( int workers )   {

int 				i=0;
pid_t				workerPID=0;

for ( i=0 ; i < workers ; i++ )  {

	if ( (workerPID = fork()) > 0 )  {    // parent (this process)
		}
	else if(workerPID == 0)  {    // worker
        prctl(PR_SET_PDEATHSIG, SIGTERM); // every child will receive SIGTERM in case parent ends
		workerRun();
		}
    else {
		printf ("\n\n\n FORK ERROR !!!");
		fflush(stdout);
		exit(-1);
	}  
}

// extra worker for REDIS cache data sending 
if ( (workerPID = fork()) > 0 )  {    // parent (this process)
	}
else if(workerPID == 0)  {    // worker
	prctl(PR_SET_PDEATHSIG, SIGTERM); // every child will receive SIGTERM in case parent ends
	workerRedis();
	}
else {
	printf ("\n\n\n FORK ERROR !!!");
	fflush(stdout);
	exit(-1);
	}
 
// extra worker for INFLUXDB  data sending 
if ( (workerPID = fork()) > 0 )  {    // parent (this process)
	}
else if(workerPID == 0)  {    // worker
	prctl(PR_SET_PDEATHSIG, SIGTERM); // every child will receive SIGTERM in case parent ends
	workerInfluxDB();
	}
else {
	printf ("\n\n\n FORK ERROR !!!");
	fflush(stdout);
	exit(-1);
	}

// extra worker to periodically check devices reported as 'unreachable' or 'snmp error' 
if ( (workerPID = fork()) > 0 )  {    // parent (this process)
	}
else if(workerPID == 0)  {    // worker
	prctl(PR_SET_PDEATHSIG, SIGTERM); // every child will receive SIGTERM in case parent ends
	workerCheckOfflineDevices();
	}
else {
	printf ("\n\n\n FORK ERROR !!!");
	fflush(stdout);
	exit(-1);
	}

// extra worker to SEND ICMP
if ( (workerPID = fork()) > 0 )  {    // parent (this process)
	}
else if(workerPID == 0)  {    // worker
	prctl(PR_SET_PDEATHSIG, SIGTERM); // every child will receive SIGTERM in case parent ends
	workerSendICMP();
	}
else {
	printf ("\n\n\n FORK ERROR  (workerSendICMP)!!!");
	fflush(stdout);
	exit(-1);
	}


// extra worker to RECEIVE ICMP
if ( (workerPID = fork()) > 0 )  {    // parent (this process)
	}
else if(workerPID == 0)  {    // worker
	prctl(PR_SET_PDEATHSIG, SIGTERM); // every child will receive SIGTERM in case parent ends
	workerReceiveICMP();
	}
else {
	printf ("\n\n\n FORK ERROR  (workerReceiveICMP)!!!");
	fflush(stdout);
	exit(-1);
	}

return 1;
}

//------------------------------------------------------------------------

// initialize Shared memory areas for main process (father) and workers
//  2 queues:  devices, interfaces  to send data from forked workers to parent
// 2 shared memory areas: devices, interfaces 

int shmInit() {

pthread_mutexattr_t mattr, ifaceAttr;

// create shared memory queue to senddata to REDIS Cache  
if ( shmQueueInit(&_queueRedis, sizeof(interfaceData), MAX_SHM_QUEUE_SIZE) != 1 )   {
    printf("\n\n ATENTION:   shmArea for _queueRedis QUEUE ERROR !  \n\n");
    exit(EXIT_FAILURE);
    } 

// create shared memory queue to send data to InfluxDB / Grafana suite 
if ( shmQueueInit(&_queueInfluxDB, sizeof(interfaceData), MAX_SHM_QUEUE_SIZE) != 1 )   {
    printf("\n\n ATENTION:   shmArea for _queueInfluxDB QUEUE ERROR !  \n\n");
    exit(EXIT_FAILURE);
    } 

// create shared memory queue  to exchange data between workers and main process  (interfaces) 
if ( shmQueueInit(&_queueInterfaces, sizeof(interfaceData), MAX_SHM_QUEUE_SIZE) != 1 )   {
    printf("\n\n ATENTION:   shmArea for Interfaces QUEUE ERROR !  \n\n");
    exit(EXIT_FAILURE);
    } 

// create shared memory queue  to exchange data between workers and main process  (devices)
if ( shmQueueInit(&_queueDevices, sizeof(deviceData), MAX_SHM_QUEUE_SIZE) != 1 )   {
    printf("\n\n ATENTION:   shmArea for Devices QUEUE ERROR !  \n\n");
    exit(EXIT_FAILURE);
    } 

// we create shared memory area for devices
if ( (_shmDevicesArea = (devicesShm *) create_shared_memory(sizeof(devicesShm))) == NULL )  {
    printf("\n\n ATENTION:   shmArea ERROR on devices!  \n\n");
    exit(EXIT_FAILURE);
    }     

// reset shm memory
memset(_shmDevicesArea, 0, sizeof(devicesShm));

// intialize mutex lock to assure mutual exclusion to shm area
pthread_mutexattr_init( &mattr );
pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK_NP);
pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);

pthread_mutex_init(& (_shmDevicesArea->lock), &mattr);

// we now create shared memory area for Interfaces
if ( (_shmInterfacesArea = (interfacesShm *) create_shared_memory(sizeof(interfacesShm))) == NULL )  {
    printf("\n\n ATENTION:   shmArea ERROR on Interfaces!  \n\n");
    exit(EXIT_FAILURE);
    }     

// reset shm memory
memset(_shmInterfacesArea, 0, sizeof(interfacesShm));

// intialize mutex lock to assure mutual exclusion to shm area
pthread_mutexattr_init( &ifaceAttr );
pthread_mutexattr_settype(&ifaceAttr, PTHREAD_MUTEX_ERRORCHECK_NP);
pthread_mutexattr_setpshared(&ifaceAttr, PTHREAD_PROCESS_SHARED);

pthread_mutex_init(& (_shmInterfacesArea->lock), &ifaceAttr);

return(1);
}

//------------------------------------------------------------------------
//------------------------------------------------------------------------

int main(int argc, char *argv[])
{

// basic info about the program
printf("\n Program %s started. PID: %i.  ", argv[0], getpid()); fflush(stdout);
printf("\n Compiled: %s...\n\n", __TIMESTAMP__); fflush(stdout);

// default database server
strcpy(_server, "dbserver01");

// check arguments!
parseArguments(argc, argv);

// crate SHared memory areas
shmInit();

// if we are in 'checking device mode', just check and exit
if (_deviceToCheck > 0) {
	verifyDevice(_deviceToCheck);
	exit(0);
	}

while (dbread (_shmDevicesArea, _shmInterfacesArea) <= 0)
  {
  printf("\n\n No devices configured? check device and device_bw tables ! \n\n");
  sleep(1);
  };

// reinit in memory database
delete_from_db_mem ();

// get last values from files
retriveBWDataFromFile(  );

// fork children
launchWorkers (_workers); 

// control loop
mainLoop();

exit(0);
}

//------------------------------------------------------------------------
//------------------------------------------------------------------------

