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

//------------------------------------------------------------------------                                   
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
int 				i=0, j=0, counter=0;
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
			counter = 0;
			while(c != '\n' && c != '\r' && ++counter < 2000) {
				fseek(f, -2, SEEK_CUR);
				c = fgetc(f);
				}

			if (c != '\n' && c != '\r')	{
				fprintf(stderr, "\n\n UNABLE to Retrieve LAST line of file:  %s", ifs->file_var_name);
				continue;
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
		else  {
			fprintf(stderr, "\n\n UNABLE to Retrieve data from file:  %s", ifs->file_var_name);
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
	if ( ! ( ping(d->ip, 2000, NULL)==0 || ping(d->ip, 2000, NULL)==0 || ping(d->ip, 2000, NULL)==0 ) ) {
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
