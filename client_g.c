/* client */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
 
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
 
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
 
#define SERVERIP "127.0.0.1"
#define SERVERPORT 8080
#define LENGTH 512 
#define BUFFSIZE 1024
#define NAMELENGTH 32
#define OPTLEN 16
#define LINEBUFF 2048
 

struct PACKET {
    char option[OPTLEN]; // instruction
    char nickname[NAMELENGTH]; // client's nickname
    char buff[BUFFSIZE]; // payload
    int clientID;

}; 
struct USER {
    int sockfd; // user's socket descriptor
    char nickname[NAMELENGTH]; // user's name
    int clientID;
};

 
struct THREADINFO {
    pthread_t thread_ID; // thread's pointer
    int sockfd; // socket file descriptor
};
 
int isconnected, sockfd;
char option[LINEBUFF];
struct USER me;

void sendFileToServer(struct USER *me);
int connect_with_server();
void setnickname(struct USER *me);
void logout(struct USER *me);
void login(struct USER *me);
void *receiver(void *param);
void *fileReceiver(void *param);
void broadcast(struct USER *me, char *msg);
void whisp(struct USER *me, char *msg);
void chat(struct USER *me);
void quit(struct USER *me);
void flag(struct USER *me);

int main(int argc, char **argv) {
    int sockfd, namereallength;
    
    memset(&me, 0, sizeof(struct USER));
    while(gets(option)) {
        if(!strncmp(option, "exit", 4)) {
            logout(&me);
            break;
        }
        if(!strncmp(option, "help", 4)) {
            FILE *fin = fopen("help.txt", "r");
            if(fin != NULL) {
                while(fgets(option, LINEBUFF-1, fin)) puts(option);
                fclose(fin);
            }
            else {
                fprintf(stderr, "Help file not found...\n");
            }
        }
        else if(!strncmp(option, "connect", 7)) {
            char *ptr = strtok(option, " ");
            ptr = strtok(0, " ");
            memset(me.nickname, 0, sizeof(char) * NAMELENGTH);
            if(ptr != NULL) {
                namereallength = strlen(ptr);
                if(namereallength > NAMELENGTH) ptr[NAMELENGTH] = 0;
                strcpy(me.nickname, ptr);
            }
            else {
                strcpy(me.nickname, "Anonymous");
            }
            login(&me);
        }
        
        else if(!strncmp(option, "nickname", 8)) {
            char *ptr = strtok(option, " ");
            ptr = strtok(0, " ");
            memset(me.nickname, 0, sizeof(char) * NAMELENGTH);
            if(ptr != NULL) {
                namereallength = strlen(ptr);
                if(namereallength > NAMELENGTH) ptr[NAMELENGTH] = 0;
                strcpy(me.nickname, ptr);
                setnickname(&me);
            }
        }
        
        /* a client expresses the desire to chat */
        else if(!strncmp(option, "chat", 5)){   
            printf("CLIENT: Assigning partner...\n");
            chat(&me);
        }
        
        else if(!strncmp(option, "whisp", 5)) {
            whisp(&me, &option[6]);        
        }
        else if(!strncmp(option, "transfer", 8)) {
            sendFileToServer(&me);        
        }
        else if(!strncmp(option, "flag", 4)){
            flag(&me);
        }
        else if(!strncmp(option, "quit", 4)){
            quit(&me);
        }
        else if(!strncmp(option, "broadcast", 9)) {
            broadcast(&me, &option[10]);
        }
        
        else if(!strncmp(option, "logout", 6)) {
            logout(&me);
        }
        
        else fprintf(stderr, "Unknown option...\n");
    }
    return 0;
}
 
void login(struct USER *me) {
    if(isconnected) {
        fprintf(stderr, "You are already connected to server at %s:%d\n", SERVERIP, SERVERPORT);
        return;
    }
    sockfd = connect_with_server();
    if(sockfd >= 0) {
        isconnected = 1;
        me->sockfd = sockfd;
        if(strcmp(me->nickname, "Anonymous")) setnickname(me);
        printf("Logged in as %s\n", me->nickname);
        printf("Receiver started [%d]...\n", sockfd);
        struct THREADINFO threadinfo;
        pthread_create(&threadinfo.thread_ID, NULL, receiver, (void *)&threadinfo);
		pthread_create(&threadinfo.thread_ID, NULL, fileReceiver, (void *)&threadinfo);
    }
    else {
        fprintf(stderr, "Connection rejected...\n");
    }
}

int connect_with_server() {
    int newfd, err_ret;
    struct sockaddr_in serv_addr;
    struct hostent *to;
     
    /* generate address */
    if((to = gethostbyname(SERVERIP))==NULL) {
        err_ret = errno;
        fprintf(stderr, "gethostbyname() error...\n");
        return err_ret;
    }
     
    /* open a socket */
    if((newfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        err_ret = errno;
        fprintf(stderr, "socket() error...\n");
        return err_ret;
    }
     
    /* set initial values */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVERPORT);
    serv_addr.sin_addr = *((struct in_addr *)to->h_addr);
    memset(&(serv_addr.sin_zero), 0, 8);
     
    /* try to connect with server */
    if(connect(newfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1) {
        err_ret = errno;
        fprintf(stderr, "connect() error...\n");
        return err_ret;
    }
    else {
        printf("Connected to server at %s:%d\n", SERVERIP, SERVERPORT);
        return newfd;
    }
}

void logout(struct USER *me) {
    int sent;
    struct PACKET packet;
    if(!isconnected) {
        fprintf(stderr, "You are not connected...\n");
        return;
    }
    memset(&packet, 0, sizeof(struct PACKET));
    strcpy(packet.option, "exit");
    strcpy(packet.nickname, me->nickname);
    sent = send(sockfd, (void *)&packet, sizeof(struct PACKET), 0);
    isconnected = 0;
}

void setnickname(struct USER *me) {
    int sent;
    struct PACKET packet;
    if(!isconnected) {
        fprintf(stderr, "You are not connected...\n");
        return;
    }
    memset(&packet, 0, sizeof(struct PACKET));
    strcpy(packet.option, "nickname");
    strcpy(packet.nickname, me->nickname);
    sent = send(sockfd, (void *)&packet, sizeof(struct PACKET), 0);
}
 
void *receiver(void *param) {
    int recvd;
    struct PACKET packet;
    printf("Waiting here [%d]...\n", sockfd);
    while(isconnected) {
        recvd = recv(sockfd, (void *)&packet, sizeof(struct PACKET), 0);
        if(!recvd) {
            fprintf(stderr, "Connection lost from server...\n");
            isconnected = 0;
            close(sockfd);
            break;
        }
        if(recvd > 0) {
            printf("[%s]: %s\n", packet.nickname, packet.buff);
            me.clientID = packet.clientID;
        }
        memset(&packet, 0, sizeof(struct PACKET));

    }
    return NULL;
}

void *fileReceiver(void *param){
	int recvd;
	char revbuf[LENGTH]; 
    struct PACKET packet;
    printf("Waiting here [%d]...\n", sockfd);
    while(isconnected) {
			//printf("[Client] Receiveing file from Server and saving it as final.txt...");
			char* fr_name = "receivedtoclient.jpg";
			FILE *fr = fopen(fr_name, "w");
			if(fr == NULL)
			{
					printf("File does not exist creating a new one", fr_name);
					fr = fopen ("receivedtoclient.jpg", "w+");
			}
				bzero(revbuf, LENGTH); 
				int fr_block_sz = 0;
				while((fr_block_sz = recv(sockfd, revbuf, LENGTH, 0)) > 0)
				{
					int write_sz = fwrite(revbuf, sizeof(char), fr_block_sz, fr);
					if(write_sz < fr_block_sz)
					{
						error("File write failed.\n");
					}
					bzero(revbuf, LENGTH);
					if (fr_block_sz == 0 || fr_block_sz != 512) 
					{
						break;
					}

				if(fr_block_sz < 0)
				{
					if (errno == EAGAIN)
					{
						printf("recv() timed out.\n");
					}
					else
					{
						fprintf(stderr, "recv() failed due to errno = %d\n", errno);
					}
				}
				printf("Ok received from server!\n");
				fclose(fr);
			}
    }
    return NULL;
}

void chat(struct USER *me){
    struct PACKET packet;
    if(!isconnected) {
        fprintf(stderr, "You are not connected...\n");
        return;
    }
    memset(&packet, 0, sizeof(struct PACKET));
    strcpy(packet.option, "chat");
    strcpy(packet.nickname, me->nickname);
    strcpy(packet.buff, "msg");
    packet.clientID = me->clientID;
    send(sockfd, (void *)&packet, sizeof(struct PACKET), 0);    
}

void quit(struct USER *me){
    struct PACKET packet;
    if(!isconnected) {
        fprintf(stderr, "You are not connected...\n");
        return;
    }
    memset(&packet, 0, sizeof(struct PACKET));
    strcpy(packet.option, "quit");
    strcpy(packet.nickname, me->nickname);
    strcpy(packet.buff, "msg");
    packet.clientID = me->clientID;
    send(sockfd, (void *)&packet, sizeof(struct PACKET), 0);    
}


  
void broadcast(struct USER *me, char *msg) {
    int sent;
    struct PACKET packet;
    if(!isconnected) {
        fprintf(stderr, "You are not connected...\n");
        return;
    }
    msg[BUFFSIZE] = 0;
    memset(&packet, 0, sizeof(struct PACKET));
    strcpy(packet.option, "broadcast");
    strcpy(packet.nickname, me->nickname);
    strcpy(packet.buff, msg);
    sent = send(sockfd, (void *)&packet, sizeof(struct PACKET), 0);
}


void whisp(struct USER *me, char *msg) {
    int sent;
    struct PACKET packet;
    if(msg == NULL) {
        return;
    }
    if(!isconnected) {
        fprintf(stderr, "You are not connected...\n");
        return;
    }
    msg[BUFFSIZE] = 0;
    memset(&packet, 0, sizeof(struct PACKET));
    strcpy(packet.option, "whisp");
    strcpy(packet.nickname, me->nickname);
    strcpy(packet.buff, msg);
    packet.clientID = me->clientID;
    sent = send(sockfd, (void *)&packet, sizeof(struct PACKET), 0);
}


void flag(struct USER *me){
    struct PACKET packet;
    if(!isconnected) {
        fprintf(stderr, "You are not connected...\n");
        return;
    }
    memset(&packet, 0, sizeof(struct PACKET));
    strcpy(packet.option, "flag");
    strcpy(packet.nickname, me->nickname);
    strcpy(packet.buff, "msg");
    packet.clientID = me->clientID;
    send(sockfd, (void *)&packet, sizeof(struct PACKET), 0);   
}

void sendFileToServer(struct USER *me)
{
	struct PACKET packet;
    if(!isconnected) {
        fprintf(stderr, "You are not connected...\n");
        return;
    }
    memset(&packet, 0, sizeof(struct PACKET));
    strcpy(packet.option, "transfer");
    strcpy(packet.nickname, me->nickname);
    strcpy(packet.buff, "msg");
    packet.clientID = me->clientID;

    send(sockfd, (void *)&packet, sizeof(struct PACKET), 0);   

		char* fs_name = "sentfromclient.jpg";
		char sdbuf[LENGTH]; 
		printf("[Client] Sending %s to the Server... ", fs_name);
		FILE *fs = fopen(fs_name, "r");
		if(fs == NULL)
		{
			printf("ERROR: File %s not found.\n", fs_name);
			exit(1);
		}

		bzero(sdbuf, LENGTH); 
		int fs_block_sz; 
		while((fs_block_sz = fread(sdbuf, sizeof(char), LENGTH, fs)) > 0)
		{
		    if(send(sockfd, sdbuf, fs_block_sz, 0) < 0)
		    {
		        fprintf(stderr, "ERROR: Failed to send file %s. (errno = %d)\n", fs_name, errno);
		        break;
		    }
		    bzero(sdbuf, LENGTH);
		}
		printf("Ok File %s from Client was Sent!\n", fs_name);
}
