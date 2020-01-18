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


#include "ifaceData.h"

#define PACKETSIZE 64
struct packet
{
    struct icmphdr hdr;
    char msg[PACKETSIZE - sizeof(struct icmphdr)];
};

int pid = -1;
struct protoent *proto = NULL;
int cnt = 1;

/*--------------------------------------------------------------------*/
/*--- checksum - standard 1s complement checksum                   ---*/
/*--------------------------------------------------------------------*/
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

/*--------------------------------------------------------------------*/
/*--- ping - Create message and send it.                           ---*/
/*    return 0 is ping Ok, return 1 is ping not OK.                ---*/
/*--------------------------------------------------------------------*/
int ping(char *address)
{
    const int               val = 255;
    int                     i=0;
    static int              counter=0, sd=0;
    struct packet           pckt;
    struct sockaddr_in      r_addr;
    int                     loop;
    struct hostent          *hname;
    struct sockaddr_in      addr_ping, *addr;

    pid = getpid();
    proto = getprotobyname("ICMP");
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
        sd = socket(PF_INET, SOCK_RAW, proto->p_proto);
    }

    if (sd < 0) {
        perror("socket");
        return 1;
    }

    if (setsockopt(sd, SOL_IP, IP_TTL, &val, sizeof(val)) != 0)
    {
        perror("Set TTL option");
        return 1;
    }
    if (fcntl(sd, F_SETFL, O_NONBLOCK) != 0)
    {
        perror("Request nonblocking I/O");
        return 1;
    }

    for (loop = 0; loop < 10; loop++)
    {

        int len = sizeof(r_addr);

        if (recvfrom(sd, &pckt, sizeof(pckt), 0, (struct sockaddr *)&r_addr, (socklen_t *)&len) > 0)
        {
            return 0;
        }

        bzero(&pckt, sizeof(pckt));
        pckt.hdr.type = ICMP_ECHO;
        pckt.hdr.un.echo.id = pid;
        for (i = 0; i < sizeof(pckt.msg) - 1; i++)
            pckt.msg[i] = i + '0';
        pckt.msg[i] = 0;
        pckt.hdr.un.echo.sequence = cnt++;
        pckt.hdr.checksum = checksum(&pckt, sizeof(pckt));
        if (sendto(sd, &pckt, sizeof(pckt), 0, (struct sockaddr *)addr, sizeof(*addr)) <= 0)
            perror("sendto");

        usleep(200000);
    }

    return 1;
}

/*--------------------------------------------------------------------*/
/*--- main - look up host and start ping processes.                ---*/
/*--------------------------------------------------------------------*/
int icmpTest(int argc, char *argv[])
{

    if (ping("www.google.com"))
        printf("Ping is not OK. \n");
    else
        printf("Ping is OK. \n");

    return 0;
}
