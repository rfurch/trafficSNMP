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
#include <pthread.h>



#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <signal.h>
#include <pty.h>
#include <math.h>
#include <mysql/mysql.h>

#include <sys/timeb.h>
#include <getopt.h>

#include "ifaceData.h"                                                                                                     

int             _verbose=0;

pid_t           _fatherPID=0;
pid_t           _sonPID=0;
pid_t           _grandsonPID=0;

int             _to_epics=0;		// register into EPICS
int             _to_files=0;		// send data to CSV Files (usually  /data/bw)
int             _to_memdb=0;		// write to DB
int             _to_hisdb=0;        // write to historic DB
int             _send_alarm=0;      // send LOW Traffic Alarms                                    
char            _process_name[100];
int 			_sample_period=10;   // default sample time in seconds
int 			_reconnect_period=10;   // default reconnec time in minutes

int				_speech_prio=8;		// minimum priority value to trigger speech messages

MYSQL       	*_mysql_connection_handler=NULL;

extern char     server[];
void 			*_shmArea=NULL; 

int 			_useSNMP = 0;

int 			_maxRandomStartDelay = 30;

pthread_mutex_t _threadMutex;


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
printf ("  -v   Verbose mode\n");
printf ("  -n   dev_id (from 'devices' table) \n ");
printf ("  -e   send data to epics (deactivated by default) \n");
printf ("  -h   send data to historic DB  (deactivated by default) \n");
printf ("  -m   send data to MEMORY DB  (deactivated by default) \n");
printf ("  -M   Maximum start delay (default 30 sec) (Prevents DB hangs) \n");
printf ("  -f   send data to FILES  (deactivated by default) \n");
printf ("  -a   send alarms  (deactivated by default) \n");
printf ("  -d   database server  (default dbserver01) \n");
printf ("  -s   sample period (default 10 seconds) \n");
printf ("  -S   try to capture values via SNMP (Overrides devices table parameter!) \n");
printf ("  -r   reconnect period (default 10 minutes) \n");
printf ("  -p   minimum priority to trigger voice messages (default=8) \n");
printf ("============================================================\n\n");
fflush(stdout);
}

//------------------------------------------------------------------------

void exitfunction()
{
kill(_grandsonPID, SIGTERM);	
//kill(0, SIGTERM);		// kill everyone on his group (all his children)
exit(0);
}

//------------------------------------------------------------------------

// called on termination signals
 
void endGrandson (int signum)  {
int stat=0; 

printf("\n\n KILLING GRANDSON with SIGTERM\n");
kill(_grandsonPID, SIGTERM);	
printf("WAITING for GRANDSON to DIE! \n\n");
waitpid(_grandsonPID, &stat, 0);
exit(0); 
}

//------------------------------------------------------------------------

// called on termination signals
 
void endSon (int signum)  {
int stat=0; 

printf("\n\n KILLING SON with SIGTERM\n");
kill(_sonPID, SIGTERM);	
printf("WAITING for SON to DIE! \n\n");
waitpid(_sonPID, &stat, 0);
exit(0); 
}

//------------------------------------------------------------------------

// evaluate alarm conditions based on traffic (heuristic condition pending!!!)

int eval_alarm(device_data *d, int n)
{
struct tm   stm;

// return to normal conditions are always evaluated, to avoid long (and unrallistic) intervals of failure
if (  d->ifs[n].alarm_status != ALARM_OK )
	if (detect_normal_traffic_01(d, n))
		{
		d->ifs[n].alarm_status = ALARM_OK;
		report_alarm(d,n);
		}

localtime_r(&(d->ifs[n].last_access.time), &stm);

// EVALUATE FIRST EXCLUSION INTERVAL ONLY IF INI-FIN values have been fully specified
if (in_interval(&stm, d->ifs[n].exc_01_ini_h, d->ifs[n].exc_01_ini_m, d->ifs[n].exc_01_fin_h, d->ifs[n].exc_01_fin_m))
	return(1);

// second exclusion interval
if (in_interval(&stm, d->ifs[n].exc_02_ini_h, d->ifs[n].exc_02_ini_m, d->ifs[n].exc_02_fin_h, d->ifs[n].exc_02_fin_m))
	return(1);

if (d->ifs[n].alarm_lo < 30)		// zero threshold means no alarm for this interface!
  return(1);

// following calculation will be only evaluated if we are not (already) in ALARM status
if (  d->ifs[n].alarm_status != ALARM_ERROR )	
	{
	int			CALC_IN_THR=5.0;
	int			CALC_OUT_THR=5.0;
	
	double      incalc=0, outcalc=0;
	
	detect_low_traffic_01((d->ifs[n].ibw_buf), &incalc);
	detect_low_traffic_01((d->ifs[n].obw_buf), &outcalc);

	// INPUT condition:
	if ( incalc > CALC_IN_THR && d->ifs[n].ibw_buf[0] < d->ifs[n].alarm_lo && d->ifs[n].ibw_buf[1] < d->ifs[n].alarm_lo )
	    {
		d->ifs[n].alarm_status = ALARM_ERROR;
		report_alarm(d,n);
		}
	else if ( outcalc > CALC_OUT_THR && d->ifs[n].obw_buf[0] < d->ifs[n].alarm_lo && d->ifs[n].obw_buf[1] < d->ifs[n].alarm_lo )
	    {
		d->ifs[n].alarm_status = ALARM_ERROR;
		report_alarm(d,n);
		}
	}
return(1);
}	

//------------------------------------------------------------------------

int saveToFile( device_data *d )
{
FILE        *f=NULL;
char        fname[500];
struct tm   stm;
int 		i=0;
double      inavg=0, outavg=0;
double      incalc=0, outcalc=0;

if (_verbose > 3)
  printf("\n\n Saving data to file:");

for (i=0 ; i<d->nInterfaces ; i++)
  {
  if ( strlen(d->ifs[i].file_var_name) > 0 )
	{
	sprintf(fname, "/data/bw/%s", d->ifs[i].file_var_name);
	if  ( (f = fopen(fname, "a")) != NULL )
  	  {
	  localtime_r(&(d->ifs[i].last_access.time), &stm);

	  if (! avg_basic((d->ifs[i].ibw_buf), 0, (MAXAVGBUF-1), &inavg) )
		  inavg=0;

	  if (! avg_basic((d->ifs[i].obw_buf), 0, (MAXAVGBUF-1), &outavg) )
		  outavg=0;

	  detect_low_traffic_01((d->ifs[i].ibw_buf), &incalc);
	  detect_low_traffic_01((d->ifs[i].obw_buf), &outcalc);
	  
	  fprintf(f, "%li.%03i,%4i%02i%02i%02i%02i%02i.%03i,%lli,%lli,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf,%li,%li,%.4lf,%.4lf\r\n",
			  d->ifs[i].last_access.time, d->ifs[i].last_access.millitm, 
              stm.tm_year+1900, stm.tm_mon+1, stm.tm_mday, stm.tm_hour, stm.tm_min, stm.tm_sec, 
              d->ifs[i].last_access.millitm, 
	          d->ifs[i].ibytes, d->ifs[i].obytes, d->ifs[i].ibw/1000, d->ifs[i].obw/1000, 
	          d->ifs[i].ibw_a/1000, d->ifs[i].obw_a/1000,
	          d->ifs[i].ibw_b/1000, d->ifs[i].obw_b/1000, d->ifs[i].ibw_c/1000, d->ifs[i].obw_c/1000, 
			  inavg/1000, outavg/1000,
	          d->ifs[i].ierrors, d->ifs[i].oerrors,
			  incalc, outcalc
			  );
      fclose(f);
      }
    usleep(5000);
    }
  }
return(1);
}    

                          
//------------------------------------------------------------------------

// this is the child. he invokes telnet/ssh iteratively, in case
// parent decides to end communication

int   processChild( device_data *d )
{
char 	aux[500];
int     end=0, ret=0;

while (!end)
  {
 
  usleep(300000);
  }	
	             
return(ret);
}

//------------------------------------------------------------------------

// this process send commands to his child, then receive and proceses answers
int   processParent( device_data *d, int infd, int outfd, pid_t child_pid, int dev_id )
{
int 				end=0;
int 				iface=0;
static time_t		old_time=0, old_db_time=0;
time_t				current_time=0;
int 				comm_error=0;

old_time = current_time = time(NULL);
if (d->authenticate && d->authenticate(d, infd, outfd))
  {
  if (_verbose > 2)	
	printf("\n Device hostname: |%s| \n",d->hostname); 

  while (!end)
    {
	// check UP to 3 pings to device!

	if ( ping(d->ip) == 0 || ping(d->ip)==0 || ping(d->ip)==0 )  {
		printf("\n device %s (%s) OK via ICMP. ", d->ip, d->name);
        update_devices_mem_ping_time(d->dev_id);
		}
	else 
		printf("\n device %s (%s) UNREACHABLE via ICMP. ", d->ip, d->name);
	fflush(stdout); 
	  
    for (iface=0 ; iface<d->nInterfaces ; iface++)
  	  {
	  if (d->ifs[iface].enable > 0 && !comm_error)
		{
		if (d->process) {  
		    d->process(d, infd, outfd, child_pid, dev_id, iface);
		    if ( fabs(d->ifs[iface].ibw - ((shm_area *)_shmArea)->ibw) > 5 ) {
				((shm_area *)_shmArea)->ibwPrev = ((shm_area *)_shmArea)->ibw; 
				((shm_area *)_shmArea)->ibw = d->ifs[iface].ibw;
				((shm_area *)_shmArea)->lastUpdate = time(NULL);
				}
		    if ( fabs(d->ifs[iface].obw - ((shm_area *)_shmArea)->obw) > 5 ) {
				((shm_area *)_shmArea)->obwPrev = ((shm_area *)_shmArea)->obw; 
				((shm_area *)_shmArea)->obw = d->ifs[iface].obw;
				((shm_area *)_shmArea)->lastUpdate = time(NULL);
				}
		    }
		}	
	  }	
	
	if (d->ncycle > 0 && !comm_error)     
	  {
	  if (_to_files)
    	saveToFile( d );

	  if (_to_memdb)
  		to_db_mem ( d );

	  if (_to_hisdb)
		to_db_hist ( d );
	  }	

    if (d->ncycle < 1000)  // for start condition
		d->ncycle++;

    // preventive disconnection, just in case  
    if ( ((current_time = time(NULL)) > (old_time + (_reconnect_period * 60))) || comm_error )
  	  {
  	  old_time = current_time;
	  dbread (d, dev_id);

	  if (_verbose > 2)			printf("\n It's high time for a reconnection!");
		
	  if (d->disconnect)
              d->disconnect( d, infd, outfd );
      sleep (1);
	  if (d->authenticate && d->authenticate(d, infd, outfd))
		comm_error=0;
	  else	
		{
		comm_error=1;
  		printf("\n Authentication error in reconnection! \n");
  		}

  	  sleep (9);
  	  }   
    else  
  	  sleep(_sample_period);  


    // preventive disconnection from DB
    if ( ((current_time = time(NULL)) > (old_db_time + 300)) || comm_error )
  	  {
  	  old_db_time = current_time;
	  db_disconnect();
	  db_connect();
	  }

	db_keepalive(_process_name);
  	}
  	
  if (d->disconnect)
    d->disconnect( d, infd, outfd );
  }

kill(child_pid, SIGINT);          
return(1);
}

//------------------------------------------------------------------------

//
// Adjust ponters to (authenticate, procees, parse and disconnect) functions
// accordingly with vendor / model
//

int initDevPointers(device_data *d)
{

if ( _useSNMP || d->snmp > 0) {
    d->authenticate = snmp_authenticate;
    d->process = snmp_process;
    d->parse_bw = snmp_parse_bw;
    d->disconnect = snmp_disconnect;
	_sample_period = (_sample_period < 30) ? 30 : _sample_period;  // min: 30 seconds sample period...
	}

return(1);
}

//------------------------------------------------------------------------

//
// launch SON and GRANDSON to get data via telnet, ssh, SNMP, etc.
//

int launchWorkers (int *retfd, device_data *devd, int dev_id)   {

// set mt pid on SHM
((shm_area *) _shmArea)->sonPid = _sonPID = getpid();  

// we fork to call telnet / ssh
_grandsonPID = forkpty(retfd, NULL, NULL, NULL);

if( _grandsonPID < 0)  {
	printf("\n\n FORK ERROR !!! \n\n");
	exit(-1);
	}
else if (_grandsonPID == 0)  {  // we are in the grandson
	// set mt pid on SHM
	((shm_area *) _shmArea)->grandsonPid = _grandsonPID = getpid(); 
	processChild(devd);
	}
else 		// we are in the son
	{
	signal(SIGTERM, endGrandson);
	signal(SIGQUIT, endGrandson);
	signal(SIGINT, endGrandson);

	atexit(exitfunction);
	processParent(devd, *retfd, *retfd, _grandsonPID, dev_id);
	}
return 1;
}

//------------------------------------------------------------------------

// basic program argument parsing

int parseArguments(int argc, char *argv[], int *dev_id) {

int     		opt=0;

while ((opt = getopt(argc, argv, "M:s:d:n:r:ehfmvaS")) != -1)
    {
    switch (opt)
        {
        case 'v':
            _verbose++;
        break;

        case 'e':
            _to_epics=1;
        break;

        case 'f':
            _to_files=1;
        break;

        case 'm':
            _to_memdb=1;
        break;

        case 'h':
            _to_hisdb=1;
        break;

        case 'a':
            _send_alarm=1;
        break;

        case 'n':
      		if(optarg)
          	*dev_id=atoi(optarg);
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
				_sample_period = (atoi(optarg) >= 1) ? atoi(optarg) : 1;
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
        	strcpy(server, optarg);
        break;


        default: /* ’?’ */
            printUsage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

if (*dev_id<0)
  {
  printUsage(argv[0]);
  exit(EXIT_FAILURE);
  }

if ( _to_epics==0 && _to_files==0 && _to_memdb==0 && _to_hisdb==0 )
  printf("\n\n ATENTION:   collected data will not be recorded to DB, MEM, FILE, etc!  (It's your call dude (your time, your CPU!) \n\n");

sprintf(_process_name, "TRAFSTATS_%i", *dev_id);

return 1;
}

//------------------------------------------------------------------------
//------------------------------------------------------------------------

int main(int argc, char *argv[])
{
int 				dev_id=-1;
int 				retfd=0;
device_data			devd;

pthread_attr_t 		attr;

// Pthreads setup: initialize mutex

pthread_mutex_init(&_threadMutex, NULL);
pthread_attr_init(&attr);
pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

// basic info about the program
printf("\n Program %s started. PID: %i.  ", argv[0], getpid()); fflush(stdout);
printf("\n Compiled: %s...\n\n", __TIMESTAMP__); fflush(stdout);

// default database server
strcpy(server, "dbserver01");

// check arguments!
parseArguments(argc, argv, &dev_id);

// we create shared memory area in which parent, son and grandson will be writting
if ( (_shmArea = create_shared_memory(sizeof(shm_area))) == NULL )  {
    printf("\n\n ATENTION:   shmArea ERROR!  \n\n");
    exit(EXIT_FAILURE);
    }     

// reset shm memory, and set STARTING flag
memset(_shmArea, 0, sizeof(shm_area));
((shm_area *) _shmArea)->starting = 1;
((shm_area *) _shmArea)->lastUpdate = time(NULL);

// set mt pid on SHM
((shm_area *) _shmArea)->fatherPid = _fatherPID = getpid();  

// wait randomly to slow start in case of many processes
randomDelay(1, _maxRandomStartDelay);

memset(&devd, 0, sizeof(device_data));
while (dbread (&devd, dev_id) <= 0)
  {
  printf("\n\n No interfaces configured on 'devices_bw' for this device ID (%i) ! \n\n", dev_id);
  sleep(1);
  };

delete_from_db_mem (dev_id);

// initialization process according to detect device type!
initDevPointers(&devd);

while (1) {

	// try 3 pings
	if ( ping(devd.ip)!=0 &&  ping(devd.ip)!=0 && ping(devd.ip)!=0 ) {
		printf("\n device %s not available via ICMP. waiting 30 seconds...", devd.ip);
		fflush(stdout);
		sleep(30);
		}
	else {

		if ( ((shm_area *) _shmArea)->starting || (time(NULL) - (((shm_area *) _shmArea)->lastUpdate)) > (6 * _sample_period) )  {

			// not the begining of program execution	
			((shm_area *) _shmArea)->starting = 0;	
			// reset timming mechanism to give new childs some time
			((shm_area *) _shmArea)->lastUpdate = time(NULL);

			// log actions if we kill processes (it is not the first loop!)
			if ( ((shm_area *) _shmArea)->grandsonPid > 0 || ( ((shm_area *) _shmArea)->sonPid > 0) ) {
				openlog("Logs", LOG_PID, LOG_USER);
				syslog(LOG_INFO, "Killing SON and GRANDSON for device_id: %i", dev_id);
				closelog();
				}
	
			// if there are processes running, kill them (we kill SON and SON kills  grandson)	
			if ( ((shm_area *) _shmArea)->sonPid > 0) {
				int stat=0; 
				printf("\n\n KILLING SON with SIGTERM\n");
				kill(((shm_area *) _shmArea)->sonPid, SIGTERM);	
				sleep(2);
				printf("WAITING for SON to DIE! \n\n");
				waitpid(((shm_area *) _shmArea)->sonPid, &stat, 0);
				}

			// main FORK 
			_sonPID = fork();
			if ( _sonPID < 0)  { // Error
				printf("\n\n FORK ERROR !!! \n\n");
				exit(-1);
				}
			else if (_sonPID == 0) { // we are in the son
				launchWorkers (&retfd, &devd, dev_id);
				}
			else {		// in the father
				signal(SIGTERM, endSon);
				signal(SIGQUIT, endSon);
				signal(SIGINT, endSon);
				}	
			}

		sleep(30);	
		shmPrint( ((shm_area *) _shmArea) );
		}
	}


exit(0);
}

//------------------------------------------------------------------------
//------------------------------------------------------------------------

