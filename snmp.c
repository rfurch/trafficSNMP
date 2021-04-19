 //
 // snmp routines for traffic grabbing 
 // First, the program gets Interface index from IF-TABLE searching by NAME (name MUST BE EXACT!)
 // that ID is then used to get IN / OUT octets counters
 //


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

#include <sys/select.h>
#include <stdio.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <net-snmp/net-snmp-config.h>
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

// we just try to figure out community name
// version, etc. Returns 0 on success

int 	snmpCheckParameters( deviceData *d, interfacesShm *shmInt) {
int     ret=0, retval = 0, i=0;

// aux variables to test snmp community / version alternatives
int     snmp_ver_a=0, snmp_ver_b=0;
char    snmp_comm_a[MAXBUF], snmp_comm_b[MAXBUF];

// values found in devices,  used to update database in order 
// to speedup starting times
char    snmp_comm_found[MAXBUF];
int     snmp_version_found=0;

// we try DB values first, if they are OK this will be
// just a quick validation
if (d->snmp == 1) {
    snmp_ver_a=SNMP_VERSION_1, snmp_ver_b=SNMP_VERSION_2c;
    }
else if (d->snmp == 2) {
    snmp_ver_a=SNMP_VERSION_2c, snmp_ver_b=SNMP_VERSION_1;
    }


if ( strcmp(d->snmp_comm, "p")  == 0) {
    strcpy(snmp_comm_a, "public");
    strcpy(snmp_comm_b, "Vostok3KA");
    }
else if ( strcmp(d->snmp_comm, "v")  == 0) {
    strcpy(snmp_comm_a, "Vostok3KA");
    strcpy(snmp_comm_b, "public");
    }


printf("\n ---------- %i %s %i %s   ------\n", snmp_ver_a, snmp_comm_a, snmp_ver_b, snmp_comm_b);


if ( snmpGetSysDesrc (snmp_ver_a, snmp_comm_a, d->ip, NULL) == 0 ) {
    d->snmpVersion = snmp_ver_a;
    strcpy(d->snmpCommunity, snmp_comm_a);
    }
else if ( snmpGetSysDesrc (snmp_ver_a, snmp_comm_b, d->ip, NULL) == 0) {
    d->snmpVersion = snmp_ver_a;
    strcpy(d->snmpCommunity, snmp_comm_b);
    }
else if ( snmpGetSysDesrc (snmp_ver_b, snmp_comm_a, d->ip, NULL) == 0 ) {
    d->snmpVersion = snmp_ver_b;
    strcpy(d->snmpCommunity, snmp_comm_a);
    }
else if ( snmpGetSysDesrc (snmp_ver_b, snmp_comm_b, d->ip, NULL) == 0) {
    d->snmpVersion = snmp_ver_b;
    strcpy(d->snmpCommunity, snmp_comm_b);
    }
else {
    printf("\n\n Unable to contact device (%s,%s) with SNMP version 1 / 2c and given community \n\n", d->name, d->ip);
  
    retval = -1;
    }

if (retval == 0) { // device available via SNMP

    // build 'found' values to compare with those on DB
    snmp_version_found = (d->snmpVersion == SNMP_VERSION_2c) ? 2 : 1;
    snmp_comm_found[0] = tolower( d->snmpCommunity[0]);
    snmp_comm_found[1] = 0;

    if (_verbose > 2)
        printf("\n SNMP Check OK -> version: %i,  community: %s", d->snmpVersion, d->snmpCommunity);

    // if we found different version / community,  update DB
    // we compare 'v' with 'Vostok3KA' using strncasecmp (len = 1)
    if ( (d->snmp != snmp_version_found) || (d->snmp_comm[0] != snmp_comm_found[0]) ) {

        if (_verbose > 2)
            printf("\n Updating DB:  version: %i -> %i  community: %c -> %c \n", 
            d->snmp, snmp_version_found, 
            d->snmp_comm[0], snmp_comm_found[0]);

        d->snmp = snmp_version_found;
        d->snmp_comm[0] = snmp_comm_found[0];
        d->snmp_comm[1] = 0;
        update_devices_snmp_ver_comm(d);
        }

    // set ALL interfaces of THIS device as 'readable via SNMP'
    for (i = 0 ; i < shmInt->nInterfaces ; i++)   
        if (shmInt->d[i].enable > 0 && d->deviceId == shmInt->d[i].deviceId) 
            shmInt->d[i].snmpDeviceOK = 1;

    if ( snmpVerifyIfXTable (d, NULL ) == 0)
        d->use64bitsCounters = 1;

    // get interface position in IF-TABLE (1.3.6.1.2.1.2.2.1.2). In case of error we cannot continue...
    // in case of error we also look into ifXtable  (1.3.6.1.2.1.31.1.1.1.1) 

    // count of interfaces found, reset
    d->interfacesFoundInSNMPTable = 0;

    // try by name / alias
    ret = getIndexOfInterfaces( d, shmInt, "1.3.6.1.2.1.2.2.1.2");
    
    if ( d->interfacesFoundInSNMPTable < d->nInterfaces )
        ret = getIndexOfInterfaces( d, shmInt, "1.3.6.1.2.1.31.1.1.1.1");

    if ( d->interfacesFoundInSNMPTable >= d->nInterfaces )
        retval = 0;
    }

return (retval);
}

//------------------------------------------------------------------------

// this routine parses snmp values captured from IF-TABLE
// d: device data structure
// n: index of interface to capture

int snmpCollectBWInfo( deviceData *d, interfaceData *iface)
{
unsigned long long int  lli1=0, lli2=0;
struct timeb 			stb;
double					delta_t=0;
char                    inCounterOid[400],outCounterOid[400];
int                     retVal = -1;

ftime(&stb); 
iface->last_access=stb;
iface->msec_prev = iface->msec;
iface->msec = stb.time*1000+stb.millitm;
delta_t =  ((double)(iface->msec - iface->msec_prev))/1000; // delta t in seconds 

iface->ibytes_prev_prev = iface->ibytes_prev;
iface->obytes_prev_prev = iface->obytes_prev;
iface->ibytes_prev = iface->ibytes;
iface->obytes_prev = iface->obytes;

// we set this values to 0 to take some action below
iface->ibytes = 0;
iface->obytes = 0;

// explicit OID overrides found ones!
if (strlen(iface->oidInOctets) > 5 && strlen(iface->oidOutOctets) > 5 )  {
    strcpy(inCounterOid, iface->oidInOctets);
    strcpy(outCounterOid, iface->oidOutOctets);
    }
else if (d->use64bitsCounters) {
    sprintf(inCounterOid, "1.3.6.1.2.1.31.1.1.1.6.%s", iface->oidIndex);
    sprintf(outCounterOid, "1.3.6.1.2.1.31.1.1.1.10.%s", iface->oidIndex);
    }
else {
    sprintf(inCounterOid, "1.3.6.1.2.1.2.2.1.10.%s", iface->oidIndex);
    sprintf(outCounterOid, "1.3.6.1.2.1.2.2.1.16.%s", iface->oidIndex);
    }

if ( getInOutCounters (d->snmpVersion, d->snmpCommunity, d->ip, inCounterOid, outCounterOid, &lli1,  &lli2) != 0) {
    printf("\n ERROR getting IN / OUT counters !!!  Device: %s (%s)\n", d->name, d->ip);
    retVal = -1;
    }
else {
    // success condition needs validation...
    retVal = 0;
    iface->ibytes = lli1;
    iface->obytes = lli2;

    if ( iface->ibytes_prev == 0 ) { // just starting, don't do anything
        }
    else if ( iface->ibytes_prev > iface->ibytes || iface->obytes_prev > iface->obytes ) { // prev > current, there was a clear counter, counters return to 0, etc, don't do any calculations
            // do nothing, we can figure out results though....
        }
    else  {
        double auxin=0;
        double auxout=0;

        auxin = (8*(double)(iface->ibytes - iface->ibytes_prev)) / delta_t;
        if (auxin < (((long long int)200) * 1000 * 1000 * 1000))  // < 200Gbps 
            iface->ibw =  auxin;
        
        auxout = (8*(double)(iface->obytes - iface->obytes_prev)) / delta_t;
        if (auxout < (((long long int)200) * 1000 * 1000 * 1000))  // < 200Gbps 
            iface->obw =  auxout;  

        // if ( iface->ibytes_prev_prev == 0 ) // second pass after starting the program, it's a good idea to use current 'instant' traffic as average!
        //	{
        //	int j=0;
        //
        //	for (j=0 ; j<MAXAVGBUF ; j++)	// copy FIRST sample to the WHOLE buffer
        //		iface->ibw_buf[j] = iface->ibw;
        //	iface->ibw_a	= iface->ibw_b = iface->ibw_c = iface->ibw;
        //
        //	for (j=0 ; j<MAXAVGBUF ; j++)	// copy FIRST sample to the WHOLE buffer
        //		iface->obw_buf[j] = iface->obw;
        //	iface->obw_a	= iface->obw_b = iface->obw_c = iface->obw;
        //	}
        //  else
            {
            // shift and copy AVG buffer.  Last value is always in the first position, previous in the second an so on...
            memmove( &(iface->ibw_buf[1]), &(iface->ibw_buf[0]), sizeof((iface->ibw_buf[0])) * (MAXAVGBUF - 1) );  
            iface->ibw_buf[0] = iface->ibw;
            
            iface->ibw_a =   0.5 * iface->ibw + 0.5 * iface->ibw_a;
            iface->ibw_b =   0.1 * iface->ibw + 0.9 * iface->ibw_b;
            iface->ibw_c =   0.02 * iface->ibw + 0.98 * iface->ibw_c;

            // shift and copy AVG buffer.  Last value is always in the first position, previous in the second an so on...
            memmove( &(iface->obw_buf[1]), &(iface->obw_buf[0]), sizeof((iface->obw_buf[0])) * (MAXAVGBUF - 1) );  
            iface->obw_buf[0] = iface->obw;

            iface->obw_a =   0.5 * iface->obw + 0.5 * iface->obw_a;
            iface->obw_b =   0.1 * iface->obw + 0.9 * iface->obw_b;
            iface->obw_c =   0.02 * iface->obw + 0.98 * iface->obw_c;
            }
        }

    if (d->ncycle > 10)  // alarms only after startup window
        if (_send_alarm)
            evalAlarm(d, iface);

    if (d->ncycle < 1000) 
        d->ncycle = d->ncycle + 1;

    //if (_verbose > 1 || iface->interfaceId==13)
    if (_verbose > 1 ) {
        printf("\n\n --------------------------- ");
        printf("\n interface: %s  (%s)", iface->name, iface->description);
        printf("\n delta t:  %lf", delta_t);
        printf("\n ibw: %lf ibw_a: %lf ibw_b: %lf ibw_c: %lf", iface->ibw, iface->ibw_a, iface->ibw_b, iface->ibw_c);
        printf("\n obw: %lf obw_a: %lf obw_b: %lf obw_c: %lf", iface->obw, iface->obw_a, iface->obw_b, iface->obw_c);
        printf("\n ibytes: %lli obytes: %lli", iface->ibytes, iface->obytes);
        printf("\n ibytes prev: %lli obytes prev: %lli", iface->ibytes_prev, iface->obytes_prev);
        printf("\n --------------------------- \n\n"); 
        fflush(stdout);
        }  
    }
return(retVal);
}

//-------------------------------------------------------------
//-------------------------------------------------------------

// for a given DEVICE (IP and community), get Interface from IF TABLE (no mibs)
// and fill last digit for interface (function returns 0 on success)
// e.g.:  goes through 1.3.6.1.2.1.2.2.1.2 (snmpwalk) and if interface "eth1"  is found
// in position  1.3.6.1.2.1.2.2.1.2.11, return '11' in oidIndex

int getIndexOfInterfaces( deviceData *d, interfacesShm *shmInt, char *walkFirstOID )
{
    oid                     name[MAX_OID_LEN];
    size_t                  name_length;
    oid                     root[MAX_OID_LEN];
    size_t                  rootlen;
    oid                     end_oid[MAX_OID_LEN];
    size_t                  end_len = 0;
    int                     count;
    int                     running;
    int                     status = STAT_ERROR;
    int                     exitval = -1;
    struct timeval          tv_a, tv_b;
    int                     iface=0;;
    netsnmp_session         session, *ss;
    netsnmp_pdu             *pdu;
    netsnmp_pdu             *response;
    netsnmp_variable_list   *vars;

    // specified on the command line 
    rootlen = MAX_OID_LEN;

    if (_verbose > 4)
        printf("\n getIndexOfInterfaces: name: %s  OID: %s", d->name, walkFirstOID);

    if (snmp_parse_oid(walkFirstOID, root, &rootlen) == NULL) {
        snmp_perror(walkFirstOID);
        goto out;
    }

    memmove(end_oid, root, rootlen*sizeof(oid));
    end_len = rootlen;
    end_oid[end_len-1]++;

    //init_snmp("myprog");
    snmp_sess_init( & (session) );
    session.version = d->snmpVersion;

    session.community = (unsigned char *)strdup(d->snmpCommunity);
    session.community_len = strlen((char *)session.community);
    session.peername = strdup(d->ip);

    //  open an SNMP session 
    ss = snmp_open(&(session));
    if (ss == NULL) {
        snmp_sess_perror("snmpwalk", &(session));
        goto out;
    }

    // get first object to start walk 
    memmove(name, root, rootlen * sizeof(oid));
    name_length = rootlen;

    running = 1;

    // Rfurch 16/4/21,  this must be done outside    
    //d->interfacesFoundInSNMPTable = 0;
    
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
        status = snmp_synch_response(ss, pdu, & (response));
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
                        // end of this part of the tree
                        // if we found at least ONE interface we consider the process SUCCESSFUL
                        if (d->interfacesFoundInSNMPTable > 0)
                            exitval = -9; 
                            // RFurch 29/3/21
                            //exitval = 0; 

                        continue;
                    }
                    snprint_objid(mybuff, 299, vars->name, vars->name_length);

                    // get interface position in IF-TABLE. In case of error we cannot continue...
                    for (iface = 0 ; iface < shmInt->nInterfaces ; iface++)   {
                        if (shmInt->d[iface].enable > 0 && d->deviceId == shmInt->d[iface].deviceId) {
                            if (strlen(shmInt->d[iface].oidIndex)<1) {
				
				// RFurch,  19/4/21:  due to errors in name length,  its important to
    				// compare  with proper length:  MAX (database, SNMP)
    
				int nameMaxLen = (strlen(shmInt->d[iface].name) > vars->val_len) ? strlen(shmInt->d[iface].name) : vars->val_len;

                                if ( _verbose > 7)            
                                    printf("\n --- Searching string: |%s| comparing with: |%s|%i| \n" , shmInt->d[iface].name, vars->val.string , nameMaxLen );

                                if (strncasecmp(shmInt->d[iface].name, (char *)(vars->val.string), nameMaxLen) == 0) {
                                    if ( _verbose > 1)            
                                        printf("\n --- FOUND:  |%s|%s| \n" , vars->val.string, mybuff);
                                    strcpy(shmInt->d[iface].oidIndex, strrchr(mybuff, '.') + 1);
                                    d->interfacesFoundInSNMPTable++;
                                    shmInt->d[iface].snmpOIDOk = 1;

                                    }
                                else {   // we try removing whitespaces... special case for RAISECOM!!!
                                    char    auxStr[300];
                                    remove_spaces (auxStr, shmInt->d[iface].name);
                                    if (strcasecmp(auxStr, (char *)(vars->val.string)) == 0) {
                                        if ( _verbose > 1)            
                                            printf("\n --- FOUND:  |%s|%s|%s|%s| \n" , shmInt->d[iface].name, auxStr, vars->val.string, mybuff);
                                        strcpy(shmInt->d[iface].oidIndex, strrchr(mybuff, '.') + 1);
                                        d->interfacesFoundInSNMPTable++;
                                        shmInt->d[iface].snmpOIDOk = 1;

                                        }

                                    // now we verify if OID is explicitly defined in DB

                                    if ( strlen(shmInt->d[iface].oidInOctets) > 5 && strlen(shmInt->d[iface].oidInOctets) > 5 ) {
                                        d->interfacesFoundInSNMPTable++;
                                        shmInt->d[iface].snmpOIDOk = 1;
                                        }
                                    }    
                                }   
                            }    
                        }    

//                    if ( _verbose > 2)            
//                        printf("\n --- Interfaces FOUND: %i  Total: %i" , d->interfacesFoundInSNMPTable, d->nInterfaces);

                    if (d->interfacesFoundInSNMPTable >= d->nInterfaces) {  // all interfaces found    
//                        if ( _verbose > 1)            
//                            printf("\n --- ALL Interfaces FOUND in IF - TABLE (%i)  \n", d->interfacesFoundInSNMPTable);

//                        if ( _verbose > 3)            
//                            printf("\n Interfaces FOUND in IF - TABLE (%i)  \n", d->interfacesFoundInSNMPTable);


                        running = 0;
                        exitval = 0;
                        continue;
                    } 
                    
                    if ((vars->type != SNMP_ENDOFMIBVIEW) &&
                        (vars->type != SNMP_NOSUCHOBJECT) &&
                        (vars->type != SNMP_NOSUCHINSTANCE)) {
                        
                        //  not an exception value 
                        if (snmp_oid_compare(name, name_length, vars->name, vars->name_length) >= 0) {
                            if (_verbose > 3) {
                                fprintf(stderr, "Error: OID not increasing: ");
                                fprint_objid(stderr, name, name_length);
                                fprintf(stderr, " >= ");
                                fprint_objid(stderr, vars->name, vars->name_length);
                                fprintf(stderr, "\n");
                            }
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
                    if (_verbose > 1)
                        printf("End of MIB\n");
                } else {
                    if (_verbose > 3) 
                        fprintf(stderr, "Error in packet.\nReason: %s\n", snmp_errstring(response->errstat));
                    if (response->errindex != 0) {
                        if (_verbose > 3) { 
                            fprintf(stderr, "Failed object: ");
                            for (count = 1, vars = response->variables; vars && count != response->errindex; vars = vars->next_variable, count++)
                                /*EMPTY*/;
                            if (vars)
                                fprint_objid(stderr, vars->name, vars->name_length);
                            fprintf(stderr, "\n");
                            }
                        }       
                    exitval = -3;
                }
            }
        } else if (status == STAT_TIMEOUT) {
            if (_verbose > 3) 
                fprintf(stderr, "Timeout: No Response from %s\n", session.peername);
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

    if ( _verbose > 1) {
        if (d->interfacesFoundInSNMPTable < d->nInterfaces)   // NOT all interfaces have been found    
            printf("\n --- %i Interfaces NOT FOUND in IF - TABLE (dev: %i - %s)  \n", d->nInterfaces - d->interfacesFoundInSNMPTable, d->deviceId, d->name);
        else
            printf("\n --- %i ALL I nterfaces  FOUND in IF - TABLE (dev: %i - %s)  \n", d->nInterfaces - d->interfacesFoundInSNMPTable, d->deviceId, d->name);
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

int getIndexOfInterfaceRR(long snmpVersion, char *interfaceName, char *walkFirstOID, char *community, char *ipAddress, char *oidIndex )
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

    session.community = (unsigned char *)strdup(community);
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
                        if (_verbose > 0)
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
                            if (_verbose > 3) {
                                fprintf(stderr, "Error: OID not increasing: ");
                                fprint_objid(stderr, name, name_length);
                                fprintf(stderr, " >= ");
                                fprint_objid(stderr, vars->name, vars->name_length);
                                fprintf(stderr, "\n");
                                }
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
                    if (_verbose > 0)
                        printf("End of MIB\n");
                } else {
                    if (_verbose > 3) 
                        fprintf(stderr, "Error in packet.\nReason: %s\n", snmp_errstring(response->errstat));
                    if (response->errindex != 0) {
                        if (_verbose > 3) {
                            fprintf(stderr, "Failed object: ");
                            for (count = 1, vars = response->variables;
                                 vars && count != response->errindex;
                                 vars = vars->next_variable, count++)
                                /*EMPTY*/;
                            if (vars)
                                fprint_objid(stderr, vars->name, vars->name_length);
                            fprintf(stderr, "\n");
                            }
                        }    
                    exitval = -3;
                }
            }
        } else if (status == STAT_TIMEOUT) {
            if (_verbose > 3) 
                fprintf(stderr, "Timeout: No Response from %s\n", session.peername);
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

    session.community = (unsigned char *)strdup(community);
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

    //    exitval = 0;

    // Perform the request.
    // If the Get Request fails, note the OID that caused the error,
    // "fix" the PDU (removing the error-prone OID) and retry.
  retry:
    status = snmp_synch_response(ss, pdu, &response);
    if (status == STAT_SUCCESS) {
        if (response->errstat == SNMP_ERR_NOERROR) {

            // location of success should be verified    
            exitval = 0;

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

            if (_verbose > 3) 
                fprintf(stderr, "Error in packet\nReason: %s\n", snmp_errstring(response->errstat));

            if (response->errindex != 0) {
                if (_verbose > 3) {
                    fprintf(stderr, "Failed object: ");
                    for (count = 1, vars = response->variables;
                         vars && count != response->errindex;
                         vars = vars->next_variable, count++)
                        /*EMPTY*/;
                    if (vars) 
                        fprint_objid(stderr, vars->name, vars->name_length);
		            
                fprintf(stderr, "\n");
                }
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
        if (_verbose > 3) 
            fprintf(stderr, "Timeout: No Response from %s.\n", session.peername);
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

int snmpGetSysDesrc (long snmpVersion, char *community, char *ip, char *result )
{

    int             count;
    int             current_name = 0;
    char           *names[SNMP_MAX_CMDLINE_OIDS];
    oid             name[MAX_OID_LEN];
    size_t          name_length;
    int             status;
    int             failures = 0;
    int             exitval = 1;
    int             counter=0;
    netsnmp_session         session, *ss;
    netsnmp_pdu             *pdu;
    netsnmp_pdu             *response;
    netsnmp_variable_list   *vars;
    
    SOCK_STARTUP;
    names[current_name++] = ".1.3.6.1.2.1.1.1.0";
    
    if (result)
        result[0] = 0;    

    //init_snmp("myprog");
    snmp_sess_init( & (session) );
    session.version = snmpVersion;

    session.community = (unsigned char *)strdup(community);
    session.community_len = strlen((char *)session.community);
    session.peername = strdup(ip);
    
    ss = snmp_open(& (session));
    if (ss == NULL) {  // diagnose snmp_open errors with the input netsnmp_session pointer 
        snmp_sess_perror("snmpget", &(session));
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
    status = snmp_synch_response(ss, pdu, &(response));
    if (status == STAT_SUCCESS) {
        if (response->errstat == SNMP_ERR_NOERROR) {
            for (vars = response->variables, counter=0; vars; counter++, vars = vars->next_variable) {
                if (_verbose > 2)
                    print_variable(vars->name, vars->name_length, vars);

                if (vars->type == ASN_OCTET_STR) {
                    if (_verbose > 1)
                        printf("\n es un STRING:  %s \n ", vars->val.string);
                    if (result)
                        strcpy(result, (char *)vars->val.string);
                    }
                }
        } else {
            if (_verbose > 3) 
                fprintf(stderr, "Error in packet\nReason: %s\n", snmp_errstring(response->errstat));

            if (response->errindex != 0) {
                if (_verbose > 3) {
                    fprintf(stderr, "Failed object: ");
                    for (count = 1, vars = response->variables;
                         vars && count != response->errindex;
                         vars = vars->next_variable, count++)
                        /*EMPTY*/;
                    if (vars) 
                        fprint_objid(stderr, vars->name, vars->name_length);
		                
                    fprintf(stderr, "\n");
                    }
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
        if (_verbose > 3) 
            fprintf(stderr, "Timeout: No Response from %s.\n", session.peername);
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

int snmpVerifyIfXTable (deviceData *d, unsigned long long int *inCounter )
{

    int             count = 0;
    int             current_name = 0;
    char           *names[SNMP_MAX_CMDLINE_OIDS];
    oid             name[MAX_OID_LEN];
    size_t          name_length;
    oid                     base_oid[MAX_OID_LEN];
    size_t                  base_len = 0;
    int             status;
    int             failures = 0;
    int             exitval = 1;
    int             counter=0;
    netsnmp_session         session, *ss;
    netsnmp_pdu             *pdu;
    netsnmp_pdu             *response;
    netsnmp_variable_list   *vars;

    SOCK_STARTUP;
    names[current_name++] = ".1.3.6.1.2.1.31.1.1.1.6";

    base_len = MAX_OID_LEN;
    if (snmp_parse_oid("1.3.6.1.2.1.31.1.1.1.6", base_oid, &base_len) == NULL) {
        snmp_perror("1.3.6.1.2.1.31.1.1.1.6");
        goto out;
    }

    //init_snmp("myprog");
    snmp_sess_init( & (session) );
    session.version = d->snmpVersion;

    session.community = (unsigned char *)strdup( d->snmpCommunity );
    session.community_len = strlen((char *)session.community);
    session.peername = strdup(d->ip);
    
    ss = snmp_open(& (session));
    if (ss == NULL) {  // diagnose snmp_open errors with the input netsnmp_session pointer 
        snmp_sess_perror("snmpget", & (session));
        goto out;
    }

    // Create PDU for GET request and add object names to request.
    pdu = snmp_pdu_create(SNMP_MSG_GETNEXT);
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
    status = snmp_synch_response(ss, pdu, &(response));
    if (status == STAT_SUCCESS) {
        if (response->errstat == SNMP_ERR_NOERROR) {
            for (vars = response->variables, counter=0; vars; counter++, vars = vars->next_variable) {

                if (_verbose > 2) {
                    print_variable(vars->name, vars->name_length, vars);
					fflush(stdout);	
					}

                if (netsnmp_oid_is_subtree(base_oid, base_len, vars->name, vars->name_length) != 0) {
                    //   not part of this subtree 
                    exitval = -1;
                    break;
                    }

                if (vars->type == ASN_COUNTER64) {
                    long i64 = 0;
                    i64 = vars->val.counter64->low;
                    i64 |= vars->val.counter64->high << 32;
                    if (_verbose > 1)
                        printf("\n es un COUNTER64:  |%li| \n ", i64);

                    if (inCounter)    
                        *inCounter = (unsigned long long int) i64 ;
                
                    exitval = 0;
                    break;
				    }
                }
        } else {
            if (_verbose > 3) 
                fprintf(stderr, "Error in packet\nReason: %s\n", snmp_errstring(response->errstat));

            if (response->errindex != 0) {
                if (_verbose > 3) {
                    fprintf(stderr, "Failed object: ");
                    for (count = 1, vars = response->variables;
                         vars && count != response->errindex;
                        vars = vars->next_variable, count++)
                        /*EMPTY*/;
                    if (vars) 
                        fprint_objid(stderr, vars->name, vars->name_length);
	
                    fprintf(stderr, "\n");
                    }
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
        if (_verbose > 3)
            fprintf(stderr, "Timeout: No Response from %s.\n", session.peername);
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
