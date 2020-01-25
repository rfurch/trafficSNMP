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
void 			*_queue = NULL;	

int 			_useSNMP = 0;

int 			_maxRandomStartDelay = 30;

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
printf ("  -e   send data to epics (deactivated by default) \n");
printf ("  -H   send data to historic DB  (deactivated by default) \n");
printf ("  -m   send data to MEMORY DB  (deactivated by default) \n");
printf ("  -M   Maximum start delay (default 30 sec) (Prevents DB hangs) \n");
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

int saveToFile( interfaceData *ifs )
{
FILE        *f=NULL;
char        fname[500];
struct tm   stm;
double      inavg=0, outavg=0;
double      incalc=0, outcalc=0;

if (_verbose > 3)
	printf("\n\n Saving data to file:");

if (ifs->ibytes_prev > 0 && ifs->obytes_prev > 0 ) {
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

while ((opt = getopt_long(argc, argv, "M:s:d:r:w:ehfmvaS", longopts, 0)) != -1)
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

        case 'M':
      		if(optarg)
          	_maxRandomStartDelay=atoi(optarg);
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

if ( _to_epics==0 && _to_files==0 && _to_memdb==0 && _to_hisdb==0 ) {
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
static time_t		old_db_time=0;
time_t				current_time=0;
int 				comm_error=0;

old_db_time = current_time = time(NULL);

while(1) {

	printf("\n           mainLoop ... ");
	if (_verbose > 6)
		for (i=0 ; i<_shmDevicesArea->nDevices ; i++)
			printf("\n            device %i snmpConfigured: %i snmpCaptured: %i ", _shmDevicesArea->d[i].deviceId, _shmDevicesArea->d[i].snmpConfigured, _shmDevicesArea->d[i].snmpCaptured);
	
	while (shmQueueGet(_queue, &ifaceData) == 1) {
		to_db_mem (&ifaceData);

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

// pre - forked worker

void workerRun()  {
int 	i=0, iface=0;
int 	devToInitFound=0;
int 	devToMeasureFound=0;
time_t	t=0;

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
	else {   // all devices configured, we can now get traffic counters

		pthread_mutex_lock (& (_shmDevicesArea->lock));
		t = time(NULL);
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

			if ( ping(_shmDevicesArea->d[devToMeasureFound].ip)==0 || ping(_shmDevicesArea->d[devToMeasureFound].ip)==0 || ping(_shmDevicesArea->d[devToMeasureFound].ip)==0 || ping(_shmDevicesArea->d[devToMeasureFound].ip)==0 || ping(_shmDevicesArea->d[devToMeasureFound].ip)==0 ) {
			
				if (_verbose > 1) {
					printf("\n Worker %i collecting info via SNMP from device (%s, %s)", getpid(), _shmDevicesArea->d[devToMeasureFound].name, _shmDevicesArea->d[devToMeasureFound].ip);
					fflush(stdout);
					}

				_shmDevicesArea->d[devToMeasureFound].lastPingOK = time(NULL);
				
				for (iface = 0 ; iface < _shmInterfacesArea->nInterfaces ; iface++)   {
					if (_shmInterfacesArea->d[iface].enable > 0 && _shmDevicesArea->d[devToMeasureFound].deviceId == _shmInterfacesArea->d[iface].deviceId) {
						snmpCollectBWInfo( &(_shmDevicesArea->d[devToMeasureFound]), &(_shmInterfacesArea->d[iface]));

						// sending to files a DB we need to be sure there are traffic measurements		
						if (_to_files && _shmInterfacesArea->d[iface].obytes_prev>0 && _shmInterfacesArea->d[iface].ibytes_prev>0)
							saveToFile( &(_shmInterfacesArea->d[iface]) );

						if (_to_memdb && _shmInterfacesArea->d[iface].obytes_prev>0 && _shmInterfacesArea->d[iface].ibytes_prev>0)
							shmQueuePut(_queue, (void *)&(_shmInterfacesArea->d[iface]));	

						}
					}	
				pthread_mutex_lock (& (_shmDevicesArea->lock));
				_shmDevicesArea->d[devToMeasureFound].snmpCaptured = 0 ; // return to 0 to next capture
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

// launch children (MAX_WORKERS).

int launchWorkers ( int workers )   {

int 				i=0;
pid_t				workerPID=0;

for ( i=0 ; i < workers ; i++ )  {

	if ( (workerPID = fork()) > 0 )  {
		}
	else if(workerPID == 0)  {    // worker
		workerRun();
		}
    else {
		printf ("\n\n\n FORK ERROR !!!");
		fflush(stdout);
		exit(-1);
	}  
}

return 1;
}

//------------------------------------------------------------------------

// initialize Shared memory areas for main process (father) and workers

int shmInit() {

pthread_mutexattr_t mattr, ifaceAttr;

// create shared memory queue  to exchange data between workers and main process  
if ( shmQueueInit(&_queue, sizeof(interfaceData), MAX_SHM_QUEUE_SIZE) != 1 )   {
    printf("\n\n ATENTION:   shmArea for QUEUE ERROR !  \n\n");
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

while (dbread (_shmDevicesArea, _shmInterfacesArea) <= 0)
  {
  printf("\n\n No devices configured? check device and device_bw tables ! \n\n");
  sleep(1);
  };

// reinit in memory database
delete_from_db_mem ();

// fork children
launchWorkers (_workers); 

// control loop
mainLoop();

exit(0);
}

//------------------------------------------------------------------------
//------------------------------------------------------------------------

