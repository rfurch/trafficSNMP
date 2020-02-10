#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <resolv.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netinet/ip_icmp.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/timeb.h>
#include <strings.h>
#include <sys/socket.h> 
#include <pthread.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <netinet/in_systm.h>

//#pragma pack (1)   // WARNING:  this MUST be set  in ALL modules (if required)

#include "ifaceData.h"

#define         DEFDATALEN      56
#define         PACKETSIZE      80
#define         MAXLLEN         2000

#define         M1              111
#define         M2              23
#define         M3              7

// basic ICMP packet with data (payload)

struct packet   {
    struct icmphdr hdr;
    char msg[PACKETSIZE - sizeof(struct icmphdr)];
};

//-------------------------------------------------------------------

// CRC calculation
// Algorithm is simple, using a 32 bit accumulator (sum), we add
// sequential 16 bit words to it, and at the end, fold back all the
// carry bits from the top 16 bits into the lower 16 bits.

unsigned short in_cksum(unsigned short *addr, int len)
{
int                             nleft = len;
int                             sum = 0;
unsigned short  *w = addr;
unsigned short  answer = 0;

while (nleft > 1)   {
    sum += *w++;
    nleft -= 2;
    }

if (nleft == 1)     {    // 4mop up an odd byte, if necessary 
    *(unsigned char *)(&answer) = *(unsigned char *)w ;
    sum += answer;
    }

// 4add back carry outs from top 16 bits to low 16 bits 
sum = (sum >> 16) + (sum & 0xffff);     // add hi 16 to low 16 /
sum += (sum >> 16);                     // add carry 
answer = ~sum;                          // truncate to 16 bits 
return(answer);
}

//--------------------------------------------------------------------

// print hexdata of any chunk

int printBuffer(char *buffer, int len)  {
    int i=0;

    printf("\n %02i: ", 0);
    for (i=0 ; i<len ; i++) {
        printf(" %03i", (unsigned char) buffer[i]);
        if ( (i+1)%16 == 0)
        printf("\n %02i: ",i);
    }

    printf("\n\n"); 
    fflush(stdout);
    return(0);
} 

//--------------------------------------------------------------------

// Build ICMP packet
int   buildICMP(char * buffer, int *totalLen, int id, int sequence, int magic1, int magic2, int magic3, int device) {

struct icmp     *icmp;
int             len = 0; 
char            *p=NULL;   
char            myString[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0"; 

if(!buffer) 
    return(-1);

icmp = (struct icmp *) buffer;
icmp->icmp_type = ICMP_ECHO;
icmp->icmp_code = 0;
icmp->icmp_id = id;
icmp->icmp_seq = htons(sequence);

// now fill data area of ICMP packet
p = (char *) &(icmp->icmp_data);
gettimeofday((struct timeval *) p, NULL);
len+=sizeof(struct timeval);
memmove(p + len, &magic1, sizeof(magic1));
len+=sizeof(magic1);
memmove(p + len, &magic2, sizeof(magic2));
len+=sizeof(magic2);
memmove(p + len, &magic3, sizeof(magic3));
len+=sizeof(magic3);
memmove(p + len, &device, sizeof(device));
len+=sizeof(device);
memmove(p + len, myString, sizeof(myString));
len+=sizeof(myString);

icmp->icmp_cksum = 0;
icmp->icmp_cksum = in_cksum((u_short *) icmp, sizeof(icmp) + len);
if (totalLen)
    *totalLen = sizeof(icmp) + len;
return(0);
}

//--------------------------------------------------------------------

// build basic socket

int buildSocket(int *s)  {

struct protoent     *proto;
int                 sockopt = 0;
int                 val=0;

if (*s)  close(*s);

proto = getprotobyname("icmp"); 
//  if getprotobyname failed, just silently force proto->p_proto to have the correct value for "icmp"
if ((*s = socket(AF_INET, SOCK_RAW,  (proto ? proto->p_proto : 1))) < 0)      {
    fprintf( stderr,  "ping: permission denied. (are you root?)\n");
    return(-1);
    }

//  set recv buf for broadcast pings 
sockopt = 48 * 1024;
setsockopt(*s, SOL_SOCKET, SO_RCVBUF, (char *) &sockopt, sizeof(sockopt));
   
val = 255;
if (setsockopt(*s, SOL_IP, IP_TTL, &val, sizeof(val)) != 0)    {
    perror("Set TTL option");
    if (*s)  
        close(*s);
    return -2;
    }
        
//if (fcntl(*s, F_SETFL, O_NONBLOCK) != 0) {
//    perror("Request nonblocking I/O");
//    return 1;
//    }
    
return(0);
}

//--------------------------------------------------------------------

// send ICMP packet
int sendICMP(int socketICMP, char *address, int device)  {

char                    buffer[2000];
int                     len=0;
struct sockaddr_in      destinationAddress, *addr;
struct hostent          *hname;
static int              sequence=0;

sequence = (sequence < 1000) ? sequence + 1 : 1;

hname = gethostbyname(address);
bzero(&destinationAddress, sizeof(destinationAddress));
destinationAddress.sin_family = hname->h_addrtype;
destinationAddress.sin_port = 0;
destinationAddress.sin_addr.s_addr = *(long *)(hname->h_addr);

addr = &destinationAddress;  

if ( buildICMP(buffer, &len, getpid(), sequence, M1, M2, M3, device) != 0)
    return(-1);
 
//printBuffer(buffer, len);
if (sendto(socketICMP, buffer, len, 0, (struct sockaddr *)addr, sizeof(*addr)) <= 0) {
    perror("sendto");
    return(-2);
    }

return(0);
}

//--------------------------------------------------------------------

// calculate delta time (NOW - given timeval, ussualy obtained from ICMP)

unsigned long int getTripTime(struct timeval *tp) {
struct timeval          tv;
unsigned long           triptime=0;

gettimeofday(&tv, NULL);

//printf("\n\n tv: %li %li    tp: %li %li", tv.tv_sec, tv.tv_usec, tp->tv_sec, tp->tv_usec);

if ((tv.tv_usec -= tp->tv_usec) < 0) {
    --tv.tv_sec;
    tv.tv_usec += 1000000;
    }
tv.tv_sec -= tp->tv_sec;
triptime = tv.tv_sec * 1000 + (tv.tv_usec / 1000);

return (triptime);
}

//--------------------------------------------------------------------

// receive Replay via ICMP (if any)
int receiveICMP(int socketICMP, char *receiveBuffer, int *receiveLen)  {
struct sockaddr_in      receiveAddr;
int                     recvAddrLen = sizeof(receiveAddr);
int                     retLen=0;

if (!receiveBuffer || !receiveLen)
    return(-1);

if ( (retLen = recvfrom(socketICMP, receiveBuffer, MAXLLEN, 0, 
    (struct sockaddr *)&receiveAddr, (socklen_t *)&recvAddrLen)) > 0) {

    *receiveLen = retLen;
    return(0);
    }
else if (retLen < (DEFDATALEN + ICMP_MINLEN)) { // discard if too short
    printf("\n RECV too short (%i) minimum len: %i", retLen,  (DEFDATALEN + ICMP_MINLEN)) ;
    return(-3);
    }

return(-1);
}    

//--------------------------------------------------------------------

// verify magic numbersinside ICMP packet to verify 
// ICMP reply has been sent by this process

int  validateICMP(char *address, char *receiveBuffer, int len, int device, long int *tripTime) {

//struct sockaddr_in      receiveAddr;
//int                     recvAddrLen = sizeof(receiveAddr);
struct icmp             *icmpPacket;
struct iphdr            *iphdr;
int                     ipv4HeaderLen=0;
int                     recvDev=0;
char                    *p = NULL;

if (!address || !receiveBuffer)
    return(-1);

iphdr = (struct iphdr *)((char *) receiveBuffer);
ipv4HeaderLen = iphdr->ihl << 2;
icmpPacket = (struct icmp *) (((char *)receiveBuffer) + ipv4HeaderLen);
p = (char *) &(icmpPacket->icmp_data);

// verify received data
if (iphdr->saddr == inet_addr(address)) {       // src IP match
    if ( icmpPacket->icmp_id == getpid() )  {    // ID (pid) matches...
        int m1=0, m2=0, m3=0, len=0;

        len+=sizeof(struct timeval);
        memmove(&m1, p + len, sizeof(m1));
        len += sizeof(m1);
        memmove(&m2, p + len, sizeof(m2));
        len += sizeof(m2);
        memmove(&m3, p + len, sizeof(m3));
        len += sizeof(m3);
        memmove(&recvDev, p + len, sizeof(recvDev));
        len += sizeof(device);

        if (m1 == M1 && m2 == M2 && m3 == M3 && recvDev == device)  {

            if (tripTime)
                *tripTime = getTripTime( (struct timeval *) &(icmpPacket->icmp_data) );

            /*printf("\n\n header len: %i \n\n", ipv4HeaderLen);
            printBuffer(&receiveBuffer[20], len);

            printf("\n id: %i %i", icmpPacket->icmp_id, getpid());
            printf("\n triptime: %li", getTripTime( (struct timeval *) &(icmpPacket->icmp_data) ));
            printf("\n address: %i %i %i %s", iphdr->daddr,  iphdr->saddr, inet_addr(address), address);
            */
            return(0);
            }
        }
    }

return(-1);
}

//--------------------------------------------------------------------

// ping destination with timeout (msec)
// timeout is expressed in milliseconds. 
// roudtriptime can be null, or points to long int in which
// RTT in milliseconds will be stored in case of success 

int ping(char *address, int timeOut, long int *roundTripTime) {
//int                 counter = 0;
int                 localSocket = 0;
fd_set              rfds;
struct timeval      tv;
int                 auxErr=0, retVal = -9, receiveLen=0;
int                 device = 9;
char                receiveBuffer[MAXLLEN];
int                 sockErr=0;

// build socket. for sanity close and re - create RAW socket. It Works for first time too...
//if ( --counter  <= 0 ) {
//    counter=1000;
//    if ( (sockErr = buildSocket( & localSocket )) != 0 )  {
//        fprintf(stderr, "\n\nERROR creating SOCKET (%i): are you root? \n\n", sockErr);
//        fflush(stderr);
//        return(-3); 
//        }
//    }   

if ( (sockErr = buildSocket( & localSocket )) != 0 )  {
    fprintf(stderr, "\n\nERROR creating SOCKET (%i): are you root? \n\n", sockErr);
    fflush(stderr);
    retVal = -1; 
    }
else {    
    // whatch or socket, read and write conditions
    FD_ZERO(&rfds);   FD_SET(localSocket, &rfds);

    // Wait for timeout
    tv.tv_sec = timeOut / 1000;  tv.tv_usec = timeOut % 1000;

    if ( sendICMP(localSocket, address, device) == 0 ) {
        auxErr = select(1 + localSocket, &rfds, NULL, NULL, &tv);
        if (auxErr == -1) {
            perror("select()");
            retVal = -2;
            }
        else if (auxErr)   {
            if (FD_ISSET(localSocket, &rfds))   {
                if (receiveICMP(localSocket, receiveBuffer, &receiveLen) == 0) {
                    if (validateICMP(address, receiveBuffer, receiveLen, device, roundTripTime) == 0)
                        retVal = 0;         // this is the ONLY correct path
                    else 
                        retVal = -3;    // validate error
                    }
                }
            }
        else        
            retVal = -2;  //  timeout error 
        }    
    }
if (localSocket)  
    close(localSocket);

return(retVal);
}

//--------------------------------------------------------------------

// Send ICMP request to every DEVICE, 
// this routine does not wait for REPLY
// receive specific field (DEVICE, int32)
// to verify later received packers BY IP and DEVICE 

int sendMultiPing(devicesShm *devList) {
int                 localSocket = 0;
int                 retVal = 0;
int                 sockErr=0, i=0;
int                 interDelay = 0, numDev = 0;

numDev = (devList->nDevices > 100) ? devList->nDevices : 100;
interDelay = 3000000 / numDev;

if ( (sockErr = buildSocket( & localSocket )) != 0 )  {
    fprintf(stderr, "\n\nERROR creating SOCKET (%i): are you root? \n\n", sockErr);    fflush(stderr);
    retVal = -1; 
    }

//pthread_mutex_lock (& (devicesList->lock));
for ( i=0 ; i<devList->nDevices ; i++ )  {
    sendICMP(localSocket, devList->d[i].ip , devList->d[i].deviceId);
    usleep(interDelay);
    }
//pthread_mutex_unlock (& (devicesList->lock));

if (localSocket)  
    close(localSocket);

return(retVal);
}

//--------------------------------------------------------------------

// parses ICMP reception buffer, detects valid ICMP Replys 
// and updates DEVICES an INTERFACES "last Ping OK" info

int 	validateAndUpdateDevices(char *receiveBuffer, int len, devicesShm *devicesList, interfacesShm *interfacesList) {
int                     retVal = 0 ;
struct icmp             *icmpPacket;
struct iphdr            *iphdr;
int                     ipv4HeaderLen=0;
char                    *p = NULL;
int                     m1=0, m2=0, m3=0, device=0, i=0, j=0;

if (!receiveBuffer)
    return(-1);

iphdr = (struct iphdr *)((char *) receiveBuffer);
ipv4HeaderLen = iphdr->ihl << 2;
icmpPacket = (struct icmp *) (((char *)receiveBuffer) + ipv4HeaderLen);
p = (char *) &(icmpPacket->icmp_data);

memmove(&m1, p + sizeof(struct timeval), sizeof(m1));
memmove(&m2, p + sizeof(struct timeval) + sizeof(m1), sizeof(m2));
memmove(&m3, p + sizeof(struct timeval) + sizeof(m1) + sizeof(m2), sizeof(m3));
memmove(&device, p + sizeof(struct timeval) + sizeof(m1) + sizeof(m2) + sizeof(m3), sizeof(device));

if ( m1 == M1 && m2 == M2 && m3 == M3 )  {
    long int    tripTime = getTripTime( (struct timeval *) &(icmpPacket->icmp_data) );
    time_t      t=0;

    //  check if received packet corresponds to ANY device
    for ( i=0 ; i<devicesList->nDevices ; i++ )  {
        if ( iphdr->saddr == devicesList->d[i].ipAddr32 && device == devicesList->d[i].deviceId) {

            if (_verbose > 0)
                printf("\n ICMP packet received: Magic OK. Matches device %i (%s)", device, devicesList->d[i].ip);    

            devicesList->d[i].lastPingRTT = tripTime;
            devicesList->d[i].lastPingOK = t;

            // update ALSO interfaces, for simplicity in data representation
			for (j=0 ; j < interfacesList->nInterfaces ; j++)  { 
                if (interfacesList->d[j].enable > 0 && interfacesList->d[j].deviceId == device) {
                    interfacesList->d[j].lastPingOK = t;
                    interfacesList->d[j].lastPingRTT = tripTime;
                    }
                }
            break;   // no need to continue with other devices
            }
        }
    }

return (retVal);
}

//--------------------------------------------------------------------

// listen for ICMP REPLY and update DEVICES as they are received 
// matching by IP, magic numbers and device

int 	receiveMultiPing(devicesShm *devicesList, interfacesShm *interfacesList) {
int                 localSocket = 0, sockErr=0;
fd_set              rfds;
struct timeval      tv;
int                 auxErr=0, retVal = -9, receiveLen=0, end=0, count;
char                receiveBuffer[MAXLLEN];

if ( (sockErr = buildSocket( & localSocket )) != 0 )  {
    fprintf(stderr, "\n\nERROR creating SOCKET (%i): are you root? \n\n", sockErr);    fflush(stderr);
    retVal = -1; 
    }

while (!end) {    

    // preventive socket clean - up
    if (++count > 10000)  {
        if (localSocket)  
            close(localSocket);

        if ( (sockErr = buildSocket( & localSocket )) != 0 )  {
            fprintf(stderr, "\n\nERROR creating SOCKET (%i): are you root? \n\n", sockErr);    fflush(stderr);
            retVal = -1; 
            }
        count = 0;    
        }

    // whatch for socket, read conditions
    FD_ZERO(&rfds);   FD_SET(localSocket, &rfds);

    // Wait for timeout (3 sec.)
    tv.tv_sec = 3;  tv.tv_usec = 0;

    auxErr = select(1 + localSocket, &rfds, NULL, NULL, &tv);
    if (auxErr == -1) {
        perror("select()");
        retVal = -2;
        }
    else if (auxErr)   {
        if (FD_ISSET(localSocket, &rfds))   {
            if (receiveICMP(localSocket, receiveBuffer, &receiveLen) == 0) {
                if (validateAndUpdateDevices(receiveBuffer, receiveLen, devicesList, interfacesList) == 0) 
                    retVal = 0;
                else    
                    retVal = -3;    // validate error
                } 
            else 
                retVal = -4;    // Receive Error
            }    
        }
    else        
        retVal = -5;  //  timeout error 
    }

if (localSocket)  
    close(localSocket);

return(retVal);
}

//--------------------------------------------------------------------
//// EXTRAS
/*

int icmpTest(int argc, char *argv[])
{

    if (ping("www.google.com", 4000))
        printf("Ping is not OK. \n");
    else
        printf("Ping is OK. \n");

    return 0;
}



//--------------------------------------------------------------------
//--- ping - Create message and send it.                           ---
//    return 0 is ping Ok, return 1 is ping not OK.                ---
//--------------------------------------------------------------------
int ping2(char *address)
{
    int               val = 255;
    //int                     i=0;
    static int              counter=0, sd=0;
    struct packet           pckt, pckt2;
    struct sockaddr_in      receiveAddr;
    int                     loop;
    struct hostent          *hname;
    struct sockaddr_in      addr_ping, *addr;

    struct icmp             *icmp;
    static char             sendbuf[1500];

    int                     len=0;
    int                     datalen=56; //, size=0, res=0;
    int                     sockopt;

    //int pid = getpid();
    struct protoent    *proto = getprotobyname("ICMP");
    hname = gethostbyname(address);
    bzero(&addr_ping, sizeof(addr_ping));
    addr_ping.sin_family = hname->h_addrtype;
    addr_ping.sin_port = 0;
    addr_ping.sin_addr.s_addr = *(long *)(hname->h_addr);

    addr = &addr_ping;

    // for sanity close and re - create RAW socket. It Works for first time too...
    if ( --counter  <= 0 ) {
        counter=1000;
        close (sd);

        proto = getprotobyname("icmp");
        //  if getprotobyname failed, just silently force proto->p_proto to have the correct value for "icmp"
        
        sd = socket(PF_INET, SOCK_RAW, proto->p_proto);
        if ((sd = socket(AF_INET, SOCK_RAW,  (proto ? proto->p_proto : 1))) < 0) {
            fprintf( stderr,  "ping (socket error): possible permission denied. (are you root?)\n");
            return(0);
            }

        // set recv buf for broadcast pings 
        sockopt = 48 * 1024;
        setsockopt(sd, SOL_SOCKET, SO_RCVBUF, (char *) &sockopt, sizeof(sockopt));
    
        val = 255;
        if (setsockopt(sd, SOL_IP, IP_TTL, &val, sizeof(val)) != 0)    {
            perror("Set TTL option");
            return 1;
            }
        
        if (fcntl(sd, F_SETFL, O_NONBLOCK) != 0) {
            perror("Request nonblocking I/O");
            return 1;
            }
        }

//   bzero(&pckt, sizeof(pckt));
//    pckt.hdr.type = ICMP_ECHO;
//    
//    pckt.hdr.un.echo.id = getpid();
//    for (i = 0; i < sizeof(pckt.msg) - 1; i++)
//        pckt.msg[i] = i + '0';
//    pckt.msg[i] = 0;
//    pckt.hdr.un.echo.sequence = cnt++;
//    //pckt.hdr.checksum = checksum(&pckt, sizeof(pckt));
//    pckt.hdr.checksum = in_cksum((unsigned short *)&pckt, sizeof(pckt));


    icmp = (struct icmp *) sendbuf;
    icmp->icmp_type = ICMP_ECHO;
    icmp->icmp_code = 0;
    icmp->icmp_id = (getpid());
    //nsent = (nsent>10000) ? 0 : (nsent + 1);
    icmp->icmp_seq = (4);
    gettimeofday((struct timeval *) icmp->icmp_data, NULL);


    len = 8 + datalen;              // checksum ICMP header and data 
    icmp->icmp_cksum = 0;
    icmp->icmp_cksum = in_cksum((u_short *) icmp, len);

    {
    //struct sockaddr_in sa;
    char buffer[INET_ADDRSTRLEN];
    inet_ntop( AF_INET, &(addr->sin_addr), buffer, sizeof( buffer ));
    // printf( "address:%s\n", buffer );

//    printf("\n SENT: TYPE: %i PID - %i (%i)  SEQ: %i ADDR - %s", pckt.hdr.type, pckt.hdr.un.echo.id, getpid() , pckt.hdr.un.echo.sequence, buffer );
    printf("\n SENT: TYPE: %i CODE: %i PID - %i (%i)  SEQ: %i ADDR - %s", icmp->icmp_type, icmp->icmp_code, icmp->icmp_id, getpid() , icmp->icmp_seq, buffer );
//    printf("\n SENT: PID - %i    ADDR - %s", pckt.hdr.un.echo.id, ((struct sockaddr *)addr)->sa_data );
    }

    
    printBuffer(sendbuf, len);
    if (sendto(sd, sendbuf, len, 0, (struct sockaddr *)addr, sizeof(*addr)) <= 0)
        perror("sendto");

    // to receive over the same packet / buffer
    bzero(&pckt2, sizeof(pckt));    
    
    usleep(800000);

    for (loop = 0; loop < 10; loop++)  {
        int recvAddrLen = sizeof(receiveAddr);
        int retLen=0, dataSize=0;
        char recvBuffer[MAXLLEN];

        if ( (retLen = recvfrom(sd, recvBuffer, sizeof(recvBuffer), 0, 
            (struct sockaddr *)&receiveAddr, (socklen_t *)&recvAddrLen)) > 0) {

            char                    addrBuffer[INET_ADDRSTRLEN];
            struct icmp             *icmppkt;
            struct iphdr            *iphdr;
            int                     hlen=0;

            //printBuffer(recvBuffer, retLen);


            inet_ntop( AF_INET,  &(receiveAddr.sin_addr) , addrBuffer, sizeof( addrBuffer ));

            iphdr = (struct iphdr *) recvBuffer;
            hlen = iphdr->ihl << 2;

            printf("\n\n header len: %i (%i) \n\n", iphdr->ihl, hlen);

            dataSize = retLen - hlen;
            icmppkt = (struct icmp *) (addrBuffer + hlen);

            printBuffer((char *)icmppkt, retLen);



//       printf("\n RECV len:%i TYPE: %i PID - %i (%i) SEQ: %i  ADDR - %s", 
//          retLen,   pckt2.hdr.type, pckt2.hdr.un.echo.id , htons(pckt2.hdr.un.echo.id) ,
//          pckt2.hdr.un.echo.sequence,  addrBuffer) ;
//
            printf("\n RECV len:%i datalen: %i TYPE: %i PID - %i (%i) SEQ: %i  ADDR - %s", 
            retLen,  dataSize,  icmppkt->icmp_type, icmppkt->icmp_id , htons(icmppkt->icmp_id) ,
            icmppkt->icmp_seq,  addrBuffer) ;


            //return 0;
            }
        else if (retLen == -1 && errno==EAGAIN)  {
            printf("\n RECV -1 and EAGAIN:  trying again....");
            }
        else if (retLen < (DEFDATALEN + ICMP_MINLEN)) { // discard if too short
            printf("\n RECV too short (%i) minimunL %i", retLen,  (DEFDATALEN + ICMP_MINLEN)) ;
            }

        usleep(200000);
        }
// /    return(0); 

    return 1;
}

// ------------------------------------------------------------
// returns printable type of ICMP packet

static char *icmp_type_name (int id)
{
switch (id)
    {
    case ICMP_ECHOREPLY:            return "Echo Reply";
    case ICMP_DEST_UNREACH:         return "Destination Unreachable";
    case ICMP_SOURCE_QUENCH:        return "Source Quench";
    case ICMP_REDIRECT:             return "Redirect (change route)";
    case ICMP_ECHO:                 return "Echo Request";
    case ICMP_TIME_EXCEEDED:        return "Time Exceeded";
    case ICMP_PARAMETERPROB:        return "Parameter Problem";
    case ICMP_TIMESTAMP:            return "Timestamp Request";
    case ICMP_TIMESTAMPREPLY:       return "Timestamp Reply";
    case ICMP_INFO_REQUEST:         return "Information Request";
    case ICMP_INFO_REPLY:           return "Information Reply";
    case ICMP_ADDRESS:              return "Address Mask Request";
    case ICMP_ADDRESSREPLY:         return "Address Mask Reply";
    default:                        return "unknown ICMP type";
    }
}

//-------------------------------------------------------------------
// network to host conversion

char *sock_ntop(const struct sockaddr *sa, socklen_t salen) {
char                portstr[7];
static char         str[128];               // Unix domain is largest 

switch (sa->sa_family)      {
    case AF_INET:
        {
        struct sockaddr_in      *sin = (struct sockaddr_in *) sa;

        if (inet_ntop(AF_INET, &sin->sin_addr, str, sizeof(str)) == NULL)
            return(NULL);
        if (ntohs(sin->sin_port) != 0) {
            snprintf(portstr, sizeof(portstr), ".%d", ntohs(sin->sin_port));
            strcat(str, portstr);
            }
        return(str);
        }
// end sock_ntop 

    default:
        snprintf(str, sizeof(str), "sock_ntop: unknown AF_xxx: %d, len %d", sa->sa_family, salen);
        return(str);
    }
return (NULL);
}

//-------------------------------------------------------------------
// return hostname

struct addrinfo * Host_serv(const char *host, const char *serv, int family, int socktype) {
int                         n;
struct addrinfo             hints, *res;

bzero(&hints, sizeof(struct addrinfo));
hints.ai_flags = AI_CANONNAME;  // always return canonical name 
hints.ai_family = family;       // 0, AF_INET, AF_INET6, etc. 
hints.ai_socktype = socktype;   // 0, SOCK_STREAM, SOCK_DGRAM, etc. 

 if ( (n = getaddrinfo(host, serv, &hints, &res)) != 0)
    printf( "host_serv error for %s, %s: %s", (host == NULL) ? "(no hostname)" : host, 
    (serv == NULL) ? "(no service name)" : serv, gai_strerror(n));


return(res);    // return pointer to first on linked list 
}


//--------------------------------------------------------------------
//--- checksum - standard 1s complement checksum                   ---
//--------------------------------------------------------------------
unsigned short checksum(void *b, int len)
{
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;

    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    if (len == 1)
        sum += *(unsigned char *)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

//--------------------------------------------------------------------

int bind_using_iface_ip(int fd, char *ipaddr, uint16_t port)
{
    struct sockaddr_in localaddr = {0};
    localaddr.sin_family    = AF_INET;
    localaddr.sin_port  = htons(port);
    localaddr.sin_addr.s_addr = inet_addr(ipaddr);
    return bind(fd, (struct sockaddr*) &localaddr, sizeof(struct sockaddr_in));
}

//--------------------------------------------------------------------

int bind_using_iface_name(int fd, char *iface_name)
{
    return setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, iface_name, strlen(iface_name));
}


*/