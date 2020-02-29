/* Simple C program that connects to MySQL Database server*/
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <pthread.h>
#include <arpa/inet.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#include <sys/types.h>
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if TIME_WITH_SYS_TIME
# ifdef WIN32
#  include <sys/timeb.h>
# else
#  include <sys/time.h>
# endif
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <stdio.h>
#include <ctype.h>
#if HAVE_WINSOCK_H
#include <winsock.h>
#endif
#if HAVE_NETDB_H
#include <netdb.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif


#include <sys/timeb.h>
#include "ifaceData.h"

char    _server[200];
char    *user = "nocuser";
char    *password = "AguasaguaS";
char    *database = "topology";

int     _verbose;

extern MYSQL       	*_mysql_connection_handler;


extern 	int	_speech_prio;		// minimum priority value to trigger speech messages

//-------------------------------------------------------------------
//-------------------------------------------------------------------

int db_connect()
{
int tout = 30;

if (_mysql_connection_handler == NULL)
	{
	_mysql_connection_handler = mysql_init(NULL);

	mysql_options(_mysql_connection_handler, MYSQL_OPT_CONNECT_TIMEOUT, &tout);
	mysql_options(_mysql_connection_handler, MYSQL_OPT_READ_TIMEOUT, &tout);
	mysql_options(_mysql_connection_handler, MYSQL_OPT_WRITE_TIMEOUT, &tout);

	// Connect to database
	if (!mysql_real_connect(_mysql_connection_handler, _server, user, password, database, 0, NULL, 0))
    	{
	    fprintf(stderr, "%s\n", mysql_error(_mysql_connection_handler));
    	sleep(1);
	    return(0);
    	}
    }
	
return(1);
}	

//-------------------------------------------------------------------

int db_disconnect()
{
if (_mysql_connection_handler)
	{	
	mysql_close(_mysql_connection_handler);
	_mysql_connection_handler = NULL;
	}
	
return(1);
}

//-------------------------------------------------------------------

int report_alarm(deviceData *d, interfaceData *iface)
{
char        querystring[3000];

db_connect();

openlog ("TRAFMET", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

// compose QUERY String
if (  iface->alarm_status == ALARM_ERROR )
    {
    sprintf(querystring, " INSERT INTO topology.alarms (alarm_prio, description, status, \
    informed) VALUES (%i, 'LOW TRAFFIC: %s-%s-%s  (%s/%s)', 1, 0);", iface->prio_lo, iface->peername, iface->name, iface->description, d->name, d->ip);

    // send SQL query
    mysql_query(_mysql_connection_handler, querystring);

    // also send it to PLAYER (speech program!)
	
	// verify priority
	if (iface->prio_lo >= _speech_prio)
		{
		sprintf(querystring, " INSERT INTO topology.speech (toplay) VALUES ('atencion.wav caida_trafico_enlace_principal.wav ');");
		mysql_query(_mysql_connection_handler, querystring);
		}

    syslog (LOG_INFO, "TRAFMET LOW: : %s-%s-%s  (%s/%s)", iface->peername, iface->name, iface->description, d->name, d->ip);
    }
else if (  iface->alarm_status == ALARM_OK )
    {
    sprintf(querystring, " INSERT INTO topology.alarms (alarm_prio, description, status, \
    informed) VALUES (%i, 'TRAFFIC RESTORED: %s-%s-%s  (%s/%s)', 1, 0);", 0, iface->peername, iface->name, iface->description, d->name, d->ip);

    // send SQL query
    mysql_query(_mysql_connection_handler, querystring);

    // also send it to PLAYER (speech program!)
//    sprintf(querystring, " INSERT INTO topology.speech (toplay) VALUES ('atencion.wav nodo_inaccesible.wav %s');", d->data[i].hablado);
//    mysql_query(_mysql_connection_handler, querystring);

    syslog (LOG_INFO, "TRAFMET RESTORED: : %s-%s-%s  (%s/%s)", iface->peername, iface->name, iface->description, d->name, d->ip);

    }

if (_verbose > 3)
     printf("\n |%s| \n", querystring);

closelog ();
return(1);
}

//-------------------------------------------------------------------

// load from database devices / interfaces to query via SNMP

int dbread (devicesShm *shmDev, interfacesShm *shmInt)  {

char 			query[2000];
int        	 	i=0, j=0;
MYSQL_RES   	*res;
MYSQL_ROW   	row;
interfaceData	ifaceAux;
deviceData		deviceAux;

if ( !shmDev || !shmInt )  {
	printf("\n\n dbread: shmDev and shmInt cannot be NULL!");
	fflush(stdout);
	return(-1);
	}

db_connect();

//  send SQL query
sprintf(query, "SELECT devBW.id, devBW.dev_id, devBW.enable AS enable_bw, dev.enable AS enable_dev, \
   devBW.if_name, dev.nombre, dev.ip, dev.adm_acc, devBW.file_var_name, \
   devBW.alarm_lo, devBW.prio_lo, \
   devBW.description, dev.getrunn, dev.cli_acc, dev.vendor_id, dev.model_id, dev.snmp, devBW.cir2, devBW.cir_tec \
   FROM devices_bw devBW LEFT JOIN devices dev \
   ON devBW.dev_id=dev.id WHERE devBW.enable>0 AND dev.enable>0 AND dev.snmp>0 \
   ORDER BY devBW.dev_id;");

if (mysql_query(_mysql_connection_handler, query))
    {
    fprintf(stderr, "%s\n", mysql_error(_mysql_connection_handler));
    sleep(1);
    return(0);
    }
res = mysql_use_result(_mysql_connection_handler);

// safe lock!
pthread_mutex_lock (& (shmDev->lock));
pthread_mutex_lock (& (shmInt->lock));

// for deleted or disable interfaces / devices:  
// we tag them as disable and then, if they are active, 
// they will be reacivated
for (i=0 ; i<shmDev->nDevices ; i++)
  	shmDev->d[i].enable = 0;
for (j=0 ; j <  shmInt->nInterfaces ; j++)
	shmInt->d[j].enable = 0;

// get every row (device - interface)
while ((row = mysql_fetch_row(res)) != NULL) {
	int 	i=0, j=0, deviceFound=0, interfaceFound=0;

	memset(&deviceAux, 0, sizeof(deviceData));
	memset(&ifaceAux, 0, sizeof(interfaceData));

	deviceAux.deviceId = (row[1]) ? atoi(row[1]) : 0;
	ifaceAux.deviceId =  deviceAux.deviceId;
	ifaceAux.interfaceId = (row[0]) ? atoi(row[0]) : 0;
    ifaceAux.enable = (row[2]) ? atoi(row[2]) : 0;
    deviceAux.enable = (row[3]) ? atoi(row[3]) : 0;

	strcpy(ifaceAux.name, (row[4]) ? (row[4]) : "" ); 
	strcpy(deviceAux.name, (row[5]) ? (row[5]) : "" ); 
	strcpy(ifaceAux.deviceName, (row[5]) ? (row[5]) : "" ); 
	strcpy(deviceAux.ip, trim((row[6]) ? (row[6]) : "") ); 
	deviceAux.ipAddr32 = inet_addr(deviceAux.ip);
    deviceAux.access_type=(row[7]) ? atoi(row[7]) : 0;
	strcpy(ifaceAux.file_var_name, (row[8]) ? (row[8]) : "" ); 
    ifaceAux.alarm_lo=1000 * ((row[9]) ? atoi(row[9]) : 0);	// this limit must be in kbps
    ifaceAux.prio_lo=(row[10]) ? atoi(row[10]) : 0;
    strcpy(ifaceAux.peername, (row[11]) ? (row[11]) : "" ); 
    deviceAux.getrunn = (row[12]) ? atoi(row[12]) : 0;
    deviceAux.cli_acc = (row[13]) ? atoi(row[13]) : 0;
    deviceAux.vendor_id = (row[14]) ? atoi(row[14]) : 0;
    deviceAux.model_id = (row[15]) ? atoi(row[15]) : 0;
	deviceAux.snmp = (row[16]) ? atoi(row[16]) : 0;
    ifaceAux.cirCom=(row[17]) ? atoll(row[17]) : 0;
    ifaceAux.cirTec=(row[18]) ? atoll(row[18]) : 0;

	// search for device in shm
	for (i=0, deviceFound=0 ; i<shmDev->nDevices ; i++) {
		if (shmDev->d[i].deviceId == deviceAux.deviceId) {
			deviceFound = 1;

			if (strcmp(deviceAux.ip, shmDev->d[j].ip)) {  // something change! -> reconfigure
				memmove( &(shmDev->d[i]), &deviceAux, sizeof(deviceData));
				shmDev->d[i].snmpConfigured = 0;
			}
			else {   // no changes, keep old values (some of them)
				deviceAux.snmpConfigured = shmDev->d[i].snmpConfigured;
				memmove( &(shmDev->d[i]), &deviceAux, sizeof(deviceData));
				shmDev->d[i].snmpConfigured = deviceAux.snmpConfigured;
				shmDev->d[i].snmpCaptured = 0;
			}
			break;  // no need to continue on the loop
			}
		}

	if (!deviceFound) {
		memmove( &(shmDev->d[ shmDev->nDevices  ]), &deviceAux, sizeof(deviceData));
		shmDev->nDevices++;
		}

	// search for interface in shm
	for (j=0, interfaceFound=0 ; j<shmInt->nInterfaces ; j++) {
		if (shmInt->d[j].deviceId == ifaceAux.deviceId && shmInt->d[j].interfaceId == ifaceAux.interfaceId ) {
			interfaceFound= 1;
			shmInt->d[j].enable = ifaceAux.enable;
			shmInt->d[j].cirCom = ifaceAux.cirCom;
			shmInt->d[j].cirTec = ifaceAux.cirTec;
			break;  // no need to continue on the loop
			}
		}

	if (!interfaceFound) {
		memmove( &(shmInt->d[ shmInt->nInterfaces ]), &ifaceAux, sizeof(interfaceData) );
		shmInt->nInterfaces++;
		}
	}

// update nInterfaces for each device	
for (i=0 ; i<shmDev->nDevices ; i++)  {
	shmDev->d[i].nInterfaces = 0;
	for (j=0 ; j < shmInt->nInterfaces ; j++) {
		if (shmDev->d[i].deviceId == shmInt->d[j].deviceId)
			shmDev->d[i].nInterfaces++;
		}
	}	

pthread_mutex_unlock (& (shmDev->lock));
pthread_mutex_unlock (& (shmInt->lock));

//  close connection
mysql_free_result(res);

if (_verbose > 2) {
	for (i=0 ; i<shmDev->nDevices ; i++)  {
	    deviceData *d = &(shmDev->d[i]);
	
		printf("\n\n Device: (%i) %i -->  %s (%s) enable: %i getrun: %i cliacc: %i, vendor:%i, model: %i ifs: %i", i, d->deviceId, d->name, d->ip, d->enable, d->getrunn, d->cli_acc, d->vendor_id, d->model_id, d->nInterfaces);
		for (j=0 ; j < shmInt->nInterfaces ; j++) {
			interfaceData *ifs = &(shmInt->d[j]); 
			if (d->deviceId == ifs->deviceId)	
	        	printf("\n      %i:  devid: %i  ifid: %i  enable: %i  ifname: %s  fvarname: %s alarm_lo: %i  ", j, ifs->deviceId, ifs->interfaceId, ifs->enable, ifs->name, ifs->file_var_name, ifs->alarm_lo);
			}
		}	
	fflush(stdout);	
	}

return(shmDev->nDevices);
}

//-------------------------------------------------------------------


// load from database devices / interfaces to query via SNMP

int dbreadOneDevice (devicesShm *shmDev, interfacesShm *shmInt, int deviceId)  {

char 			query[2000];
int        	 	i=0, j=0;
MYSQL_RES   	*res;
MYSQL_ROW   	row;
interfaceData	ifaceAux;
deviceData		deviceAux;

if ( !shmDev || !shmInt )  {
	printf("\n\n dbread: shmDev and shmInt cannot be NULL!");
	fflush(stdout);
	return(-1);
	}

db_connect();

//  send SQL query
sprintf(query, "SELECT a.id, a.dev_id, a.enable AS enable_bw, b.enable AS enable_dev, \
   a.if_name, b.nombre, b.ip, b.adm_acc, a.file_var_name, \
   a.alarm_lo, a.prio_lo, \
   a.description, b.getrunn, b.cli_acc, b.vendor_id, b.model_id, b.snmp, a.cir2 \
   FROM devices_bw a LEFT JOIN devices b \
   ON a.dev_id=b.id WHERE a.enable>0 AND b.enable>0 AND b.id=%i \
   ORDER BY a.dev_id;", deviceId);

if (mysql_query(_mysql_connection_handler, query))
    {
    fprintf(stderr, "%s\n", mysql_error(_mysql_connection_handler));
    sleep(1);
    return(0);
    }
res = mysql_use_result(_mysql_connection_handler);

// safe lock!
pthread_mutex_lock (& (shmDev->lock));
pthread_mutex_lock (& (shmInt->lock));

// for deleted or disable interfaces / devices:  
// we tag them as disable and then, if they are active, 
// they will be reacivated
for (i=0 ; i<shmDev->nDevices ; i++)
  	shmDev->d[i].enable = 0;
for (j=0 ; j <  shmInt->nInterfaces ; j++)
	shmInt->d[j].enable = 0;

// get every row (device - interface)
while ((row = mysql_fetch_row(res)) != NULL) {
	int 	i=0, j=0, deviceFound=0, interfaceFound=0;

	memset(&deviceAux, 0, sizeof(deviceData));
	memset(&ifaceAux, 0, sizeof(interfaceData));

	deviceAux.deviceId = (row[1]) ? atoi(row[1]) : 0;
	ifaceAux.deviceId =  deviceAux.deviceId;
	ifaceAux.interfaceId = (row[0]) ? atoi(row[0]) : 0;
    ifaceAux.enable = (row[2]) ? atoi(row[2]) : 0;
    deviceAux.enable = (row[3]) ? atoi(row[3]) : 0;

	strcpy(ifaceAux.name, (row[4]) ? (row[4]) : "" ); 
	strcpy(deviceAux.name, (row[5]) ? (row[5]) : "" ); 
	strcpy(deviceAux.ip, (row[6]) ? (row[6]) : "" ); 
    deviceAux.access_type=(row[7]) ? atoi(row[7]) : 0;
	strcpy(ifaceAux.file_var_name, (row[8]) ? (row[8]) : "" ); 
    ifaceAux.alarm_lo=1000 * ((row[9]) ? atoi(row[9]) : 0);	// this limit must be in kbps
    ifaceAux.prio_lo=(row[10]) ? atoi(row[10]) : 0;
    strcpy(ifaceAux.peername, (row[11]) ? (row[11]) : "" ); 
    deviceAux.getrunn = (row[12]) ? atoi(row[12]) : 0;
    deviceAux.cli_acc = (row[13]) ? atoi(row[13]) : 0;
    deviceAux.vendor_id = (row[14]) ? atoi(row[14]) : 0;
    deviceAux.model_id = (row[15]) ? atoi(row[15]) : 0;
	deviceAux.snmp = (row[16]) ? atoi(row[16]) : 0;
    ifaceAux.cirCom=(row[17]) ? atoll(row[17]) : 0;

	// search for device in shm
	for (i=0, deviceFound=0 ; i<shmDev->nDevices ; i++) {
		if (shmDev->d[i].deviceId == deviceAux.deviceId) {
			deviceFound = 1;

			if (strcmp(deviceAux.ip, shmDev->d[j].ip)) {  // something change! -> reconfigure
				memmove( &(shmDev->d[i]), &deviceAux, sizeof(deviceData));
				shmDev->d[i].snmpConfigured = 0;
			}
			else {   // no changes, keep old values (some of them)
				deviceAux.snmpConfigured = shmDev->d[i].snmpConfigured;
				memmove( &(shmDev->d[i]), &deviceAux, sizeof(deviceData));
				shmDev->d[i].snmpConfigured = deviceAux.snmpConfigured;
				shmDev->d[i].snmpCaptured = 0;
			}
			break;  // no need to continue on the loop
			}
		}

	if (!deviceFound) {
		memmove( &(shmDev->d[ shmDev->nDevices  ]), &deviceAux, sizeof(deviceData));
		shmDev->nDevices++;
		}

	// search for interface in shm
	for (j=0, interfaceFound=0 ; j<shmInt->nInterfaces ; j++) {
		if (shmInt->d[j].deviceId == ifaceAux.deviceId && shmInt->d[j].interfaceId == ifaceAux.interfaceId ) {
			interfaceFound= 1;
			shmInt->d[j].enable = ifaceAux.enable;
			shmInt->d[j].cirCom = ifaceAux.cirCom;
			break;  // no need to continue on the loop
			}
		}

	if (!interfaceFound) {
		memmove( &(shmInt->d[ shmInt->nInterfaces ]), &ifaceAux, sizeof(interfaceData) );
		shmInt->nInterfaces++;
		}
	}

// update nInterfaces for each device	
for (i=0 ; i<shmDev->nDevices ; i++)  {
	shmDev->d[i].nInterfaces = 0;
	for (j=0 ; j < shmInt->nInterfaces ; j++) {
		if (shmDev->d[i].deviceId == shmInt->d[j].deviceId)
			shmDev->d[i].nInterfaces++;
		}
	}	

pthread_mutex_unlock (& (shmDev->lock));
pthread_mutex_unlock (& (shmInt->lock));

//  close connection
mysql_free_result(res);

if (_verbose > 2) {
	for (i=0 ; i<shmDev->nDevices ; i++)  {
	    deviceData *d = &(shmDev->d[i]);
	
		printf("\n\n Device: (%i) %i -->  %s (%s) enable: %i getrun: %i cliacc: %i, vendor:%i, model: %i ifs: %i", i, d->deviceId, d->name, d->ip, d->enable, d->getrunn, d->cli_acc, d->vendor_id, d->model_id, d->nInterfaces);

		printf("\n\n Device: (%i) %i -->  %s (%s) enable: %i getrun: %i cliacc: %i, vendor:%i, model: %i ifs: %i", i, d->deviceId, d->name, d->ip, d->enable, d->getrunn, d->cli_acc, d->vendor_id, d->model_id, d->nInterfaces);
		for (j=0 ; j < shmInt->nInterfaces ; j++) {
			interfaceData *ifs = &(shmInt->d[j]); 
			if (d->deviceId == ifs->deviceId)	
	        	printf("\n      %i:  devid: %i  ifid: %i  enable: %i  ifname: %s  fvarname: %s alarm_lo: %i  ", j, ifs->deviceId, ifs->interfaceId, ifs->enable, ifs->name, ifs->file_var_name, ifs->alarm_lo);
			}
		}	
	fflush(stdout);	
	}

return(shmDev->nDevices);
}

//-------------------------------------------------------------------

// delete specific device data from MEMORY DB

int delete_from_db_mem ()
{
//char        querystring[3000];

//db_connect();

//sprintf(querystring, "DELETE FROM topology.devices_bw_mem WHERE dev_id=%i ;", devid);
//
//if (_verbose > 3)
//  printf("\n |%s| \n", querystring);
//
//mysql_query(_mysql_connection_handler, querystring);

return(0);
}


//-------------------------------------------------------------------

// send instant data to MEMORY DB

int to_db_mem (interfaceData *d)
{
char        querystring[3000];

db_connect();

  if (d->enable > 0)
	{
	sprintf(querystring,"INSERT INTO topology.devices_bw_mem ( devid, ifid, ifalias, tdate, \
				tstamp, ibytes, obytes, ibw, obw, ibw_a, obw_a, ibw_b, obw_b, ibw_c, obw_c \
				last_snmp_ok, last_icmp_ok, snmp_config_ok, oid_found) \
				VALUES ( %i, %i, '%s', NOW(), NOW(), %lli, %lli, %.2lf, %.2lf, %.2lf, %.2lf, \
				%.2lf, %.2lf, %.2lf, %.2lf, %li, %li, %i, %i ) \
				ON DUPLICATE KEY UPDATE devid=%i, ifid=%i, ifalias='%s', tdate=NOW(), \
				tstamp=NOW(), ibytes=%lli, obytes=%lli, ibw=%.2lf, obw=%.2lf, ibw_a=%.2lf, obw_a=%.2lf, \
				ibw_b=%.2lf, obw_b=%.2lf, ibw_c=%.2lf, obw_c=%.2lf, \
				last_snmp_ok=%li, last_icmp_ok=%li, snmp_config_ok=%i, oid_found=%i;",
                d->deviceId, d->interfaceId, d->description, d->ibytes, d->obytes,
				d->ibw/1000, d->obw/1000, d->ibw_a/1000, d->obw_a/1000,
				d->ibw_b/1000, d->obw_b/1000, d->ibw_c/1000, d->obw_c/1000,
				d->lastSNMPOK, d->lastPingOK, d->snmpDeviceOK, d->snmpOIDOk, 
                d->deviceId, d->interfaceId, d->description, d->ibytes, d->obytes, 
				d->ibw/1000, d->obw/1000, d->ibw_a/1000, d->obw_a/1000, 
				d->ibw_b/1000, d->obw_b/1000, d->ibw_c/1000, d->obw_c/1000, 
				d->lastSNMPOK, d->lastPingOK, d->snmpDeviceOK, d->snmpOIDOk);


	if (_verbose > 3)
  	  printf("\n |%s| \n", querystring);

    mysql_query(_mysql_connection_handler, querystring);
	}

return(0);
}


//-------------------------------------------------------------------
// send data to historic DB

int to_db_hist (deviceData *d)
{
char        querystring[3000];
int 		i=0;

db_connect();

for (i=0 ; i<d->nInterfaces ; i++)
  {
  if (d->ifs[i].enable > 0)
	{
	sprintf(querystring, "INSERT INTO topology.devices_bw_hist ( devid, ifid, tdate, \
				tstamp, ibytes, obytes, ibw, obw, ibw_a, obw_a, ibw_b, obw_b, ibw_c, obw_c) \
				VALUES ( %i, %i, NOW(), NOW(), %lli, %lli, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf );",
                d->deviceId, d->ifs[i].interfaceId, d->ifs[i].ibytes, d->ifs[i].obytes, 
				d->ifs[i].ibw/1000, d->ifs[i].obw/1000, d->ifs[i].ibw_a/1000, d->ifs[i].obw_a/1000, 
				d->ifs[i].ibw_b/1000, d->ifs[i].obw_b/1000, d->ifs[i].ibw_c/1000, d->ifs[i].obw_c/1000);


	if (_verbose > 3)
  	  printf("\n |%s| \n", querystring);

    mysql_query(_mysql_connection_handler, querystring);
	}
  }   
return(0);
}


//-------------------------------------------------------------------

int db_keepalive(char *name)
{
static int          counter=0;
pid_t        	    pid=0;
char                sqlquery[200];
int                 retval=0;

pid = getpid();
db_connect();

sprintf(sqlquery, "INSERT INTO topology.process_status (name, pid, counter) VALUES ('%s', %i, %i) ON DUPLICATE KEY UPDATE pid=%i, counter=%i ;", name, pid, counter, pid, counter);
counter = (++counter > 10000) ? 0 : counter;
mysql_query(_mysql_connection_handler, sqlquery);

return(retval);
}

//-------------------------------------------------------------------

int update_devices_mem(deviceData *d)
{
char                sqlquery[200];
int                 retval=0;

db_connect();

sprintf(sqlquery, "UPDATE topology.devices_mem SET last_icmp_ok=FROM_UNIXTIME(%li),  last_snmp_ok=FROM_UNIXTIME(%li) WHERE id=%i;", d->lastPingOK, d->lastSNMPOK, d->deviceId);
//printf("\n --- %s ---", sqlquery); fflush(stdout);
mysql_query(_mysql_connection_handler, sqlquery);

return(retval);
}

//-------------------------------------------------------------------
//-------------------------------------------------------------------


