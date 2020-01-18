/* Simple C program that connects to MySQL Database server*/
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <pthread.h>



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

char    server[200];
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
	if (!mysql_real_connect(_mysql_connection_handler, server, user, password, database, 0, NULL, 0))
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


int report_alarm(device_data *d, int n)
{
char        querystring[3000];

db_connect();

openlog ("TRAFMET", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

// compose QUERY String
if (  d->ifs[n].alarm_status == ALARM_ERROR )
    {
    sprintf(querystring, " INSERT INTO topology.alarms (alarm_prio, description, status, \
    informed) VALUES (%i, 'LOW TRAFFIC: %s-%s-%s  (%s/%s)', 1, 0);", d->ifs[n].prio_lo, d->ifs[n].peername, d->ifs[n].name, d->ifs[n].description, d->name, d->ip);

    // send SQL query
    mysql_query(_mysql_connection_handler, querystring);

    // also send it to PLAYER (speech program!)
	
	// verify priority
	if (d->ifs[n].prio_lo >= _speech_prio)
		{
		sprintf(querystring, " INSERT INTO topology.speech (toplay) VALUES ('atencion.wav caida_trafico_enlace_principal.wav ');");
		mysql_query(_mysql_connection_handler, querystring);
		}

    syslog (LOG_INFO, "TRAFMET LOW: : %s-%s-%s  (%s/%s)", d->ifs[n].peername, d->ifs[n].name, d->ifs[n].description, d->name, d->ip);
    }
else if (  d->ifs[n].alarm_status == ALARM_OK )
    {
    sprintf(querystring, " INSERT INTO topology.alarms (alarm_prio, description, status, \
    informed) VALUES (%i, 'TRAFFIC RESTORED: %s-%s-%s  (%s/%s)', 1, 0);", 0, d->ifs[n].peername, d->ifs[n].name, d->ifs[n].description, d->name, d->ip);

    // send SQL query
    mysql_query(_mysql_connection_handler, querystring);

    // also send it to PLAYER (speech program!)
//    sprintf(querystring, " INSERT INTO topology.speech (toplay) VALUES ('atencion.wav nodo_inaccesible.wav %s');", d->data[i].hablado);
//    mysql_query(_mysql_connection_handler, querystring);

    syslog (LOG_INFO, "TRAFMET RESTORED: : %s-%s-%s  (%s/%s)", d->ifs[n].peername, d->ifs[n].name, d->ifs[n].description, d->name, d->ip);

    }

if (_verbose > 3)
     printf("\n |%s| \n", querystring);

closelog ();
return(1);
}

//-------------------------------------------------------------------


int dbread (device_data *devd, int devid)
{
char 			query[2000];
int        	 	i=0;
MYSQL_RES   	*res;
MYSQL_ROW   	row;
iface_data		*iface1=NULL;

db_connect();

//  send SQL query
//a.to_epics, a.ep_ibw_pv, a.ep_ibw_a_pv, a.ep_ibw_b_pv, a.ep_ibw_c_pv, a.ep_obw_pv, a.ep_obw_a_pv, a.ep_obw_b_pv, a.ep_obw_c_pv

sprintf(query, "SELECT a.id, a.enable AS enable_bw, b.enable AS enable_dev, \
					   a.dev_id, a.if_name, b.nombre, b.ip, b.adm_acc, a.file_var_name, \
					   a.alarm_lo, a.prio_lo, \
					   a.exc_01_ini_h, a.exc_01_ini_m, a.exc_01_fin_h, a.exc_01_fin_m, \
					   a.exc_02_ini_h, a.exc_02_ini_m, a.exc_02_fin_h, a.exc_02_fin_m, \
					   a.description, b.getrunn, b.cli_acc, b.vendor_id, b.model_id, b.snmp \
					   FROM devices_bw a LEFT JOIN devices b \
					   ON a.dev_id=b.id WHERE a.enable>0 AND b.enable>0 AND dev_id=%i ;", devid);
if (mysql_query(_mysql_connection_handler, query))
    {
    fprintf(stderr, "%s\n", mysql_error(_mysql_connection_handler));
    sleep(1);
    return(0);
    }
res = mysql_use_result(_mysql_connection_handler);


// for deleted or disable interfaces:  we tag them as disable and then, if they area active, 
// they will be reacivated
for (i=0 ; i<devd->nInterfaces ; i++)
  devd->ifs[i].enable = 0;

while ((row = mysql_fetch_row(res)) != NULL)
    {
    int     ifId=0;
	int 	j=0, found=0;
    
    ifId = (row[0]) ? atoi(row[0]) : 0;

    // search for ID in existing data
    for (j=0, found=0 ; j<devd->nInterfaces && !found ; j++)
  	  if (devd->ifs[j].ifid == ifId)
		{
		iface1 = &(devd->ifs[j]);
        found=1;
		}

    if (!found)
  	  {
      if (( (devd->ifs) = realloc(devd->ifs, ((devd->nInterfaces) + 1) * sizeof(iface_data) )) != NULL )
    	{
        memset(&(devd->ifs[devd->nInterfaces]), 0, sizeof(iface_data) );
		iface1 = &(devd->ifs[devd->nInterfaces]);
		iface1->alarm_status = ALARM_OK;	// to avoid false alarms on startup
		devd->nInterfaces++;
		}
      else
      	perror("Error de asignacion de memoria");
	  }

	// at this point iface1 can be:
	// NULL (realloc failed) 
	// pointer to a (new) data,  allocated above
	// a pointer to existent data that we need to update
	iface1->enable = 1;
    iface1->ifid = ifId;

    devd->dev_id=(row[3]) ? atoi(row[3]) : 0;
	strcpy(iface1->name, (row[4]) ? (row[4]) : "" ); 
	strcpy(devd->name, (row[5]) ? (row[5]) : "" ); 
	strcpy(devd->ip, (row[6]) ? (row[6]) : "" ); 
    devd->access_type=(row[7]) ? atoi(row[7]) : 0;
	strcpy(iface1->file_var_name, (row[8]) ? (row[8]) : "" ); 
    iface1->alarm_lo=1000 * ((row[9]) ? atoi(row[9]) : 0);	// this limit must be in kbps
    iface1->prio_lo=(row[10]) ? atoi(row[10]) : 0;
    iface1->exc_01_ini_h = (row[11]) ? atoi(row[11]) : 0;
    iface1->exc_01_ini_m = (row[12]) ? atoi(row[12]) : 0;
    iface1->exc_01_fin_h = (row[13]) ? atoi(row[13]) : 0;
    iface1->exc_01_fin_m = (row[14]) ? atoi(row[14]) : 0;
    iface1->exc_02_ini_h = (row[15]) ? atoi(row[15]) : 0;
    iface1->exc_02_ini_m = (row[16]) ? atoi(row[16]) : 0;
    iface1->exc_02_fin_h = (row[17]) ? atoi(row[17]) : 0;
    iface1->exc_02_fin_m = (row[18]) ? atoi(row[18]) : 0;
    strcpy(iface1->peername, (row[19]) ? (row[19]) : "" ); 
    devd->getrunn = (row[20]) ? atoi(row[20]) : 0;
    devd->cli_acc = (row[21]) ? atoi(row[21]) : 0;
    devd->vendor_id = (row[22]) ? atoi(row[22]) : 0;
    devd->model_id = (row[23]) ? atoi(row[23]) : 0;
	devd->snmp = (row[24]) ? atoi(row[24]) : 0;
    }

if (_verbose > 2)
    {
    printf("\n Device: %i %s (%s) getrun: %i cliacc: %i, vendor:%i, model: %i \n", devd->dev_id, devd->name, devd->ip, devd->getrunn, devd->cli_acc, devd->vendor_id, devd->model_id);
	
    printf("\n Interfaces: \n");
	for (i=0 ; i<devd->nInterfaces ; i++)
        printf("%i:  ifid: %i  acc: %i  ifname: %s  fvarname: %s devn: %s  IP: %s alarm_lo: %i\n", i, devd->ifs[i].ifid, devd->access_type, devd->ifs[i].name, devd->ifs[i].file_var_name, devd->name, devd->ip, devd->ifs[i].alarm_lo);
    }

//  close connection
mysql_free_result(res);

return(devd->nInterfaces);
}

//-------------------------------------------------------------------

// load from database devices / interfaces to query via SNMP

int dbread (device_list *devList)
{
char 			query[2000];
int        	 	i=0, j=0;
MYSQL_RES   	*res;
MYSQL_ROW   	row;
iface_data		*ifaceAux = NULL;
device_data		*deviceAux = NULL;

db_connect();

//  send SQL query
sprintf(query, "SELECT a.id, a.dev_id, a.enable AS enable_bw, b.enable AS enable_dev, \
   a.if_name, b.nombre, b.ip, b.adm_acc, a.file_var_name, \
   a.alarm_lo, a.prio_lo, \
   a.description, b.getrunn, b.cli_acc, b.vendor_id, b.model_id, b.snmp \
   FROM devices_bw a LEFT JOIN devices b \
   ON a.dev_id=b.id WHERE a.enable>0 AND b.enable>0 AND b.snmp>0;");

if (mysql_query(_mysql_connection_handler, query))
    {
    fprintf(stderr, "%s\n", mysql_error(_mysql_connection_handler));
    sleep(1);
    return(0);
    }
res = mysql_use_result(_mysql_connection_handler);

// basic lock!
pthread_mutex_lock (&_threadMutex);

// for deleted or disable interfaces:  we tag them as disable and then, if they are active, 
// they will be reacivated
for (i=0 ; i<devList->nDevices ; i++)  {
  	devList->d[i].enable = 0;
	for (j=0 ; j < devList->d[i].nInterfaces ; j++)
		devList->d[i].ifs[j].enable = 0;
	}

while ((row = mysql_fetch_row(res)) != NULL)
    {
    int     ifId=0, devId=0;
	int 	i=0, j=0, deviceFound=0, interfaceFound=0;
    
    ifId = (row[0]) ? atoi(row[0]) : 0;
    devId = (row[1]) ? atoi(row[1]) : 0;

    // search for ID in existing device / interface

    for (i=0, deviceFound=0 ; i < devList->nDevices && ! (deviceFound && interfaceFound) ; i++) {
		if (devList[i].d->dev_id == devId) {
        	deviceFound=1;
			deviceAux = &(devList[i]);

    		for (j=0, interfaceFound=0 ; j<devList[i].d->nInterfaces && !interfaceFound ; j++) {
				if (devList[i].d->ifs[j].ifid == ifId) {
					ifaceAux = &( devList[i].d->ifs[j]);
        			interfaceFound=1;
				}
			}
		}	
	}		

    if (!interfaceFound)
  	  {
      if (( (devd->ifs) = realloc(devd->ifs, ((devd->ifs_n) + 1) * sizeof(iface_data) )) != NULL )
    	{
        memset(&(devd->ifs[devd->ifs_n]), 0, sizeof(iface_data) );
		ifaceAux = &(devd->ifs[devd->ifs_n]);
		ifaceAux->alarm_status = ALARM_OK;	// to avoid false alarms on startup
		devd->ifs_n++;
		}
      else
      	perror("Error de asignacion de memoria");
	  }

	// at this point iface1 can be:
	// NULL (realloc failed) 
	// pointer to a (new) data,  allocated above
	// a pointer to existent data that we need to update
	ifaceAux->enable = 1;
    ifaceAux->ifid = ifId;

    devd->dev_id=(row[3]) ? atoi(row[3]) : 0;
	strcpy(ifaceAux->name, (row[4]) ? (row[4]) : "" ); 
	strcpy(devd->name, (row[5]) ? (row[5]) : "" ); 
	strcpy(devd->ip, (row[6]) ? (row[6]) : "" ); 
    devd->access_type=(row[7]) ? atoi(row[7]) : 0;
	strcpy(ifaceAux->file_var_name, (row[8]) ? (row[8]) : "" ); 
    ifaceAux->alarm_lo=1000 * ((row[9]) ? atoi(row[9]) : 0);	// this limit must be in kbps
    ifaceAux->prio_lo=(row[10]) ? atoi(row[10]) : 0;
    strcpy(ifaceAux->peername, (row[19]) ? (row[19]) : "" ); 
    devd->getrunn = (row[20]) ? atoi(row[20]) : 0;
    devd->cli_acc = (row[21]) ? atoi(row[21]) : 0;
    devd->vendor_id = (row[22]) ? atoi(row[22]) : 0;
    devd->model_id = (row[23]) ? atoi(row[23]) : 0;
	devd->snmp = (row[24]) ? atoi(row[24]) : 0;
    }

// unlock!
pthread_mutex_unlock (&_threadMutex);


if (_verbose > 2)
    {
    printf("\n Device: %i %s (%s) getrun: %i cliacc: %i, vendor:%i, model: %i \n", devd->dev_id, devd->name, devd->ip, devd->getrunn, devd->cli_acc, devd->vendor_id, devd->model_id);
	
    printf("\n Interfaces: \n");
	for (i=0 ; i<devd->ifs_n ; i++)
        printf("%i:  ifid: %i  acc: %i  ifname: %s  fvarname: %s devn: %s  IP: %s alarm_lo: %i\n", i, devd->ifs[i].ifid, devd->access_type, devd->ifs[i].name, devd->ifs[i].file_var_name, devd->name, devd->ip, devd->ifs[i].alarm_lo);
    }

//  close connection
mysql_free_result(res);

return(devList->nDevices);
}

//-------------------------------------------------------------------

// delete specific device data from MEMORY DB

int delete_from_db_mem (int devid)
{
char        querystring[3000];

db_connect();

sprintf(querystring, "DELETE FROM topology.devices_bw_mem WHERE dev_id=%i ;", devid);

if (_verbose > 3)
  printf("\n |%s| \n", querystring);

mysql_query(_mysql_connection_handler, querystring);

return(0);
}


//-------------------------------------------------------------------

// send instant data to MEMORY DB

int to_db_mem (device_data *d)
{
char        querystring[3000];
int 		i=0;

db_connect();

for (i=0 ; i<d->nInterfaces ; i++)
  {
  if (d->ifs[i].enable > 0)
	{
	sprintf(querystring, "INSERT INTO topology.devices_bw_mem ( devid, ifid, ifalias, tdate, \
				tstamp, ibytes, obytes, ibw, obw, ibw_a, obw_a, ibw_b, obw_b, ibw_c, obw_c) \
				VALUES ( %i, %i, '%s', NOW(), NOW(), %lli, %lli, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf, %.2lf ) \
				ON DUPLICATE KEY UPDATE devid=%i, ifid=%i, ifalias='%s', tdate=NOW(), \
				tstamp=NOW(), ibytes=%lli, obytes=%lli, ibw=%.2lf, obw=%.2lf, ibw_a=%.2lf, obw_a=%.2lf, \
				 ibw_b=%.2lf, obw_b=%.2lf, ibw_c=%.2lf, obw_c=%.2lf; ", 
                d->dev_id, d->ifs[i].ifid, d->ifs[i].description, d->ifs[i].ibytes, d->ifs[i].obytes, 
				d->ifs[i].ibw/1000, d->ifs[i].obw/1000, d->ifs[i].ibw_a/1000, d->ifs[i].obw_a/1000, 
				d->ifs[i].ibw_b/1000, d->ifs[i].obw_b/1000, d->ifs[i].ibw_c/1000, d->ifs[i].obw_c/1000, 
                d->dev_id, d->ifs[i].ifid, d->ifs[i].description, d->ifs[i].ibytes, d->ifs[i].obytes, 
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
// send data to historic DB

int to_db_hist (device_data *d)
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
                d->dev_id, d->ifs[i].ifid, d->ifs[i].ibytes, d->ifs[i].obytes, 
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

int update_devices_mem_ping_time(int dev_id)
{
char                sqlquery[200];
int                 retval=0;

db_connect();

sprintf(sqlquery, "UPDATE topology.devices_mem SET last_icmp_ok=NOW() WHERE id=%i;", dev_id);
mysql_query(_mysql_connection_handler, sqlquery);

return(retval);
}

//-------------------------------------------------------------------
//-------------------------------------------------------------------


