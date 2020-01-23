


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
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

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <signal.h>
#include <pty.h>
#include <math.h>

#include <sys/mman.h> 

#include <sys/timeb.h>

#include "ifaceData.h"                                                                                                     

//------------------------------------------------------------------------
//------------------------------------------------------------------------

// random delay in seconds

int randomDelay(int min, int max) {

    int randomDelay = 5;

    if ( min < 0 )   min = 0;
    if ( max < min )  max = min + 1;

    srand(getpid());
    randomDelay = (rand() % (max - min) ) + min;

    if (_verbose > 0) {
        printf("\n Random sleep (%i seconds) ", randomDelay);
        fflush(stdout);
        }

    sleep(randomDelay);

    return(1);
}

//------------------------------------------------------------------------

char *adc_ltrim(char *s)
{
    while(isspace(*s)) s++;
    return s;
}

//------------------------------------------------------------------------

char *adc_rtrim(char *s)
{
    char* back = s + strlen(s);
    while(isspace(*--back));
    *(back+1) = '\0';
    return s;
}

//------------------------------------------------------------------------

char *adc_trim(char *s)
{
    return (adc_rtrim(adc_ltrim(s))); 
}

//------------------------------------------------------------------------

// get de (first) useful char of string 
int str_firstchar(char *s)
{
	if (!s) 	
		return(0);
	
	while (*s == ' ' || *s == '\t' )
		s++;
	
	return ((int)*s); 
}

//------------------------------------------------------------------------

int str_extract(char *s, int n1, int n2, char *ret)
{
	int j=0, n=0;

	if (!s) return(0);
	n=strlen(s);
	ret[0]=0;
	
	while (s[n1+j]!=0 && (n1+j)<=n2 && j<n )
		{
		ret[j] = s[n1 + j];
		j++;
		}
				
	ret[j] = 0;			
	return(1);
}

//------------------------------------------------------------------------

//
// extract string FROM specific character, till the end of line, excluding whitespaces and CR, LF
//

int str_extract_from(char *s, int c1, char *ret)
{
	int i=0, j=0, n=0, found=0;

	if (!s) return(0);
	n=strlen(s);
	ret[0]=0;
	
	// find first char 
	while ( s[j]!=c1 && j<n )
		j++;
	
	if (s[j] == c1)   // found
		{
		found=1;
		j++;
		
		while ( s[j]!=0 && j<n )
			{
			if ( s[j]!=' ' && s[j]!=13 && s[j]!=10 )
				ret[i++] = s[j];
			j++;
			}	
		ret[i] = 0;			
		}
		
	return(found);
}

//------------------------------------------------------------------------

// extrae el nombre de host de una linea del tipo "XXXXX HOStname> YYYY"

int   get_hostname(char *buffer_aux, char *hostname_str)
{
char    *ptr=NULL;
int     i=0;

if (!buffer_aux || !hostname_str)
  return(0);

ptr=buffer_aux;
while (*ptr!=0 && (*ptr == '\n' || *ptr == '\r' || *ptr == ' '))
  ptr++;

if (*ptr == 27 && *(ptr+1) == 91)   // ESC[   on allied telesis!!!
    ptr+=3;

while (*ptr!= 0 && *ptr != '>' && *ptr != '#')
  hostname_str[i++] = *(ptr++);

hostname_str[i++] = 0;
return(1);
}

//------------------------------------------------------------------------

int read_stream(int fd, char *ptr, int maxbytes)
{
int             nleft=0, nread=0, count=0;

nleft = maxbytes;
while (nleft > 0 && ++count<5)
    {
    if ( (nread = read(fd, ptr, nleft)) < 0)
        return(nread);          /* error, return < 0 */
    else if (nread == 0)
        break;                          /* EOF, return #bytes read */
    nleft -= nread;
    ptr   += nread;
    usleep(5000);
    }
return(maxbytes - nleft);               /* return >= 0 */
}

//------------------------------------------------------------------------

// wait for a given string, 'retries' times, tout (msec) each time

int wait_for_string(int fd, long int tout, int retries, char *expect, char *buffer, int maxbytes, int delaytime)
{
fd_set                  rset;
struct timeval          tv;
int                     ret=0, found=0;
int                     nleft=0, nread=0, count=0;
char                    *ptr=NULL;
long int 				tout_sec=0;
long int 				tout_usec=0;

if (fd<=0 || !buffer || !expect)
    return(0);

if (retries<2) retries=2;
tout*=1000; if (tout < 10000) tout = 10000;
delaytime*=1000; if (delaytime < 10000) delaytime = 10000;

tout_sec=tout/1000000;
tout_usec=tout%1000000;

nleft = maxbytes;
memset(buffer, 0, maxbytes);
ptr=buffer;

if (_verbose>3)
	{
	printf("\nWAIT FOR: |%s|", expect);
	fflush(stdout);
	}

while (nleft > 0 && count++ < retries && !found)
    {
    FD_ZERO(&rset);
    FD_SET(fd, &rset);

    tv.tv_sec = tout_sec;
    tv.tv_usec = tout_usec;

    if ( (ret = select(fd+1, &rset, NULL, NULL, &tv)) < 0)
        return(0);
    else if (ret>0)
        {
        usleep(delaytime);
        if ( (nread = read(fd, ptr, nleft)) <= 0)
            return(0);          /* error or EOF, return 0 */
        if (_verbose>3)
			{
            printf("\nRES: |%i|%s|", nread, buffer);
            fflush(stdout);
            }

        nleft -= nread;
        ptr   += nread;
        *ptr=0;
        if (strstr(buffer, expect))
      		{
//      		str_normalize(buffer, nread);
            return(1);
            }
        }
    }


        if (_verbose>3)
			{
            printf("\n wait_for_string exists with 0 by default! ");
            fflush(stdout);
            }


return(0);
}

//-------------------------------------------------------------------


// wait for a given string, 'retries' times, tout (msec) each time

int wait_for_string_nonzero(int fd, long int tout, int retries, char *expect, char *buffer, int maxbytes, int delaytime)
{
fd_set                  rset;
struct timeval          tv;
int                     ret=0, found=0;
int                     nleft=0, nread=0, count=0;
char                    *ptr=NULL;
long int 				tout_sec=0;
long int 				tout_usec=0;

if (fd<=0 || !buffer || !expect)
    return(0);

if (retries<2) retries=2;
tout*=1000; if (tout < 10000) tout = 10000;
delaytime*=1000; if (delaytime < 10000) delaytime = 10000;

tout_sec=tout/1000000;
tout_usec=tout%1000000;

nleft = maxbytes;
memset(buffer, 0, maxbytes);
ptr=buffer;

if (_verbose>3)
	{
	printf("\nWAIT FOR: |%s|", expect);
	fflush(stdout);
	}

while (nleft > 0 && count++ < retries && !found)
    {
    FD_ZERO(&rset);
    FD_SET(fd, &rset);

    tv.tv_sec = tout_sec;
    tv.tv_usec = tout_usec;

    if ( (ret = select(fd+1, &rset, NULL, NULL, &tv)) < 0)
        return(0);
    else if (ret>0)
        {
        usleep(delaytime);
        if ( (nread = read(fd, ptr, nleft)) <= 0)
            return(0);          /* error or EOF, return 0 */
        if (_verbose>3)
			{
            printf("\nRES: |%i|%s|", nread, buffer);
            fflush(stdout);
            }

		{ // replace characters in '0' in read chunk
		int j=0;
		for (j=0 ; j<nread ; j++)
			if (ptr[j] == 0)
				ptr[j] = 32;	
		}	

        nleft -= nread;
        ptr   += nread;
        *ptr=0;
        if (strstr(buffer, expect))
      		{
//      		str_normalize(buffer, nread);
            return(1);
            }
        }
    }


        if (_verbose>3)
			{
            printf("\n wait_for_string exists with 0 by default! ");
            fflush(stdout);
            }


return(0);
}

//-------------------------------------------------------------------






// try to read from a descriptor for some time, retrying
// tout is in msec

int read_wait(int fd, long int tout, int retries, char *buffer, int maxbytes)
{
fd_set                  rset;
struct timeval          tv;
int                     ret=0;
int                     nleft=0, nread=0, count=0;
unsigned char           *ptr=NULL;
long int 				tout_sec=0;
long int 				tout_usec=0;

if (fd<=0 || !buffer)
    return(0);

if (retries<2) retries=2;
tout*=1000;
if (tout < 10000) tout = 10000;
tout_sec=tout/1000000;
tout_usec=tout%1000000;

printf("\n | %li | %li | \n ", tout_sec , tout_usec);  fflush(stdout);

nleft = maxbytes;
memset(buffer, 0, maxbytes);
ptr=(unsigned char *)buffer;

while (nleft > 0 && count++ < retries)
    {
    FD_ZERO(&rset);
    FD_SET(fd, &rset);

    tv.tv_sec = tout_sec;
    tv.tv_usec = tout_usec;

    if ( (ret = select(fd+1, &rset, NULL, NULL, &tv)) < 0)
        return(0);
    else if (ret>0)
        {
        if ( (nread = read(fd, ptr, nleft)) <= 0)
            return(0);          /* error or EOF, return 0 */

        if (_verbose>3)
            printf("\nRES: |%s|", buffer);

        nleft -= nread;
        ptr   += nread;
        *ptr=0;
        }
    }
return(1);
}

//------------------------------------------------------------------------

int str_normalize(unsigned char *str, int slen)
{
int i=0;

if (!str ||  slen<1)
  return(0);

for (i=0 ; str[i] && i<slen; i++)  
  if ( (str[i]<32 || str[i]>125) && str[i]!=13 && str[i]!=10)
	str[i]=' ';

return(1);  
}

//------------------------------------------------------------------------

int send_cmd(int fd, char  *cmd)
{
int 	ret=0;

if (fd>0 && cmd)  
  {
  ret = write(fd, cmd, strlen(cmd)); /* Write to child.s stdin */
  return(1);
  }
return(ret);  
}

//------------------------------------------------------------------------

int send_control_d(int fd)
{
int 	ret=0;
char 	aux[10];

aux[0] = 4;
if (fd>0)  
  ret = write(fd, aux, 1);  // send CONTROL - D (0x04)
return(ret);  
}

//------------------------------------------------------------------------
//------------------------------------------------------------------------

// basic average for intervals, value returned in {avg)
int avg_basic(long int * buffer, short ini, short fin, double *avg)
{
int i=0, n=0;

// check basic limits
if (ini<0 || fin <0 || ini >= MAXAVGBUF || fin >= MAXAVGBUF)
	return(0);
	
*avg=0;
for (i=ini, n=0 ; i<=fin ; i++, n++)
	*avg+=buffer[i];

*avg/=n;
return(1);
}

//------------------------------------------------------------------------

// first attempt to detect low traffic

int detect_low_traffic_01(long int * buffer, double *calc)
{
double a=0,b=0;

avg_basic(buffer, 0, 1, &a);
avg_basic(buffer, 2, (MAXAVGBUF - 1), &b);

if (fabs(a) < 0.1)	// avoid division by zero exception
	a=0.1;	

*calc = fabs( (b - a) / a );
return(1);
}
//------------------------------------------------------------------------

// all values in input avg buffer AND output avg buffer MUST be over threshold
// VERY restrictive condition for 'normal traffic'
int detect_normal_traffic_01(deviceData *d, interfaceData *iface)
{
int i=0;

// any value in INPUT OR OUTPUT below threshold means no compliance (abnormal traffic condition)
for (i=0 ; i<MAXAVGBUF ; i++)
	if ( iface->ibw_buf[i] < iface->alarm_lo )
		return(0);

for (i=0 ; i<MAXAVGBUF ; i++)
	if ( iface->obw_buf[i] < iface->alarm_lo )
		return(0);

return(1);
}

//------------------------------------------------------------------------

// check if current hour/minute is in a given interval
int in_interval(struct tm *current, short inih, short inim, short finh, short finm)
{
int 	tcur=0, tini=0, tfin=0;

// some of the values are invalid -> no evaluation of interval
if (inih < 0 || inim < 0 || finh < 0 || finm < 0)
	return(0);
	
tcur = current->tm_hour	* 60 + current->tm_min;
tini = inih * 60 + inim;
tfin = finh * 60 + finm;
	
if (tini <= tfin)	// ej:  03:20 to 09:40 or 12:10 - 12:45
	{
	if (tini<=tcur && tcur<=tfin)
		return(1);
	}
else     // ej:  22:20 to 02:40
	{
	if (tfin<=tcur || tcur<=tfin)
		return(1);
	}
		
return(0);
}

//------------------------------------------------------------------------


void* create_shared_memory(size_t size) { 
    // Our memory buffer will be readable and writable: 
    int protection = PROT_READ | PROT_WRITE; 

    // The buffer will be shared (meaning other processes can access it), but 
    // anonymous (meaning third-party processes cannot obtain an address for it), 
    // so only this process and its children will be able to use it: 
    int visibility = MAP_ANONYMOUS | MAP_SHARED; 

    // The remaining parameters to `mmap()` are not important for this use case, 
    // but the manpage for `mmap` explains their purpose. 
    return mmap(NULL, size, protection, visibility, 0, 0); 
} 
//-------------------------------------------------------------------
