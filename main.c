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

int 			_deviceToCheck = -1;  // set to verify SNMP info from specific device

int 			_useSNMP = 0;

//------------------------------------------------------------------------                                   
//------------------------------------------------------------------------

void shmPrint(shm_area *s)  {
printf("\n\n ---- SHM AREA -----");
printf("\n PIDS:  Father: %i, Son: %i, grandSon: %i", s->fatherPid, s->sonPid, s->grandsonPid);
printf("\n Input BW: %02lf  Output BW: %02lf  (prev: %02lf, %02lf) ", s->ibw, s->obw, s->ibwPrev, s->obwPrev);
printf("\n Last Update: %li (%li seconds ago!) ", s->lastUpdate, time(NULL) - s->lastUpdate);
printf("\n ---- END OF SHM AREA -----\n\n");
}

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

// evaluate alarm conditions based on traffic (heuristic condition pending!!!)

int evalAlarm(deviceData *d, interfaceData *iface)
{
struct tm   stm;

// return to normal conditions are always evaluated, to avoid long (and unrallistic) intervals of failure
if (  iface->alarm_status != ALARM_OK )
	if (detect_normal_traffic_01(d, iface))
		{
		iface->alarm_status = ALARM_OK;
		report_alarm(d, iface);
		}

localtime_r(&(iface->last_access.time), &stm);

// EVALUATE FIRST EXCLUSION INTERVAL ONLY IF INI-FIN values have been fully specified
if (in_interval(&stm, iface->exc_01_ini_h, iface->exc_01_ini_m, iface->exc_01_fin_h, iface->exc_01_fin_m))
	return(1);

// second exclusion interval
if (in_interval(&stm, iface->exc_02_ini_h, iface->exc_02_ini_m, iface->exc_02_fin_h, iface->exc_02_fin_m))
	return(1);

if (iface->alarm_lo < 30)		// zero threshold means no alarm for this interface!
  return(1);

// following calculation will be only evaluated if we are not (already) in ALARM status
if (  iface->alarm_status != ALARM_ERROR )	
	{
	int			CALC_IN_THR=5.0;
	int			CALC_OUT_THR=5.0;
	
	double      incalc=0, outcalc=0;
	
	detect_low_traffic_01((iface->ibw_buf), &incalc);
	detect_low_traffic_01((iface->obw_buf), &outcalc);

	// INPUT condition:
	if ( incalc > CALC_IN_THR && iface->ibw_buf[0] < iface->alarm_lo && iface->ibw_buf[1] < iface->alarm_lo )
	    {
		iface->alarm_status = ALARM_ERROR;
		report_alarm(d, iface);
		}
	else if ( outcalc > CALC_OUT_THR && iface->obw_buf[0] < iface->alarm_lo && iface->obw_buf[1] < iface->alarm_lo )
	    {
		iface->alarm_status = ALARM_ERROR;
		report_alarm(d, iface);
		}
	}
return(1);
}	

//------------------------------------------------------------------------

//
// This routine gets las line of traffic to avoid discrepancies 
// after restarting the process 

int retriveBWDataFromFile(  )
{
FILE       	 	*f=NULL;
char       		 	fname[500];
char 				c=0;
//char 				line[2000] = "";
//char 				*ret=NULL;
int 				i=0, j=0;
interfaceData 		*ifs = NULL;

for (j=0 ; j<_shmInterfacesArea->nInterfaces ; j++) {
	
	ifs = &(_shmInterfacesArea->d[j]);
	if (_verbose > 3)
		{printf("\n\n Retrieving LAST line of file:  %s", ifs->file_var_name); fflush(stdout); }

	if ( strlen(ifs->file_var_name) > 0 ) {
		sprintf(fname, "/data/bw/%s", ifs->file_var_name);
		if  ( (f = fopen(fname, "r")) != NULL )  {
			fseek(f, -4, SEEK_END);  // go to the end

			c = fgetc(f);
			while(c != '\n' && c != '\r' ) {
				fseek(f, -2, SEEK_CUR);
				c = fgetc(f);
				}

			//fseek(f, 1, SEEK_CUR);
			//ret = fgets(line, 1000, f);
			//printf("\n ---%s---", line);

			i = fscanf(f, "%*i.%*i,%*i.%*i,%*i,%*i,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf[^\n]",
					&(ifs->ibw), &(ifs->obw), &(ifs->ibw_a), &(ifs->obw_a),
					&(ifs->ibw_b), &(ifs->obw_b), &(ifs->ibw_c), &(ifs->obw_c)
					);
			i=i;

			ifs->ibw = 1000 * ifs->ibw_c ;
			ifs->obw = 1000 * ifs->obw_c;
			ifs->ibw_a = 1000 * ifs->ibw_c ;
			ifs->obw_a = 1000 * ifs->obw_c;
			ifs->ibw_b = 1000 * ifs->ibw_c ;
			ifs->obw_b = 1000 * ifs->obw_c;
			ifs->ibw_c = 1000 * ifs->ibw_c ;
			ifs->obw_c = 1000 * ifs->obw_c;

			//printf ("\n -- %lf %lf %lf %lf", ifs->ibw,ifs->obw, ifs->ibw_c ,ifs->obw_c);
			//fflush(stdout);	

			fclose(f);
			}
		}
	}
	
return(1);
}    


//------------------------------------------------------------------------

int saveToFile( interfaceData *ifs )
{
FILE        *f=NULL;
char        fname[500];
struct tm   stm;
double      inavg=0, outavg=0;
double      incalc=0, outcalc=0;

if (_verbose > 3)
	printf("\n\n Saving data to file:");

if ( strlen(ifs->file_var_name) > 0 ) {
	sprintf(fname, "/data/bw/%s", ifs->file_var_name);
	if  ( (f = fopen(fname, "a")) != NULL )  {
		localtime_r(&(ifs->last_access.time), &stm);

		if (! avg_basic((ifs->ibw_buf), 0, (MAXAVGBUF-1), &inavg) )
			inavg=0;

		if (! avg_basic((ifs->obw_buf), 0, (MAXAVGBUF-1), &outavg) )
			outavg=0;

		detect_low_traffic_01((ifs->ibw_buf), &incalc);
		detect_low_traffic_01((ifs->obw_buf), &outcalc);
		
		fprintf(f, "%li.%03i,%4i%02i%02i%02i%02i%02i.%03i,%lli,%lli,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%li,%li,%.4lf,%.4lf\r\n",
				ifs->last_access.time, ifs->last_access.millitm, 
				stm.tm_year+1900, stm.tm_mon+1, stm.tm_mday, stm.tm_hour, stm.tm_min, stm.tm_sec, 
				ifs->last_access.millitm, 
				ifs->ibytes, ifs->obytes, ifs->ibw/1000, ifs->obw/1000, 
				ifs->ibw_a/1000, ifs->obw_a/1000,
				ifs->ibw_b/1000, ifs->obw_b/1000, ifs->ibw_c/1000, ifs->obw_c/1000, 
				inavg/1000, outavg/1000,
				ifs->ierrors, ifs->oerrors,
				incalc, outcalc
				);
		fclose(f);
		}
	}


return(1);
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

// worker to send data to redis 

void workerRedis()  {

interfaceData		ifaceData;
redisContext 		*c = NULL;
redisReply 			*reply = NULL;
static time_t		old_db_time=0;
time_t				current_time=0;
int 				comm_error=0;
char 				myStr[3000];    

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
		sprintf(myStr, "HSET devices_bw:%i 'json' '{\"name\":\"%s\",\"descr\":\"%s\",\"ibw\":%.2f,\"obw\":%.2f,\"file\":\"%s\"}'  'name' '%s' 'descr' '%s' 'ibw' '%.2f' 'obw' '%.2f' ", ifaceData.interfaceId, ifaceData.name, ifaceData.description, ifaceData.ibw_a, ifaceData.obw_a, ifaceData.file_var_name ,ifaceData.name, ifaceData.description, ifaceData.ibw_a, ifaceData.obw_a);
        reply = redisCommand(c, myStr);
		//printf("\n %s", reply->str );
        freeReplyObject(reply);

		// reccord also in a SET (kinf of index)
        reply = redisCommand(c, "SADD devices_bw %i", ifaceData.interfaceId );
		//printf("\n %s", reply->str );
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

// pre - forked worker

void workerRun()  {
int 	i=0, iface=0;
int 	devToInitFound=0;
int 	devToMeasureFound=0;
time_t	t=0;
int 	snmpCaptureOk=0;   // flag to detect changes un traffic counters as 'valid reading' 

sleep(rand()%20);  

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

		if ( snmpCheckParameters( &(_shmDevicesArea->d[devToInitFound]), _shmInterfacesArea )																										 == 1 ) {    // OK!
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
			printf("\n Worker %i:  device (%s, %s) snmpCheckParameters ERROR!", getpid(), _shmDevicesArea->d[devToInitFound].name, _shmDevicesArea->d[devToInitFound].ip);
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

			if ( ping(_shmDevicesArea->d[devToMeasureFound].ip)==0 || ping(_shmDevicesArea->d[devToMeasureFound].ip)==0 || ping(_shmDevicesArea->d[devToMeasureFound].ip)==0  ) {
			
				if (_verbose > 1) {
					printf("\n Worker %i collecting info via SNMP from device (%s, %s)", getpid(), _shmDevicesArea->d[devToMeasureFound].name, _shmDevicesArea->d[devToMeasureFound].ip);
					fflush(stdout);
					}

				_shmDevicesArea->d[devToMeasureFound].lastPingOK = time(NULL);
				
				for (iface = 0, snmpCaptureOk = 0 ; iface < _shmInterfacesArea->nInterfaces ; iface++)   {
					if (_shmInterfacesArea->d[iface].enable > 0 && _shmDevicesArea->d[devToMeasureFound].deviceId == _shmInterfacesArea->d[iface].deviceId) {
						snmpCollectBWInfo( &(_shmDevicesArea->d[devToMeasureFound]), &(_shmInterfacesArea->d[iface]));

						// if this is not the first iteration
						if ( (_shmInterfacesArea->d[iface].obytes_prev>0 || _shmInterfacesArea->d[iface].ibytes_prev>0)  ) {

							// sending to files a DB we need to be sure there are traffic measurements		
							if ( _to_files )
								saveToFile( &(_shmInterfacesArea->d[iface]) );

							// if we need to send data to DB, we will use SHM queue (parent will perform DB operation)	
							if ( _to_memdb )
								shmQueuePut(_queueInterfaces, (void *)&(_shmInterfacesArea->d[iface]));	

							// send data to influxDB.  Other process will take care, to avoid blocking...
							shmQueuePut(_queueInfluxDB, (void *)&(_shmInterfacesArea->d[iface]));	

							// send data to REDIS.  Other process will take care, to avoid blocking...
							shmQueuePut(_queueRedis, (void *)&(_shmInterfacesArea->d[iface]));

							// detect traffic counters change of at least ONE interface for this device
							if (_shmInterfacesArea->d[iface].obytes_prev != _shmInterfacesArea->d[iface].obytes || _shmInterfacesArea->d[iface].ibytes_prev != _shmInterfacesArea->d[iface].ibytes ) 
								snmpCaptureOk = 1;
							}
						}
					}	
				pthread_mutex_lock (& (_shmDevicesArea->lock));
				_shmDevicesArea->d[devToMeasureFound].snmpCaptured = 0 ; // return to 0 to next capture
				if (snmpCaptureOk > 0) {
					_shmDevicesArea->d[devToMeasureFound].lastSNMPOK = time(NULL);		

					//printf("\n Pongo %li en dev %i", _shmDevicesArea->d[devToMeasureFound].lastPingOK , _shmDevicesArea->d[devToMeasureFound].deviceId)		;
					//fflush(stdout);
					shmQueuePut(_queueDevices, (void *)&(_shmDevicesArea->d[devToMeasureFound]));	
					}
				pthread_mutex_unlock (& (_shmDevicesArea->lock));						
				}
			else {	
				if (_verbose > 1) {
					printf("\n Worker %i device (%s, %s) NOT reachable via ICMP: No data collection...", getpid(), _shmDevicesArea->d[devToMeasureFound].name, _shmDevicesArea->d[devToMeasureFound].ip);
					fflush(stdout);
					}
				}
			}
		}

	sleep (1);
	}

}


//------------------------------------------------------------------------

void exitfunction()
{
kill(_grandsonPID, SIGTERM);	
//kill(0, SIGTERM);		// kill everyone on his group (all his children)
exit(0);
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

int verifyDevice (int deviceId) {

int 			i=0,j=0;
int 			nDevices=0;

if ( (nDevices = dbreadOneDevice ( _shmDevicesArea, _shmInterfacesArea,  deviceId)) < 1 ) {
	printf("\n Device %i not present (or disabled) in devices + devices_bw...\n\n", deviceId);
	fflush(stdout);
	return(-1);
}

for (i=0 ; i<_shmDevicesArea->nDevices ; i++)  {
	deviceData *d = &(_shmDevicesArea->d[i]);

	printf("\n\n Device: (%i) %i -->  %s (%s) enable: %i getrun: %i cliacc: %i, vendor:%i, model: %i ifs: %i", i, d->deviceId, d->name, d->ip, d->enable, d->getrunn, d->cli_acc, d->vendor_id, d->model_id, d->nInterfaces);
	if ( d->snmp <= 0)
		printf("\n REMEMBER TO configure 'snmp' field in 'devices' table to 1 or 2");
	for (j=0 ; j < _shmInterfacesArea->nInterfaces ; j++) {
		interfaceData *ifs = &(_shmInterfacesArea->d[j]); 
		if (d->deviceId == ifs->deviceId)	
			printf("\n      %i:  devid: %i  ifid: %i  enable: %i  ifname: %s  fvarname: %s alarm_lo: %i  ", j, ifs->deviceId, ifs->interfaceId, ifs->enable, ifs->name, ifs->file_var_name, ifs->alarm_lo);
		}
	}	

printf("\n\n Checking ICMP.....");	

for (i=0 ; i<_shmDevicesArea->nDevices ; i++)  {
	deviceData *d = &(_shmDevicesArea->d[i]);
	if ( ! ( ping(d->ip)==0 || ping(d->ip)==0 || ping(d->ip)==0 ) ) {
		printf("  NOT reachable via ICMP. Aborting test... \n\n ");
		fflush(stdout);
		return(-1);
		}
	else {	
		printf("  OK");
		fflush(stdout);
		}
	}

printf("\n\n Checking SNMP community and version...");	
fflush(stdout);
for (i=0 ; i<_shmDevicesArea->nDevices ; i++)  {
	deviceData *d = &(_shmDevicesArea->d[i]);

	d->snmpVersion = -1;

	printf("\n Testing V2, Vosto...."); fflush(stdout);
	if ( snmpGetSysDesrc (SNMP_VERSION_2c, "Vostok3KA", d->ip, NULL) == 0 ) {
		strcpy(d->snmpCommunity, "Vostok3KA");
		d->snmpVersion = SNMP_VERSION_2c;
		printf("   OK");
		}
	else {
		printf("   ERROR / TIMEOUT");
		printf("\n Testing V2, pub...."); fflush(stdout);
		if ( snmpGetSysDesrc (SNMP_VERSION_2c, "public", d->ip, NULL) == 0 ) {
			strcpy(d->snmpCommunity, "public");
			d->snmpVersion = SNMP_VERSION_2c;
			printf("   OK");
			}
		else {
			printf("   ERROR / TIMEOUT");
			printf("\n Testing V1, Vosto...."); fflush(stdout);
			if ( snmpGetSysDesrc (SNMP_VERSION_1, "Vostok3KA", d->ip, NULL) == 0 ) {
				strcpy(d->snmpCommunity, "Vostok3KA");
				d->snmpVersion = SNMP_VERSION_1;
				printf("   OK");
				}
			else {
				printf("   ERROR / TIMEOUT");
				printf("\n Testing V1, pub...."); fflush(stdout);
				if ( snmpGetSysDesrc (SNMP_VERSION_1, "public", d->ip, NULL) == 0 ) {
					strcpy(d->snmpCommunity, "public");
					d->snmpVersion = SNMP_VERSION_1;
					printf("   OK");
					}
				else {
					printf("   ERROR / TIMEOUT");
					}
				}
			}
		}

if (d->snmpVersion > -1) {
	int ret = 0, iface = 0;

	printf("\n Testing ifXtable ...."); fflush(stdout);
	if ( snmpVerifyIfXTable (d, NULL ) == 0)
		printf("   OK");
	else 
		printf("   Not present ....");

	printf("\n Testing Interfaces ID...."); fflush(stdout);

	// get interface position in IF-TABLE (1.3.6.1.2.1.2.2.1.2). In case of error we cannot continue...
	// in case of error we also look into ifXtable  (1.3.6.1.2.1.31.1.1.1.1) 
	if ( (ret = getIndexOfInterfaces( d, _shmInterfacesArea, "1.3.6.1.2.1.2.2.1.2")) != 0 ) {
		if ( (ret = getIndexOfInterfaces( d, _shmInterfacesArea, "1.3.6.1.2.1.31.1.1.1.1")) != 0 ) {
			printf("\n\n UNABLE to find some Interfaces (device *%s, %s) error returned: %i !! \n\n", d->name, d->ip, ret );

			}
		}

	for (iface = 0 ; iface < _shmInterfacesArea->nInterfaces ; iface++)   {
		if (_shmInterfacesArea->d[iface].enable > 0 && d->deviceId == _shmInterfacesArea->d[iface].deviceId) {
//				printf("\n    Interface %s (file:%s) ", _shmInterfacesArea->d[iface].name, _shmInterfacesArea->d[iface].file_var_name);
			printf("\n    Interface %30s ....  ", _shmInterfacesArea->d[iface].name);
			if ( strlen(_shmInterfacesArea->d[iface].oidIndex) > 0 )
				printf(" ok (index: %s) ", _shmInterfacesArea->d[iface].oidIndex);
			else
				printf(" NOT FOUND ******************** ");
			}
		}
	}
else 	
	printf(" \n\n SNMP v1/2 configured on device ? ");

}

printf("\n\n");	
fflush(stdout);	

return(0);
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

