 //
 // snmp routines for traffic grabbing 
 // First, the program gets Interface index from IF-TABLE searching by NAME (name MUST BE EXACT!)
 // that ID is then used to get IN / OUT octets counters
 //

#include <net-snmp/net-snmp-config.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>


#include <sys/select.h>
#include <stdio.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <net-snmp/net-snmp-includes.h>

#define NETSNMP_DS_WALK_INCLUDE_REQUESTED	        1
#define NETSNMP_DS_WALK_PRINT_STATISTICS	        2
#define NETSNMP_DS_WALK_DONT_CHECK_LEXICOGRAPHIC	3
#define NETSNMP_DS_WALK_TIME_RESULTS     	        4
#define NETSNMP_DS_WALK_DONT_GET_REQUESTED	        5
#define NETSNMP_DS_WALK_TIME_RESULTS_SINGLE	        6

#define NETSNMP_DS_APP_DONT_FIX_PDUS                0

#include <sys/timeb.h>

#include "ifaceData.h"                                                                                                  

char           *end_name = NULL;

//-------------------------------------------------------------

// authentication handling...
// for SNMP (v1) we just try to figure out community name...

int 	snmp_authenticate( device_data *d, int infd, int outfd )
{

if ( snmpGetSysDesrc (SNMP_VERSION_2c, "Vostok3KA", d->ip, NULL) == 0 ) {
    strcpy(d->snmpCommunity, "Vostok3KA");
    d->snmpVersion = SNMP_VERSION_2c;
    }
else if ( snmpGetSysDesrc (SNMP_VERSION_2c, "public", d->ip, NULL) == 0) {
    strcpy(d->snmpCommunity, "public");
    d->snmpVersion = SNMP_VERSION_2c;
    }
else if ( snmpGetSysDesrc (SNMP_VERSION_1, "Vostok3KA", d->ip, NULL) == 0 ) {
    strcpy(d->snmpCommunity, "Vostok3KA");
    d->snmpVersion = SNMP_VERSION_1;
    }
else if ( snmpGetSysDesrc (SNMP_VERSION_1, "public", d->ip, NULL) == 0) {
    strcpy(d->snmpCommunity, "public");
    d->snmpVersion = SNMP_VERSION_1;
    }
else {
    printf("\n\n Unable to contact device with SNMP version 1 / 2c and given community \n\n");
    sleep(5);
    exit(0);
}

if ( snmpVerifyIfXTable (d->snmpVersion, d->snmpCommunity, d->ip, NULL ) == 0)
    d->use64bitsCounters = 1;

// get interface position in IF-TABLE. In case of error we cannot continue...
if ( getIndexOfInterfaces( d->snmpVersion, d, "1.3.6.1.2.1.2.2.1.2", d->snmpCommunity, d->ip) != 0 ) {
    printf("\n\n UNABLE to find some Interfaces  !! \n\n" );
    exit (-1);
    }

return(1);
}

//------------------------------------------------------------------------

// in the case of SNMP we do nothing....

int snmp_disconnect( device_data *d, int infd, int outfd )
{
if (_verbose > 4)
  printf("\n SNMP Disconnection (stub function, nothing done!) \n");
  
fflush(stdout);  
return(1);
}

//------------------------------------------------------------------------

// this routine parses snmp values captured from IF-TABLE
// d: device data structure
// n: index of interface to capture

int snmp_parse_bw( device_data *d, int n, char *b)
{
unsigned long long int   		lli1=0, lli2=0;
struct timeb 			stb;
double					delta_t=0;
char                    inCounterOid[400],outCounterOid[400];

ftime(&stb); 
d->ifs[n].last_access=stb;
d->ifs[n].msec_prev = d->ifs[n].msec;
d->ifs[n].msec = stb.time*1000+stb.millitm;
delta_t =  ((double)(d->ifs[n].msec - d->ifs[n].msec_prev))/1000; // delta t in seconds 

d->ifs[n].ibytes_prev_prev = d->ifs[n].ibytes_prev;
d->ifs[n].obytes_prev_prev = d->ifs[n].obytes_prev;
d->ifs[n].ibytes_prev = d->ifs[n].ibytes;
d->ifs[n].obytes_prev = d->ifs[n].obytes;

// we set this values to 0 to take some action below
d->ifs[n].ibytes = 0;
d->ifs[n].obytes = 0;

if (d->use64bitsCounters) {
    sprintf(inCounterOid, "1.3.6.1.2.1.31.1.1.1.6.%s", d->ifs[n].oidIndex);
    sprintf(outCounterOid, "1.3.6.1.2.1.31.1.1.1.10.%s", d->ifs[n].oidIndex);
    }
else {
sprintf(inCounterOid, "1.3.6.1.2.1.2.2.1.10.%s", d->ifs[n].oidIndex);
sprintf(outCounterOid, "1.3.6.1.2.1.2.2.1.16.%s", d->ifs[n].oidIndex);
}

if ( getInOutCounters (d->snmpVersion, d->snmpCommunity, d->ip, inCounterOid, outCounterOid, &lli1,  &lli2) != 0) 
    printf("\n ERROR getting IN / OUT counters !!! \n");

d->ifs[n].ibytes = lli1;
d->ifs[n].obytes = lli2;

if ( d->ifs[n].ibytes_prev == 0 ) // just starting, don't do anything
  {
  }
else if ( d->ifs[n].ibytes_prev > d->ifs[n].ibytes || d->ifs[n].obytes_prev > d->ifs[n].obytes ) // prev > current, there was a clear counter, counters return to 0, etc, don't do any calculations
  {
      // do nothing, we can figure out results though....
  }
else
  {
  double auxin=0;
  double auxout=0;

  auxin = (8*(double)(d->ifs[n].ibytes - d->ifs[n].ibytes_prev)) / delta_t;
  if (auxin < (((long long int)10) * 1000 * 1000 * 1000))  // < 10Gbps 
	d->ifs[n].ibw =  auxin;
  
  auxout = (8*(double)(d->ifs[n].obytes - d->ifs[n].obytes_prev)) / delta_t;
  if (auxout < (((long long int)10) * 1000 * 1000 * 1000))  // < 10Gbps 
	d->ifs[n].obw =  auxout;  

  if ( d->ifs[n].ibytes_prev_prev == 0 ) // second pass after starting the program, it's a good idea to use current 'instant' traffic as average!
	{
	int j=0;

	for (j=0 ; j<MAXAVGBUF ; j++)	// copy FIRST sample to the WHOLE buffer
		d->ifs[n].ibw_buf[j] = d->ifs[n].ibw;
	d->ifs[n].ibw_a	= d->ifs[n].ibw_b = d->ifs[n].ibw_c = d->ifs[n].ibw;

	for (j=0 ; j<MAXAVGBUF ; j++)	// copy FIRST sample to the WHOLE buffer
		d->ifs[n].obw_buf[j] = d->ifs[n].obw;
	d->ifs[n].obw_a	= d->ifs[n].obw_b = d->ifs[n].obw_c = d->ifs[n].obw;
	}
  else
  	  {
	  // shift and copy AVG buffer.  Last value is always in the first position, previous in the second an so on...
	  memmove( &(d->ifs[n].ibw_buf[1]), &(d->ifs[n].ibw_buf[0]), sizeof((d->ifs[n].ibw_buf[0])) * (MAXAVGBUF - 1) );  
	  d->ifs[n].ibw_buf[0] = d->ifs[n].ibw;
	  
	  d->ifs[n].ibw_a =   0.5 * d->ifs[n].ibw + 0.5 * d->ifs[n].ibw_a;
	  d->ifs[n].ibw_b =   0.1 * d->ifs[n].ibw + 0.9 * d->ifs[n].ibw_b;
	  d->ifs[n].ibw_c =   0.02 * d->ifs[n].ibw + 0.98 * d->ifs[n].ibw_c;

	  // shift and copy AVG buffer.  Last value is always in the first position, previous in the second an so on...
	  memmove( &(d->ifs[n].obw_buf[1]), &(d->ifs[n].obw_buf[0]), sizeof((d->ifs[n].obw_buf[0])) * (MAXAVGBUF - 1) );  
	  d->ifs[n].obw_buf[0] = d->ifs[n].obw;

	  d->ifs[n].obw_a =   0.5 * d->ifs[n].obw + 0.5 * d->ifs[n].obw_a;
	  d->ifs[n].obw_b =   0.1 * d->ifs[n].obw + 0.9 * d->ifs[n].obw_b;
	  d->ifs[n].obw_c =   0.02 * d->ifs[n].obw + 0.98 * d->ifs[n].obw_c;
	  }
  }

if (d->ncycle > 10)  // alarms only after startup window
  if (_send_alarm)
	eval_alarm(d, n);
  
if (_verbose > 1)
  {
  printf("\n\n --------------------------- ");
  printf("\n interface: %s  (%s)", d->ifs[n].name, d->ifs[n].description);
  printf("\n delta t:  %lf", delta_t);
  printf("\n ibw: %lf ibw_a: %lf", d->ifs[n].ibw, d->ifs[n].ibw_a);
  printf("\n obw: %lf obw_a: %lf", d->ifs[n].obw, d->ifs[n].obw_a);
  printf("\n ibytes: %lli obytes: %lli", d->ifs[n].ibytes, d->ifs[n].obytes);
  printf("\n ibytes prev: %lli obytes prev: %lli", d->ifs[n].ibytes_prev, d->ifs[n].obytes_prev);
  printf("\n --------------------------- \n\n"); 
  fflush(stdout);
  }  

return(0);
}

//------------------------------------------------------------------------

int snmp_process( device_data *d, int infd, int outfd, pid_t child_pid, int dev_id, int iface )
{
char 				buffer_aux[8000];

if (d->parse_bw)
    d->parse_bw( d, iface, buffer_aux );		

return(1);
}

//-------------------------------------------------------------
//-------------------------------------------------------------

// for a give IP and community, get Interface from IF TABLE (no mibs)
// and fill last digit for interface (function returns 0 on success)
// e.g.:  goes through 1.3.6.1.2.1.2.2.1.2 (snmpwalk) and if interface "eth1"  is found
// ind position  1.3.6.1.2.1.2.2.1.2.11, return '11' in oidIndex

int getIndexOfInterfaces(long snmpVersion, device_data *d, char *walkFirstOID, char *community, char *ipAddress )
{
    netsnmp_session         session, *ss;
    netsnmp_pdu             *pdu, *response;
    netsnmp_variable_list   *vars;
   // int             arg;
    oid             name[MAX_OID_LEN];
    size_t          name_length;
    oid             root[MAX_OID_LEN];
    size_t          rootlen;
    oid             end_oid[MAX_OID_LEN];
    size_t          end_len = 0;
    int             count;
    int             running;
    int             status = STAT_ERROR;
    int             exitval = -1;
    struct timeval  tv_a, tv_b;
    int             iface=0, ifaceFound=0;;

    // specified on the command line 
    rootlen = MAX_OID_LEN;

    if (snmp_parse_oid(walkFirstOID, root, &rootlen) == NULL) {
        snmp_perror(walkFirstOID);
        goto out;
    }

    memmove(end_oid, root, rootlen*sizeof(oid));
    end_len = rootlen;
    end_oid[end_len-1]++;

    //init_snmp("myprog");
    snmp_sess_init( &session );
    session.version = snmpVersion;

    session.community = (u_char *)strdup(community);
    session.community_len = strlen((char *)session.community);
    session.peername = strdup(ipAddress);

    //  open an SNMP session 
    ss = snmp_open(&session);
    if (ss == NULL) {
        snmp_sess_perror("snmpwalk", &session);
        goto out;
    }

    // get first object to start walk 
    memmove(name, root, rootlen * sizeof(oid));
    name_length = rootlen;

    running = 1;

    while (running) {

        if ( session.version == SNMP_VERSION_2c ) {
            // following lines are OK ONLY for V2c
            pdu = snmp_pdu_create(SNMP_MSG_GETBULK);
            pdu->non_repeaters = 0;
            pdu->max_repetitions = 10;    /* fill the packet */
        }
        else {
            // create regular PDU for GETNEXT request and add object name to request 
            pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
        }

        snmp_add_null_var(pdu, name, name_length);

        // do the request 
        if (netsnmp_ds_get_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_WALK_TIME_RESULTS_SINGLE))
            netsnmp_get_monotonic_clock(&tv_a);
        status = snmp_synch_response(ss, pdu, &response);
        if (status == STAT_SUCCESS) {
            if (netsnmp_ds_get_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_WALK_TIME_RESULTS_SINGLE))
                netsnmp_get_monotonic_clock(&tv_b);
            if (response->errstat == SNMP_ERR_NOERROR) {
                // check resulting variables 
                for (vars = response->variables; vars; vars = vars->next_variable) {
                    char mybuff[300];
 
                    if (snmp_oid_compare(end_oid, end_len, vars->name, vars->name_length) <= 0) {
                        //   not part of this subtree 
                        running = 0;
                        continue;
                    }
                    snprint_objid(mybuff, 299, vars->name, vars->name_length);

                    // get interface position in IF-TABLE. In case of error we cannot continue...
                    for (iface = 0 ; iface < d->nInterfaces ; iface++)   {
                        if (strcmp(d->ifs[iface].name, (char *)(vars->val.string)) == 0) {
                            if ( _verbose > 1)            
                                printf("\n --- FOUND:  |%s|%s| \n" , vars->val.string, mybuff);
                            strcpy(d->ifs[iface].oidIndex, strrchr(mybuff, '.') + 1);
                            ifaceFound++;
                            }
                        }    
                    if (ifaceFound == d->nInterfaces) {  // all interfaces found    
                        if ( _verbose > 1)            
                            printf("\n --- ALL Interfaces FOUND in IF - TABLE \n");

                        running = 0;
                        exitval = 0;
                        continue;
                    } 
                    
                    if ((vars->type != SNMP_ENDOFMIBVIEW) &&
                        (vars->type != SNMP_NOSUCHOBJECT) &&
                        (vars->type != SNMP_NOSUCHINSTANCE)) {
                        
                        //  not an exception value 
                        if (snmp_oid_compare(name, name_length,vars->name, vars->name_length) >= 0) {
                            fprintf(stderr, "Error: OID not increasing: ");
                            fprint_objid(stderr, name, name_length);
                            fprintf(stderr, " >= ");
                            fprint_objid(stderr, vars->name, vars->name_length);
                            fprintf(stderr, "\n");
                            running = 0;
                            exitval = -2;
                        }
                        memmove((char *) name, (char *) vars->name, vars->name_length * sizeof(oid));
                        name_length = vars->name_length;
                    } else
                        //* an exception value, so stop 
                        running = 0;
                }
            } else {
                //  error in response, print it 
                running = 0;
                if (response->errstat == SNMP_ERR_NOSUCHNAME) {
                    printf("End of MIB\n");
                } else {
                    fprintf(stderr, "Error in packet.\nReason: %s\n", snmp_errstring(response->errstat));
                    if (response->errindex != 0) {
                        fprintf(stderr, "Failed object: ");
                        for (count = 1, vars = response->variables;
                             vars && count != response->errindex;
                             vars = vars->next_variable, count++)
                            /*EMPTY*/;
                        if (vars)
                            fprint_objid(stderr, vars->name, vars->name_length);
                        fprintf(stderr, "\n");}
                    exitval = -3;
                }
            }
        } else if (status == STAT_TIMEOUT) {
            fprintf(stderr, "Timeout: No Response from %s\n",
                    session.peername);
            running = 0;
            exitval = -4;
        } else {                /* status == STAT_ERROR */
            snmp_sess_perror("snmpwalk", ss);
            running = 0;
            exitval = -5;
        }
        if (response)
            snmp_free_pdu(response);
    }

    if (ifaceFound < d->nInterfaces) {  // NOT all interfaces have been found    
        if ( _verbose > 1)            
            printf("\n --- %i  Interfaces NOT FOUND in IF - TABLE \n", d->nInterfaces - ifaceFound );
    }

    snmp_close(ss);
    if (session.community)
        free(session.community);
    if (session.peername)
        free(session.peername);
        
out:
    SOCK_CLEANUP;
    return exitval;
}

//-------------------------------------------------------------

// for a give IP and community, get Interfac from IF TABLE (no mibs)
// and returns last digit for interface (function returns 0 on success)
// e.g.:  goes through 1.3.6.1.2.1.2.2.1.2 (snmpwalk) and if interface "eth1"  is found
// ind position  1.3.6.1.2.1.2.2.1.2.11, return '11' in oidIndex

int getIndexOfInterface(long snmpVersion, char *interfaceName, char *walkFirstOID, char *community, char *ipAddress, char *oidIndex )
{
    netsnmp_session         session, *ss;
    netsnmp_pdu             *pdu, *response;
    netsnmp_variable_list   *vars;
   // int             arg;
    oid             name[MAX_OID_LEN];
    size_t          name_length;
    oid             root[MAX_OID_LEN];
    size_t          rootlen;
    oid             end_oid[MAX_OID_LEN];
    size_t          end_len = 0;
    int             count;
    int             running;
    int             status = STAT_ERROR;
    int             exitval = -1;
    struct timeval  tv_a, tv_b;

    // specified on the command line 
    rootlen = MAX_OID_LEN;

    if (snmp_parse_oid(walkFirstOID, root, &rootlen) == NULL) {
        snmp_perror(walkFirstOID);
        goto out;
    }

    memmove(end_oid, root, rootlen*sizeof(oid));
    end_len = rootlen;
    end_oid[end_len-1]++;

    //init_snmp("myprog");
    snmp_sess_init( &session );
    session.version = snmpVersion;

    session.community = (u_char *)strdup(community);
    session.community_len = strlen((char *)session.community);
    session.peername = strdup(ipAddress);

    //  open an SNMP session 
    ss = snmp_open(&session);
    if (ss == NULL) {
        snmp_sess_perror("snmpwalk", &session);
        goto out;
    }

    // get first object to start walk 
    memmove(name, root, rootlen * sizeof(oid));
    name_length = rootlen;

    running = 1;

    while (running) {
        // create PDU for GETNEXT request and add object name to request 
        pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
        snmp_add_null_var(pdu, name, name_length);

        // do the request 
        if (netsnmp_ds_get_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_WALK_TIME_RESULTS_SINGLE))
            netsnmp_get_monotonic_clock(&tv_a);
        status = snmp_synch_response(ss, pdu, &response);
        if (status == STAT_SUCCESS) {
            if (netsnmp_ds_get_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_WALK_TIME_RESULTS_SINGLE))
                netsnmp_get_monotonic_clock(&tv_b);
            if (response->errstat == SNMP_ERR_NOERROR) {
                // check resulting variables 
                for (vars = response->variables; vars; vars = vars->next_variable) {
                    char mybuff[300];
 
                    if (snmp_oid_compare(end_oid, end_len, vars->name, vars->name_length) <= 0) {
                        //   not part of this subtree 
                        running = 0;
                        continue;
                    }
                    snprint_objid(mybuff, 299, vars->name, vars->name_length);
                    
                    if (strcmp(interfaceName, (char *)(vars->val.string)) == 0) {
                        printf("\n --- FOUND:  |%s|%s| \n" , vars->val.string, mybuff);
                        
                        strcpy(oidIndex, strrchr(mybuff, '.') + 1);

                        running = 0;
                        exitval = 0;
                        continue;
                    } 
                    
                    if ((vars->type != SNMP_ENDOFMIBVIEW) &&
                        (vars->type != SNMP_NOSUCHOBJECT) &&
                        (vars->type != SNMP_NOSUCHINSTANCE)) {
                        
                        //  not an exception value 
                        if (snmp_oid_compare(name, name_length,vars->name, vars->name_length) >= 0) {
                            fprintf(stderr, "Error: OID not increasing: ");
                            fprint_objid(stderr, name, name_length);
                            fprintf(stderr, " >= ");
                            fprint_objid(stderr, vars->name, vars->name_length);
                            fprintf(stderr, "\n");
                            running = 0;
                            exitval = -2;
                        }
                        memmove((char *) name, (char *) vars->name, vars->name_length * sizeof(oid));
                        name_length = vars->name_length;
                    } else
                        //* an exception value, so stop 
                        running = 0;
                }
            } else {
                //  error in response, print it 
                running = 0;
                if (response->errstat == SNMP_ERR_NOSUCHNAME) {
                    printf("End of MIB\n");
                } else {
                    fprintf(stderr, "Error in packet.\nReason: %s\n", snmp_errstring(response->errstat));
                    if (response->errindex != 0) {
                        fprintf(stderr, "Failed object: ");
                        for (count = 1, vars = response->variables;
                             vars && count != response->errindex;
                             vars = vars->next_variable, count++)
                            /*EMPTY*/;
                        if (vars)
                            fprint_objid(stderr, vars->name, vars->name_length);
                        fprintf(stderr, "\n");}
                    exitval = -3;
                }
            }
        } else if (status == STAT_TIMEOUT) {
            fprintf(stderr, "Timeout: No Response from %s\n",
                    session.peername);
            running = 0;
            exitval = -4;
        } else {                /* status == STAT_ERROR */
            snmp_sess_perror("snmpwalk", ss);
            running = 0;
            exitval = -5;
        }
        if (response)
            snmp_free_pdu(response);
    }

    snmp_close(ss);
    if (session.community)
        free(session.community);
    if (session.peername)
        free(session.peername);
        
out:
    SOCK_CLEANUP;
    return exitval;
}

//-------------------------------------------------------------

// get counters (in octets, outOctets) for specific interface in device 'ipAddress'
// with 'community' parameter. recevies also  OID for IN / OUT counters
// returns 0 on success

int getInOutCounters (long snmpVersion, char *community, char *ipAddress, char *inCounterOid, char *outCounterOid, unsigned long long int *inCounter, unsigned long long int *outCounter)
{
    netsnmp_session session, *ss;
    netsnmp_pdu    *pdu;
    netsnmp_pdu    *response;
    netsnmp_variable_list *vars;
    int             count;
    int             current_name = 0;
    char           *names[SNMP_MAX_CMDLINE_OIDS];
    oid             name[MAX_OID_LEN];
    size_t          name_length;
    int             status;
    int             failures = 0;
    int             exitval = 1;
    int             counter=0;

    SOCK_STARTUP;
    names[current_name++] = inCounterOid;
    names[current_name++] = outCounterOid;
    
    //init_snmp("myprog");
    snmp_sess_init( &session );
    session.version = snmpVersion;

    session.community = (u_char *)strdup(community);
    session.community_len = strlen((char *)session.community);
    session.peername = strdup(ipAddress);
    
    ss = snmp_open(&session);
    if (ss == NULL) {  // diagnose snmp_open errors with the input netsnmp_session pointer 
        snmp_sess_perror("snmpget", &session);
        goto out;
    }

    // Create PDU for GET request and add object names to request.
    pdu = snmp_pdu_create(SNMP_MSG_GET);
    for (count = 0; count < current_name; count++) {
        name_length = MAX_OID_LEN;
        if (!snmp_parse_oid(names[count], name, &name_length)) {
            snmp_perror(names[count]);
            failures++;
        } else
            snmp_add_null_var(pdu, name, name_length);
    }
    if (failures)
        goto close_session;

    exitval = 0;

    // Perform the request.
    // If the Get Request fails, note the OID that caused the error,
    // "fix" the PDU (removing the error-prone OID) and retry.
  retry:
    status = snmp_synch_response(ss, pdu, &response);
    if (status == STAT_SUCCESS) {
        if (response->errstat == SNMP_ERR_NOERROR) {
            for (vars = response->variables, counter=0; vars; counter++, vars = vars->next_variable) {
                //print_variable(vars->name, vars->name_length, vars);

                if (vars->type == ASN_GAUGE) {
                    //printf("\n es un GAUGE:  %ln \n ", vars->val.integer);
                    if (counter == 0) // first variable
                        *inCounter = (unsigned long long int) vars->val.integer ;
                    else if (counter == 1) // second  variable
                        *outCounter = (unsigned long long int) vars->val.integer ;
                    }
                else if (vars->type == ASN_INTEGER)  {
                    //printf("\n es un INTEGER:  %ln \n ", vars->val.integer);
                    
                    if (counter == 0) // first variable
                        *inCounter = (unsigned long long int) vars->val.integer ;
                    else if (counter == 1) // second  variable
                        *outCounter = (unsigned long long int) vars->val.integer ;
                    }
                else if (vars->type == ASN_COUNTER) {
                    //printf("\n es un COUNTER:  |%u| \n ", (unsigned int)( *(vars->val.integer) & 0xffffffff));
                    
                    if (counter == 0) // first variable
                        *inCounter = (unsigned long long int)(*(vars->val.integer) & 0xffffffff) ;
                    else if (counter == 1) // second  variable
                        *outCounter = (unsigned long long int)(*(vars->val.integer) & 0xffffffff) ;
                    }
                else if (vars->type == ASN_COUNTER64) {
                    long i64 = 0;
                    i64 = vars->val.counter64->low;
                    i64 |= vars->val.counter64->high << 32;
                    //printf("\n es un COUNTER64:  |%li| \n ", vars->val.counter64->high);

                    if (counter == 0) // first variable
                        *inCounter = (unsigned long long int) i64 ;
                    else if (counter == 1) // second  variable
                        *outCounter = (unsigned long long int) i64; 
                  }
                }
        } else {
            fprintf(stderr, "Error in packet\nReason: %s\n",
                    snmp_errstring(response->errstat));

            if (response->errindex != 0) {
                fprintf(stderr, "Failed object: ");
                for (count = 1, vars = response->variables;
                     vars && count != response->errindex;
                     vars = vars->next_variable, count++)
                    /*EMPTY*/;
                if (vars) {
                    fprint_objid(stderr, vars->name, vars->name_length);
		}
                fprintf(stderr, "\n");
            }
            exitval = -2;

            // * retry if the errored variable was successfully removed 
            if (!netsnmp_ds_get_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_APP_DONT_FIX_PDUS)) {
                pdu = snmp_fix_pdu(response, SNMP_MSG_GET);
                snmp_free_pdu(response);
                response = NULL;
                if (pdu != NULL) {
                    goto retry;
		        }
            }
        }                       /* endif -- SNMP_ERR_NOERROR */

    } else if (status == STAT_TIMEOUT) {
        fprintf(stderr, "Timeout: No Response from %s.\n",
                session.peername);
        exitval = -3;

    } else {                    /* status == STAT_ERROR */
        snmp_sess_perror("snmpget", ss);
        exitval = -4;

    }                           /* endif -- STAT_SUCCESS */

    if (response)
        snmp_free_pdu(response);

close_session:
    snmp_close(ss);
    if (session.community)
        free(session.community);
    if (session.peername)
        free(session.peername);
out:
    SOCK_CLEANUP;
    return exitval;
}                               /* end main() */

//-------------------------------------------------------------

// simple SNMP get sysDescription (.1.3.6.1.2.1.1.1.0) to verify community, version, etc. 
// requires community, ip and 
// and returns 0 in case of success. (only in this case 'result' contains a valid string)
// result can be null if we don't need SYS Description string

int snmpGetSysDesrc (long snmpVersion, char *community, char *ipAddress, char *result )
{
    netsnmp_session session, *ss;
    netsnmp_pdu    *pdu;
    netsnmp_pdu    *response;
    netsnmp_variable_list *vars;
    int             count;
    int             current_name = 0;
    char           *names[SNMP_MAX_CMDLINE_OIDS];
    oid             name[MAX_OID_LEN];
    size_t          name_length;
    int             status;
    int             failures = 0;
    int             exitval = 1;
    int             counter=0;

    SOCK_STARTUP;
    names[current_name++] = ".1.3.6.1.2.1.1.1.0";
    
    if (result)
        result[0] = 0;    

    //init_snmp("myprog");
    snmp_sess_init( &session );
    session.version = snmpVersion;

    session.community = (u_char *)strdup(community);
    session.community_len = strlen((char *)session.community);
    session.peername = strdup(ipAddress);
    
    ss = snmp_open(&session);
    if (ss == NULL) {  // diagnose snmp_open errors with the input netsnmp_session pointer 
        snmp_sess_perror("snmpget", &session);
        goto out;
    }

    // Create PDU for GET request and add object names to request.
    pdu = snmp_pdu_create(SNMP_MSG_GET);
    for (count = 0; count < current_name; count++) {
        name_length = MAX_OID_LEN;
        if (!snmp_parse_oid(names[count], name, &name_length)) {
            snmp_perror(names[count]);
            failures++;
        } else
            snmp_add_null_var(pdu, name, name_length);
    }
    if (failures)
        goto close_session;

    exitval = 0;

    // Perform the request.
    // If the Get Request fails, note the OID that caused the error,
    // "fix" the PDU (removing the error-prone OID) and retry.
  retry:
    status = snmp_synch_response(ss, pdu, &response);
    if (status == STAT_SUCCESS) {
        if (response->errstat == SNMP_ERR_NOERROR) {
            for (vars = response->variables, counter=0; vars; counter++, vars = vars->next_variable) {
                print_variable(vars->name, vars->name_length, vars);

                if (vars->type == ASN_OCTET_STR) {
                    printf("\n es un STRING:  %s \n ", vars->val.string);
                    if (result)
                        strcpy(result, (char *)vars->val.string);
                    }
                }
        } else {
            fprintf(stderr, "Error in packet\nReason: %s\n",
                    snmp_errstring(response->errstat));

            if (response->errindex != 0) {
                fprintf(stderr, "Failed object: ");
                for (count = 1, vars = response->variables;
                     vars && count != response->errindex;
                     vars = vars->next_variable, count++)
                    /*EMPTY*/;
                if (vars) {
                    fprint_objid(stderr, vars->name, vars->name_length);
		}
                fprintf(stderr, "\n");
            }
            exitval = -2;

            // * retry if the errored variable was successfully removed 
            if (!netsnmp_ds_get_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_APP_DONT_FIX_PDUS)) {
                pdu = snmp_fix_pdu(response, SNMP_MSG_GET);
                snmp_free_pdu(response);
                response = NULL;
                if (pdu != NULL) {
                    goto retry;
		        }
            }
        }                       /* endif -- SNMP_ERR_NOERROR */

    } else if (status == STAT_TIMEOUT) {
        fprintf(stderr, "Timeout: No Response from %s.\n",
                session.peername);
        exitval = -3;

    } else {                    /* status == STAT_ERROR */
        snmp_sess_perror("snmpget", ss);
        exitval = -4;

    }                           /* endif -- STAT_SUCCESS */

    if (response)
        snmp_free_pdu(response);

close_session:
    snmp_close(ss);
    if (session.community)
        free(session.community);
    if (session.peername)
        free(session.peername);
out:
    SOCK_CLEANUP;
    return exitval;
}                               // end main() 

//-------------------------------------------------------------

// simple SNMP get (if possible)  ifXtable  (1.3.6.1.2.1.31.1.1.1.1 ) to verify
// if 64 bits cpounters are available
// 1.3.6.1.2.1.31.1.1.1.1 (ifName)
// 1.3.6.1.2.1.31.1.1.1.6 (ifHCInOctets)  <-- value tested, for interface '1'
// 1.3.6.1.2.1.31.1.1.1.10 (ifHCOutOctets)
// returns 0 in case of success. (only in this case 'counter' contains a valid string)

int snmpVerifyIfXTable (long snmpVersion, char *community, char *ipAddress, unsigned long long int *inCounter )
{
    netsnmp_session session, *ss;
    netsnmp_pdu    *pdu;
    netsnmp_pdu    *response;
    netsnmp_variable_list *vars;
    int             count;
    int             current_name = 0;
    char           *names[SNMP_MAX_CMDLINE_OIDS];
    oid             name[MAX_OID_LEN];
    size_t          name_length;
    int             status;
    int             failures = 0;
    int             exitval = 1;
    int             counter=0;

    SOCK_STARTUP;
    names[current_name++] = ".1.3.6.1.2.1.31.1.1.1.6.1";

    //init_snmp("myprog");
    snmp_sess_init( &session );
    session.version = snmpVersion;

    session.community = (u_char *)strdup(community);
    session.community_len = strlen((char *)session.community);
    session.peername = strdup(ipAddress);
    
    ss = snmp_open(&session);
    if (ss == NULL) {  // diagnose snmp_open errors with the input netsnmp_session pointer 
        snmp_sess_perror("snmpget", &session);
        goto out;
    }

    // Create PDU for GET request and add object names to request.
    pdu = snmp_pdu_create(SNMP_MSG_GET);
    for (count = 0; count < current_name; count++) {
        name_length = MAX_OID_LEN;
        if (!snmp_parse_oid(names[count], name, &name_length)) {
            snmp_perror(names[count]);
            failures++;
        } else
            snmp_add_null_var(pdu, name, name_length);
    }
    if (failures)
        goto close_session;

    exitval = 0;

    // Perform the request.
    // If the Get Request fails, note the OID that caused the error,
    // "fix" the PDU (removing the error-prone OID) and retry.
  retry:
    status = snmp_synch_response(ss, pdu, &response);
    if (status == STAT_SUCCESS) {
        if (response->errstat == SNMP_ERR_NOERROR) {
            for (vars = response->variables, counter=0; vars; counter++, vars = vars->next_variable) {
                print_variable(vars->name, vars->name_length, vars);

                if (vars->type == ASN_COUNTER64) {
                    long i64 = 0;
                    i64 = vars->val.counter64->low;
                    i64 |= vars->val.counter64->high << 32;
                    printf("\n es un COUNTER64:  |%li| \n ", i64);

                    if (inCounter)    
                        *inCounter = (unsigned long long int) i64 ;
                    }
                }
        } else {
            fprintf(stderr, "Error in packet\nReason: %s\n",
                    snmp_errstring(response->errstat));

            if (response->errindex != 0) {
                fprintf(stderr, "Failed object: ");
                for (count = 1, vars = response->variables;
                     vars && count != response->errindex;
                     vars = vars->next_variable, count++)
                    /*EMPTY*/;
                if (vars) {
                    fprint_objid(stderr, vars->name, vars->name_length);
		}
                fprintf(stderr, "\n");
            }
            exitval = -2;

            // * retry if the errored variable was successfully removed 
            if (!netsnmp_ds_get_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_APP_DONT_FIX_PDUS)) {
                pdu = snmp_fix_pdu(response, SNMP_MSG_GET);
                snmp_free_pdu(response);
                response = NULL;
                if (pdu != NULL) {
                    goto retry;
		        }
            }
        }                       /* endif -- SNMP_ERR_NOERROR */

    } else if (status == STAT_TIMEOUT) {
        fprintf(stderr, "Timeout: No Response from %s.\n",
                session.peername);
        exitval = -3;

    } else {                    /* status == STAT_ERROR */
        snmp_sess_perror("snmpget", ss);
        exitval = -4;

    }                           /* endif -- STAT_SUCCESS */

    if (response)
        snmp_free_pdu(response);

close_session:
    snmp_close(ss);
    if (session.community)
        free(session.community);
    if (session.peername)
        free(session.peername);
out:
    SOCK_CLEANUP;
    return exitval;
}                               /* end main() */

//-------------------------------------------------------------
//-------------------------------------------------------------

int mmain(int argc, char *argv[])
{
char                        oidIndex[200];
unsigned long long int      outCounter=0, inCounter=0;
char                        result[1000], snmpCommunity[200];
char                        inCounterOid[200],outCounterOid[200];
char                        interfaceName[300] = "eth0.52";
char                        ipAddress[300] = "172.16.114.152";
long                        snmpVersion=-1;
int                         use64bitsCounters=0;

if (argc < 3)  {
    printf("\n\n get Interface Traffic counters \n\n Use: %s ipAddress interfaceName \n\n", argv[0]);
    exit(-1);
}

strcpy(ipAddress, argv[1]);
strcpy(interfaceName, argv[2]);

printf("\n ------------------------------------------\n"); fflush(stdout);

if ( snmpGetSysDesrc (SNMP_VERSION_2c, "Vostok3KA", ipAddress, result) == 0 ) {
    strcpy(snmpCommunity, "Vostok3KA");
    snmpVersion = SNMP_VERSION_2c;
    }
else if ( snmpGetSysDesrc (SNMP_VERSION_2c, "public", ipAddress, result) == 0) {
    strcpy(snmpCommunity, "public");
    snmpVersion = SNMP_VERSION_2c;
    }
else if ( snmpGetSysDesrc (SNMP_VERSION_1, "Vostok3KA", ipAddress, result) == 0 ) {
    strcpy(snmpCommunity, "Vostok3KA");
    snmpVersion = SNMP_VERSION_1;
    }
else if ( snmpGetSysDesrc (SNMP_VERSION_1, "public", ipAddress, result) == 0) {
    strcpy(snmpCommunity, "public");
    snmpVersion = SNMP_VERSION_1;
    }
else {
    printf("\n\n Unable to contact device with SNMP version 1 / 2c and given community \n\n");
    exit(0);
}

if ( snmpVerifyIfXTable (snmpVersion, snmpCommunity, ipAddress, &inCounter ) == 0)
    use64bitsCounters = 1;

printf("\n\n Using Community: %s Version: %s 64 bits counters: %s", snmpCommunity, (snmpVersion == SNMP_VERSION_2c) ? "2c" : "1", (use64bitsCounters>0) ? "yes" : "no");

if ( getIndexOfInterface( snmpVersion, interfaceName, "1.3.6.1.2.1.2.2.1.2", snmpCommunity, ipAddress, oidIndex) != 0 ) {
    printf("\n\n UNABLE to get Interface Index for |%s|: !! \n\n", interfaceName);
    exit (-1);
    }

    printf("\n Interface Index: %s (%s) \n", oidIndex, interfaceName);
    if (use64bitsCounters) {
        sprintf(inCounterOid, "1.3.6.1.2.1.31.1.1.1.6.%s", oidIndex);
        sprintf(outCounterOid, "1.3.6.1.2.1.31.1.1.1.10.%s", oidIndex);
        }
    else {
    sprintf(inCounterOid, "1.3.6.1.2.1.2.2.1.10.%s", oidIndex);
    sprintf(outCounterOid, "1.3.6.1.2.1.2.2.1.16.%s", oidIndex);
    }

    while (1) {

        if ( getInOutCounters ( snmpVersion, snmpCommunity, ipAddress, inCounterOid, outCounterOid, &inCounter,  &outCounter) == 0) 
            printf("\n Counters (IN: %lli1 / OUT: %lli): ", inCounter,  outCounter);
        else 
            printf("\n ERROR getting IN / OUT counters !!! \n");

        fflush(stdout);    

        sleep(10);
    }

printf("\n ------------------------------------------\n"); fflush(stdout);

exit(0);
}

//-------------------------------------------------------------
//-------------------------------------------------------------