/* server */
 
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
 
#define IP "127.0.0.1"
#define PORT 8080
#define BACKLOG 10
 
#define BUFFSIZE 1024
#define LINEBUFF 2048
#define NAMELENGTH 32
#define OPTLEN 16
#define LENGTH 512 
#define CLIENTNUM 10
#define PAIRNUM 5 

typedef enum { FALSE, TRUE } BOOL;


struct PACKET {
    char option[OPTLEN]; // instruction
    char nickname[NAMELENGTH]; // client's nickname
    char buff[BUFFSIZE]; // payload
    int clientID;// uniquely identify a client

};
 
struct THREADINFO {
    pthread_t thread_ID; // thread's pointer
    int sockfd; // socket file descriptor
    char nickname[NAMELENGTH]; // client's nickname
    int clientID; // uniquely identify a client
    int pairID; // indicate what channel this client is in, "0" means it's not in any channel, "-1" means it's blocked
};
 
struct LLNODE {
    struct THREADINFO threadinfo;
    struct LLNODE *next;
};
 
struct LLIST {
    struct LLNODE *head, *tail;
    int size;
};
 
 
int sockfd, newfd;
struct THREADINFO thread_info[CLIENTNUM];
struct LLIST client_list;
pthread_mutex_t clientlist_mutex;
int clientID = 1;
int globalpairID = 0;
int flag[CLIENTNUM] = {0};
 
void *io_handler(void *param);
void *client_handler(void *fd);
void block(char *ptr);
void unblock(char *ptr);
void throwout(char *ptr);

 
int compare(struct THREADINFO *a, struct THREADINFO *b) {
    return a->sockfd - b->sockfd;
}
 
void list_init(struct LLIST *ll) {
    ll->head = ll->tail = NULL;
    ll->size = 0;
}
 
int list_insert(struct LLIST *ll, struct THREADINFO *thr_info) {
    if(ll->size == CLIENTNUM) return -1;
    if(ll->head == NULL) {
        ll->head = (struct LLNODE *)malloc(sizeof(struct LLNODE));
        ll->head->threadinfo = *thr_info;
        ll->head->next = NULL;
        ll->tail = ll->head;
    }
    else {
        ll->tail->next = (struct LLNODE *)malloc(sizeof(struct LLNODE));
        ll->tail->next->threadinfo = *thr_info;
        ll->tail->next->next = NULL;
        ll->tail = ll->tail->next;
    }
    ll->size++;
    return 0;
}
 
int list_delete(struct LLIST *ll, struct THREADINFO *thr_info) {
    struct LLNODE *curr, *temp;
    if(ll->head == NULL) return -1;
    if(compare(thr_info, &ll->head->threadinfo) == 0) {
        temp = ll->head;
        ll->head = ll->head->next;
        if(ll->head == NULL) ll->tail = ll->head;
        free(temp);
        ll->size--;
        return 0;
    }
    for(curr = ll->head; curr->next != NULL; curr = curr->next) {
        if(compare(thr_info, &curr->next->threadinfo) == 0) {
            temp = curr->next;
            if(temp == ll->tail) ll->tail = curr;
            curr->next = curr->next->next;
            free(temp);
            ll->size--;
            return 0;
        }
    }
    return -1;
}
 
void list_traversal(struct LLIST *ll) {
    struct LLNODE *curr;
    struct THREADINFO *thr_info;
    int numchat = 0;

    for(curr = client_list.head; curr != NULL; curr = curr->next) {
        if(curr->threadinfo.pairID > 0){      
            numchat++;
        }           
    }
    printf("**************************************************\n");
    printf("Number of clients in the chat queue: %d\n", ll->size);
    printf("Number of clients chatting currently: %d\n",numchat);
    printf("sockfd nickname clientID pairID flag_time\n");
    for(curr = ll->head; curr != NULL; curr = curr->next) {
        thr_info = &curr->threadinfo;
        printf("[%d]    %s    %d         %d        %d\n", thr_info->sockfd, 
                thr_info->nickname, thr_info->clientID, thr_info->pairID, flag[thr_info->clientID - 1]);
    }
    printf("**************************************************\n");
}

int main(int argc, char **argv) {
    int err_ret, sin_size;
    struct sockaddr_in serv_addr, client_addr;
    pthread_t interrupt;
    char option[LINEBUFF];

    while(gets(option)) {
        if(!strncmp(option, "start", 5)) {            

            /* initialize linked list */
            list_init(&client_list);            

            /* initiate mutex */
            pthread_mutex_init(&clientlist_mutex, NULL);           

            /* create socket */
            if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
                err_ret = errno;
                fprintf(stderr, "socket() failed...\n");
                return err_ret;
            }           
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(PORT);
            serv_addr.sin_addr.s_addr = inet_addr(IP);
            memset(&(serv_addr.sin_zero), 0, 8);
             
            /* bind address with socket */
            if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1) {
                err_ret = errno;
                fprintf(stderr, "bind() failed...\n");
                return err_ret;
            }
             
            /* start listening for connection */
            if(listen(sockfd, BACKLOG) == -1) {
                err_ret = errno;
                fprintf(stderr, "listen() failed...\n");
                return err_ret;
            }
             
            /* initiate interrupt handler for IO controlling */
            printf("Starting admin interface...\n");
            if(pthread_create(&interrupt, NULL, io_handler, NULL) != 0) {
                err_ret = errno;
                fprintf(stderr, "pthread_create() failed...\n");
                return err_ret;
            }
             
            /* keep accepting connections */
            printf("Starting socket listener...\n");
            while(1) {
                sin_size = sizeof(struct sockaddr_in);
                if((newfd = accept(sockfd, (struct sockaddr *)&client_addr, (socklen_t*)&sin_size)) == -1) {
                    err_ret = errno;
                    fprintf(stderr, "accept() failed...\n");
                    return err_ret;
                }
                else {
                    if(client_list.size == CLIENTNUM) {
                        fprintf(stderr, "Connection full, request rejected...\n");
                        struct PACKET cpacket;
                        memset(&cpacket, 0, sizeof(struct PACKET));
                        strcpy(cpacket.option, "msg");
                        strcpy(cpacket.buff, "CLIENT: Connection full, request rejected...");
                        send(newfd, (void *)&cpacket, sizeof(struct PACKET), 0);
                        continue;
                    }
                    printf("Connection requested received...\n");
                    struct THREADINFO threadinfo;
                    threadinfo.sockfd = newfd;
                    strcpy(threadinfo.nickname, "Anonymous");
                    threadinfo.clientID = clientID++;
                    threadinfo.pairID = 0;
                    pthread_mutex_lock(&clientlist_mutex);
                    list_insert(&client_list, &threadinfo);
                    pthread_mutex_unlock(&clientlist_mutex);
                    pthread_create(&threadinfo.thread_ID, NULL, client_handler, (void *)&threadinfo);

                    struct PACKET packet;
                    memset(&packet, 0, sizeof(struct PACKET));
                    packet.clientID = threadinfo.clientID;
                    send(threadinfo.sockfd, (void *)&packet, sizeof(struct PACKET), 0);
                }
            }  
            return 0;
        }
        else{
            fprintf(stderr, "You need to call \"start\" to start server.\n");
        }
    }
}
 
void *io_handler(void *param) {
    char option[LINEBUFF];
    int targetfd;
    struct LLNODE *curr;

    while(gets(option)) {
        if(!strncmp(option, "exit", 4)) {
            printf("Terminating server...\n");
            pthread_mutex_destroy(&clientlist_mutex);
            close(sockfd);
            exit(0);
        }

        else if(!strncmp(option, "end", 3)) {
            printf("SERVER: Server is shut down.\n");
            pthread_mutex_lock(&clientlist_mutex);
            for(curr = client_list.head; curr != NULL; curr = curr->next) {
                if(curr->threadinfo.pairID <= 0){
                    struct PACKET spacket;
                    memset(&spacket, 0, sizeof(struct PACKET));
                    strcpy(spacket.option, "msg");
                    strcpy(spacket.nickname, curr->threadinfo.nickname);
                    strcpy(spacket.buff, "Server is shut down.");
                    send(curr->threadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);
                    list_delete(&client_list, &(curr->threadinfo));
                }
                else{
                    struct PACKET spacket;
                    memset(&spacket, 0, sizeof(struct PACKET));
                    strcpy(spacket.option, "msg");
                    strcpy(spacket.nickname, curr->threadinfo.nickname);
                    strcpy(spacket.buff, "Server is shutdown. Your chatting channel is ended.");
                    send(curr->threadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);
                    list_delete(&client_list, &(curr->threadinfo));
                }
            }
            pthread_mutex_unlock(&clientlist_mutex);
        }

        else if(!strncmp(option, "stats", 5)) {
            pthread_mutex_lock(&clientlist_mutex);
            list_traversal(&client_list);
            pthread_mutex_unlock(&clientlist_mutex);
        }

        else if(!strncmp(option, "block", 5)){
            char *ptr = strtok(option, " ");
            ptr = strtok(0, " ");
            block(ptr);          
        }        

        else if(!strncmp(option, "unblock", 7)){
            char *ptr = strtok(option, " ");
            ptr = strtok(0, " ");
            unblock(ptr);          
        }

        else if(!strncmp(option, "throwout", 8)){
            char *ptr = strtok(option, " ");
            ptr = strtok(0, " ");
            throwout(ptr);
        }

        else {
            fprintf(stderr, "Unknown command: %s\n", option);
        }

    }
    return NULL;
}
 
void *client_handler(void *fd) {
    struct THREADINFO threadinfo = *(struct THREADINFO *)fd;
    struct PACKET packet;
    struct LLNODE *curr;
    int bytes, sent;

    BOOL flagbusy = TRUE;// flagbusy is TRUE means no client availabe, FALSE mean at least one client is free

    while(1) {
        bytes = recv(threadinfo.sockfd, (void *)&packet, sizeof(struct PACKET), 0);
        if(!bytes) {
            fprintf(stderr, "Connection lost from [%d] %s...\n", threadinfo.sockfd, threadinfo.nickname);
            pthread_mutex_lock(&clientlist_mutex);
            list_delete(&client_list, &threadinfo);
            pthread_mutex_unlock(&clientlist_mutex);
            break;
        }
        
        if(!strcmp(packet.option, "nickname")) {
            printf("Set nickname to %s\n", packet.nickname);
            pthread_mutex_lock(&clientlist_mutex);
            for(curr = client_list.head; curr != NULL; curr = curr->next) {
                if(compare(&curr->threadinfo, &threadinfo) == 0) {
                    strcpy(curr->threadinfo.nickname, packet.nickname);
                    strcpy(threadinfo.nickname, packet.nickname);
                    break;
                }
            }
            pthread_mutex_unlock(&clientlist_mutex);
        }
                
        else if(!strcmp(packet.option, "chat")){
            struct THREADINFO cthreadinfo;
            pthread_mutex_lock(&clientlist_mutex);
            for(curr = client_list.head; curr != NULL; curr = curr->next) {
                if(curr->threadinfo.sockfd == threadinfo.sockfd){      
                    cthreadinfo = curr->threadinfo;
                }           
            }
            pthread_mutex_unlock(&clientlist_mutex); 
            // if the client is blocked, do nothing
            if(cthreadinfo.pairID == -1){
                printf("SERVER: You are blocked.\n");
            }
            // if the client already has a partner, do nothing
            else if(cthreadinfo.pairID > 0){
                printf("SERVER: You've already have a partner.\n");
                struct PACKET spacket;
                memset(&spacket, 0, sizeof(struct PACKET));
                strcpy(spacket.option, "msg");
                strcpy(spacket.nickname, packet.nickname);
                spacket.clientID = cthreadinfo.clientID;
                strcpy(spacket.buff, "CLIENT: You've already have a partner.");
                send(cthreadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);
            }
            // else, check if there's client available, if yes, find an available
            // client and assign it to this client. Else, do nothing.
            else{
                pthread_mutex_lock(&clientlist_mutex);
                for(curr = client_list.head; curr != NULL; curr = curr->next) {
                    if(curr->threadinfo.pairID == 0){      
                        if(!compare(&curr->threadinfo, &cthreadinfo)) continue;
                        flagbusy = FALSE;
                    }           
                }
                pthread_mutex_unlock(&clientlist_mutex); 
                // if there is no client available, do nothing
                if(flagbusy == TRUE){
                    printf("SERVER: There is no client available.\n");
                    struct PACKET spacket;
                    memset(&spacket, 0, sizeof(struct PACKET));
                    strcpy(spacket.option, "msg");
                    strcpy(spacket.nickname, cthreadinfo.nickname);
                    strcpy(spacket.buff, "CLIENT: There is no client available.");
                    spacket.clientID = cthreadinfo.clientID;
                    sent = send(cthreadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);                    
                }
                // if there is client available, assign availble client to this client
                else{
                    globalpairID++;
                    // set the this client's pairID to globalpairID
                    pthread_mutex_lock(&clientlist_mutex);
                    for(curr = client_list.head; curr != NULL; curr = curr->next) {
                        if(compare(&curr->threadinfo, &cthreadinfo) == 0){
                            curr->threadinfo.pairID = globalpairID;
                            cthreadinfo.pairID = globalpairID;
                        }
                    }
                    // find an availbe client, and set its pairID to globalpairID
                    pthread_mutex_unlock(&clientlist_mutex); 
                    pthread_mutex_lock(&clientlist_mutex);
                    for(curr = client_list.head; curr != NULL; curr = curr->next) {
                        if(curr->threadinfo.pairID == 0){// find an available client      
                            if(!compare(&curr->threadinfo, &cthreadinfo)) continue;
                            curr->threadinfo.pairID = globalpairID;
                            printf("SERVER: Sucessfullly find a partner.\n");
                            printf("SERVER: my clientID: %d, my nickname: %s, now my pairID: %d\n", 
                                        cthreadinfo.clientID, cthreadinfo.nickname, cthreadinfo.pairID);
                            printf("SERVER: my partner clientID: %d, my partner nickname: %s, now my partner pairID: %d\n", 
                                        curr->threadinfo.clientID, curr->threadinfo.nickname, curr->threadinfo.pairID);
                            // tell the client's partner that he/she has joined a chat channel
                            struct PACKET spacket;
                            memset(&spacket, 0, sizeof(struct PACKET));
                            strcpy(spacket.option, "msg");
                            strcpy(spacket.nickname, curr->threadinfo.nickname);
                            char partnername[NAMELENGTH];
                            strcpy(partnername, cthreadinfo.nickname);
                            char partnerbuff[LINEBUFF];
                            strcat(partnerbuff, "CLIENT: You've got a partner: ");
                            strcat(partnerbuff, partnername);
                            strcpy(spacket.buff, partnerbuff);
                            spacket.clientID = curr->threadinfo.clientID;
                            sent = send(curr->threadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);
                            // tell the client that server has assigned a new partner for him/her
                            struct PACKET cpacket;
                            memset(&cpacket, 0, sizeof(struct PACKET));
                            strcpy(cpacket.option, "msg");
                            cpacket.clientID = curr->threadinfo.clientID;                            
                            strcpy(cpacket.nickname, curr->threadinfo.nickname);
                            char myname[NAMELENGTH];
                            strcpy(myname, curr->threadinfo.nickname);
                            char mybuff[LINEBUFF];
                            strcat(mybuff, "CLIENT: You've got a partner: ");
                            strcat(mybuff, myname);
                            strcpy(cpacket.buff, mybuff);
                            send(cthreadinfo.sockfd, (void *)&cpacket, sizeof(struct PACKET), 0);
                            break;
                        }           
                    }
                    pthread_mutex_unlock(&clientlist_mutex); 
                }
            }
        }

   

        else if(!strcmp(packet.option, "whisp")) {
            struct THREADINFO wthreadinfo;
            pthread_mutex_lock(&clientlist_mutex);
            for(curr = client_list.head; curr != NULL; curr = curr->next) {
                if(curr->threadinfo.sockfd == threadinfo.sockfd){      
                    wthreadinfo = curr->threadinfo;
                }           
            }
            pthread_mutex_unlock(&clientlist_mutex); 
            if(wthreadinfo.pairID == -1){
                printf("SERVER: You are blocked.\n");
            }
            else if (wthreadinfo.pairID == 0){
                printf("SERVER: You need to call \"chat\" first to get a partner.\n");
            }
            // find the partner and whisper to the partner
            else{
                pthread_mutex_lock(&clientlist_mutex);
                for(curr = client_list.head; curr != NULL; curr = curr->next) {
                    if(wthreadinfo.pairID == curr->threadinfo.pairID) {// found the partner
                        if(!compare(&curr->threadinfo, &wthreadinfo)) continue;
                        printf("SERVER: Whisp to client NO. %d\n", curr->threadinfo.clientID);
                        struct PACKET spacket;
                        memset(&spacket, 0, sizeof(struct PACKET));
                        strcpy(spacket.option, "msg");
                        strcpy(spacket.nickname, packet.nickname);
                        strcpy(spacket.buff, packet.buff);
                        spacket.clientID = wthreadinfo.clientID;
                        sent = send(curr->threadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);
                    }
                }
                pthread_mutex_unlock(&clientlist_mutex);
            }
        }



        else if(!strcmp(packet.option, "flag")){
            struct THREADINFO fthreadinfo;
            pthread_mutex_lock(&clientlist_mutex);
            for(curr = client_list.head; curr != NULL; curr = curr->next) {
                if(curr->threadinfo.sockfd == threadinfo.sockfd){      
                    fthreadinfo = curr->threadinfo;
                }           
            }
            pthread_mutex_unlock(&clientlist_mutex); 
            if(fthreadinfo.pairID == -1){
                printf("SERVER: You are blocked.\n");
            }
            else if (fthreadinfo.pairID == 0){
                printf("SERVER: You need to call \"chat\" first to get a partner.\n");
                struct PACKET spacket;
                memset(&spacket, 0, sizeof(struct PACKET));
                strcpy(spacket.option, "msg");
                strcpy(spacket.nickname, fthreadinfo.nickname);
                strcpy(spacket.buff, "CLIENT: You need to call \"chat\" first to get a partner.");
                spacket.clientID = fthreadinfo.clientID;
                send(fthreadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);                
            }
            else{
                // find this client's partner and tell the partner
                pthread_mutex_lock(&clientlist_mutex);
                for(curr = client_list.head; curr != NULL; curr = curr->next) {
                    if(curr->threadinfo.pairID == fthreadinfo.pairID) {
                        if(!compare(&curr->threadinfo, &fthreadinfo)) continue;
                        flag[curr->threadinfo.clientID - 1]++;
                        printf("SERVER: Client NO. %d is flagged %d times.\n",curr->threadinfo.clientID, 
                                    flag[curr->threadinfo.clientID - 1]);
                        struct PACKET spacket;
                        memset(&spacket, 0, sizeof(struct PACKET));
                        strcpy(spacket.option, "msg");
                        strcpy(spacket.nickname, curr->threadinfo.nickname);
                        strcpy(spacket.buff, "CLIENT: Your partner reported you.");
                        spacket.clientID = fthreadinfo.clientID;
                        send(curr->threadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);
                        break;
                    }
                }
                pthread_mutex_unlock(&clientlist_mutex);
            }
        }        

        else if(!strcmp(packet.option, "transfer")){
				//Receive file from Client 1
				//printf("Get called dammit");
				char revbuf[512];
				char* fr_name = "temp.jpg";
				FILE *fr = fopen(fr_name, "a");
				if(fr == NULL)
				{
					printf("File does not exist creating a new one", fr_name);
					fr = fopen ("temp.jpg", "w+");
				}

					bzero(revbuf, LENGTH); 
					int fr_block_sz = 0;
					while((fr_block_sz = recv(newfd, revbuf, LENGTH, 0)) > 0) 
					{
						int write_sz = fwrite(revbuf, sizeof(char), fr_block_sz, fr);
						if(write_sz < fr_block_sz)
						{
							error("File write failed on server.\n");
						}
						bzero(revbuf, LENGTH);
						if (fr_block_sz == 0 || fr_block_sz != 512) 
						{
							break;
						}
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
							exit(1);
						}
        			}
					printf("Ok received from client!\n");
					fclose(fr); 

				/* Call the Script */
				//system("cd ; chmod +x script.sh ; ./script.sh");



/*			//Send file received from client 1 to client 2
				struct THREADINFO wthreadinfo;
				pthread_mutex_lock(&clientlist_mutex);
				for(curr = client_list.head; curr != NULL; curr = curr->next) {
					if(curr->threadinfo.sockfd == threadinfo.sockfd){      
						wthreadinfo = curr->threadinfo;
					}           
				}
				pthread_mutex_unlock(&clientlist_mutex); 
				if(wthreadinfo.pairID == -1){
					printf("SERVER: You are blocked.\n");
				}
				else if (wthreadinfo.pairID == 0){
					printf("SERVER: You need to call \"chat\" first to get a partner.\n");
				}
				// find the partner and whisper to the partner
				else{
							char* fs_name = "temp.jpg";
							char sdbuf[LENGTH]; // Send buffer
							printf("[Server] Sending %s to the Client...", fs_name);
							FILE *fs = fopen(fs_name, "r");
							if(fs == NULL)
							{
								fprintf(stderr, "ERROR: File %s not found on server. (errno = %d)\n", fs_name, errno);
								exit(1);
							}

							bzero(sdbuf, LENGTH); 
							int fs_block_sz; 
							while((fs_block_sz = fread(sdbuf, sizeof(char), LENGTH, fs))>0)
							{
								if(send(curr->threadinfo.sockfd, sdbuf, fs_block_sz, 0) < 0)
								{
									fprintf(stderr, "ERROR: Failed to send file %s. (errno = %d)\n", fs_name, errno);
									exit(1);
								}
								bzero(sdbuf, LENGTH);
							}
							printf("Ok sent to client!\n");
							close(curr->threadinfo.sockfd);
							printf("[Server] Connection with Client closed. Server will wait now...\n");
							while(waitpid(-1, NULL, WNOHANG) > 0);
							//sent = send(curr->threadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);

				}*/
		pthread_mutex_unlock(&clientlist_mutex);
		}


        /* end current chat channel */
        else if(!strcmp(packet.option, "quit")){
            struct THREADINFO qthreadinfo;
            pthread_mutex_lock(&clientlist_mutex);
            for(curr = client_list.head; curr != NULL; curr = curr->next) {
                if(curr->threadinfo.sockfd == threadinfo.sockfd){      
                    qthreadinfo = curr->threadinfo;
                }           
            }
            pthread_mutex_unlock(&clientlist_mutex);
            if(qthreadinfo.pairID == -1){
                printf("SERVER: You are blocked.\n");
            }
            else if (qthreadinfo.pairID == 0){
                printf("SERVER: You need to call \"chat\" first to get a partner.\n");
            }
            else{
                // find the client's partner and set its pairID to 0
                pthread_mutex_lock(&clientlist_mutex);
                for(curr = client_list.head; curr != NULL; curr = curr->next) {
                    if(curr->threadinfo.pairID == qthreadinfo.pairID) {
                        if(!compare(&curr->threadinfo, &qthreadinfo)) continue;
                        curr->threadinfo.pairID = 0;
                        struct PACKET spacket;
                        memset(&spacket, 0, sizeof(struct PACKET));
                        strcpy(spacket.option, "msg");
                        strcpy(spacket.nickname, curr->threadinfo.nickname);
                        strcpy(spacket.buff, "CLIENT: Your partner quit current channel.");
                        spacket.clientID = curr->threadinfo.clientID;
                        sent = send(curr->threadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);
                    }
                }
                pthread_mutex_unlock(&clientlist_mutex);
                // set the client itself pairID to 0
                pthread_mutex_lock(&clientlist_mutex);
                for(curr = client_list.head; curr != NULL; curr = curr->next) {
                    if(compare(&curr->threadinfo, &qthreadinfo) == 0) {
                        curr->threadinfo.pairID = 0;
                        qthreadinfo.pairID = 0;
                    }
                }
                pthread_mutex_unlock(&clientlist_mutex);
            }
        }

        else if(!strcmp(packet.option, "broadcast")) {
            pthread_mutex_lock(&clientlist_mutex);
            for(curr = client_list.head; curr != NULL; curr = curr->next) {
                struct PACKET spacket;
                memset(&spacket, 0, sizeof(struct PACKET));
                if(!compare(&curr->threadinfo, &threadinfo)) continue;
                strcpy(spacket.option, "msg");
                strcpy(spacket.nickname, packet.nickname);
                strcpy(spacket.buff, packet.buff);
                sent = send(curr->threadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);
            }
            pthread_mutex_unlock(&clientlist_mutex);
        }  

        else if(!strcmp(packet.option, "exit")) {
            printf("SERVER: [%d] %s has disconnected...\n", threadinfo.sockfd, threadinfo.nickname);
            pthread_mutex_lock(&clientlist_mutex);
            list_delete(&client_list, &threadinfo);
            pthread_mutex_unlock(&clientlist_mutex);
            break;
        }

        else {
            fprintf(stderr, "SERVER: Garbage data from [%d] %s...\n", threadinfo.sockfd, threadinfo.nickname);
        }
        
    }
    /* clean up */
    close(threadinfo.sockfd);     
    return NULL;
}


void block(char *ptr) {
    struct LLNODE *curr;
    struct THREADINFO bthreadinfo;
    BOOL flagID = FALSE;// flagID is TRUE means this clientID is legal, FALSE means there is no such client.
    int i;

    if(ptr == NULL) {
        return;
    }
    i = atoi(ptr);
    // ~~~~~~~~~~~~~~~~~~check if this clientID is legal~~~~~~~~~~~~~~~~~~
    // if this client is legal(it's in the list), set flagID to TRUE
    pthread_mutex_lock(&clientlist_mutex);
    for(curr = client_list.head; curr != NULL; curr = curr->next) {
        if(curr->threadinfo.clientID == i){      
            flagID = TRUE;
            bthreadinfo = curr->threadinfo;
        }           
    }
    pthread_mutex_unlock(&clientlist_mutex); 
    // ~~~~~~~~~~~~~~~~~~~if clientID is not legal~~~~~~~~~~~~~~~~~~~~~~~~
    if(flagID == FALSE){
        printf("SERVER: Cannot find clientID: %d\n", i);
        return;
    }
    // ~~~~~~~~~~~~~~~~~~~~~~~if clientID is legal~~~~~~~~~~~~~~~~~~~~~~~~~
    // if this client is already blocked
    if(bthreadinfo.pairID == -1){
        printf("This client is already blocked.\n");
        return;
    }
    // if this client doesn't have a partner
    if(bthreadinfo.pairID == 0){
        pthread_mutex_lock(&clientlist_mutex);
        for(curr = client_list.head; curr != NULL; curr = curr->next){
            if(!compare(&curr->threadinfo, &bthreadinfo)) {
                curr->threadinfo.pairID = -1;
                bthreadinfo.pairID = -1;
                printf("SERVER: This client is blocked sucessfullly.\n");
                struct PACKET bpacket;
                memset(&bpacket, 0, sizeof(struct PACKET));
                strcpy(bpacket.option, "msg");
                strcpy(bpacket.nickname, curr->threadinfo.nickname);
                strcpy(bpacket.buff, "CLIENT: You're blocked.");
                send(curr->threadinfo.sockfd, (void *)&bpacket, sizeof(struct PACKET), 0);
            }
        }
        pthread_mutex_unlock(&clientlist_mutex); 
    }
    // if this client has a partner
    else{
        // set partner's pairID to 0
        pthread_mutex_lock(&clientlist_mutex);
        for(curr = client_list.head; curr != NULL; curr = curr->next){
            if(curr->threadinfo.pairID == bthreadinfo.pairID) {
                if(!compare(&curr->threadinfo, &bthreadinfo)) continue;
                curr->threadinfo.pairID = 0;
                struct PACKET bpacket;
                memset(&bpacket, 0, sizeof(struct PACKET));
                strcpy(bpacket.option, "msg");
                strcpy(bpacket.nickname, curr->threadinfo.nickname);
                strcpy(bpacket.buff, "CLIENT: Your partner is blocked. Current channel is ended.");
                send(curr->threadinfo.sockfd, (void *)&bpacket, sizeof(struct PACKET), 0);
            }
        }
        pthread_mutex_unlock(&clientlist_mutex); 
        // set itself pairID to -1
        pthread_mutex_lock(&clientlist_mutex);
        for(curr = client_list.head; curr != NULL; curr = curr->next){
            if(!compare(&curr->threadinfo, &bthreadinfo)) {
                curr->threadinfo.pairID = -1;
                bthreadinfo.pairID = -1;
                struct PACKET bpacket;
                memset(&bpacket, 0, sizeof(struct PACKET));
                strcpy(bpacket.option, "msg");
                strcpy(bpacket.nickname, curr->threadinfo.nickname);
                strcpy(bpacket.buff, "CLIENT: You're blocked.");
                send(curr->threadinfo.sockfd, (void *)&bpacket, sizeof(struct PACKET), 0);
            }
        }
        pthread_mutex_unlock(&clientlist_mutex);
        printf("SERVER: This client is blocked sucessfullly. And ended current channel.\n");
    }
}


void unblock(char *ptr){
    struct LLNODE *curr;
    struct THREADINFO bthreadinfo;
    BOOL flagID = FALSE;// flagID is TRUE means this clientID is legal, FALSE means there is no such client.
    int i;

    if(ptr == NULL) {
        return;
    }
    i = atoi(ptr);
    // ~~~~~~~~~~~~~~~~~~check if this clientID is legal~~~~~~~~~~~~~~~~~~
    // if this client is legal(it's in the list), set flagID to TRUE
    pthread_mutex_lock(&clientlist_mutex);
    for(curr = client_list.head; curr != NULL; curr = curr->next) {
        if(curr->threadinfo.clientID == i){      
            flagID = TRUE;
            bthreadinfo = curr->threadinfo;
        }           
    }
    pthread_mutex_unlock(&clientlist_mutex); 
    // ~~~~~~~~~~~~~~~~~~~if clientID is not legal~~~~~~~~~~~~~~~~~~~~~~~~
    if(flagID == FALSE){
        printf("SERVER: Cannot find clientID: %d\n", i);
        return;
    }
    // ~~~~~~~~~~~~~~~~~~~~~~~if clientID is legal~~~~~~~~~~~~~~~~~~~~~~~~~
    // if this client is blocked, set its pairID to 0
    if(bthreadinfo.pairID == -1){
        pthread_mutex_lock(&clientlist_mutex);
        for(curr = client_list.head; curr != NULL; curr = curr->next){
            if(!compare(&curr->threadinfo, &bthreadinfo)) {
                curr->threadinfo.pairID = 0;
                bthreadinfo.pairID = 0;
                printf("SERVER: This client is unblocked sucessfullly.\n");
                struct PACKET bpacket;
                memset(&bpacket, 0, sizeof(struct PACKET));
                strcpy(bpacket.option, "msg");
                strcpy(bpacket.nickname, curr->threadinfo.nickname);
                strcpy(bpacket.buff, "CLIENT: You're unblocked.");
                send(curr->threadinfo.sockfd, (void *)&bpacket, sizeof(struct PACKET), 0);
            }
        }
        pthread_mutex_unlock(&clientlist_mutex); 
    }
    // if this client is not blocked, do nothing
    else{
        printf("SERVER: This client is not blocked.\n");
        return;
    }
}

void throwout(char *ptr){
    struct LLNODE *curr;
    struct THREADINFO tthreadinfo;
    BOOL flagID = FALSE;// flagID is TRUE means this clientID is legal, FALSE means there is no such client.
    int i;

    if(ptr == NULL) {
        return;
    }
    i = atoi(ptr);
    // ~~~~~~~~~~~~~~~~~check if this clientID is legal~~~~~~~~~~~~~~~~~~~
    // if this client is legal(it's in the list), set flagID to TRUE
    pthread_mutex_lock(&clientlist_mutex);
    for(curr = client_list.head; curr != NULL; curr = curr->next) {
        if(curr->threadinfo.clientID == i){      
            flagID = TRUE;
            tthreadinfo = curr->threadinfo;
        }           
    }
    pthread_mutex_unlock(&clientlist_mutex); 
    // ~~~~~~~~~~~~~~~~~~~~~~if clientID is not legal~~~~~~~~~~~~~~~~~~~~~~
    if(flagID == FALSE){
        printf("SERVER: Cannot find clientID: %d\n", i);
        return;
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~if clientID is legal~~~~~~~~~~~~~~~~~~~~~~~~~
    // if this client is doesn't have a partner
    if(tthreadinfo.pairID == -1 || tthreadinfo.pairID == 0){
        pthread_mutex_lock(&clientlist_mutex);
        for(curr = client_list.head; curr != NULL; curr = curr->next) {
            if(curr->threadinfo.clientID == i){// find the client that needs to be throwout
                list_delete(&client_list, &(curr->threadinfo));
                printf("SERVER: Client NO. %d is thrown out.\n", i);
                break;
            }            
        }
        pthread_mutex_unlock(&clientlist_mutex);
    }
    // if this client has a partner
    else{
        // set partner's pairID to 0
        pthread_mutex_lock(&clientlist_mutex);
        for(curr = client_list.head; curr != NULL; curr = curr->next){
            if(curr->threadinfo.pairID == tthreadinfo.pairID) {
                if(!compare(&curr->threadinfo, &tthreadinfo)) continue;
                curr->threadinfo.pairID = 0;
                struct PACKET packet;
                memset(&packet, 0, sizeof(struct PACKET));
                strcpy(packet.option, "msg");
                strcpy(packet.nickname, curr->threadinfo.nickname);
                strcpy(packet.buff, "CLIENT: Your partner is thrown out. Current channel is ended.");
                send(curr->threadinfo.sockfd, (void *)&packet, sizeof(struct PACKET), 0);
            }
        }
        pthread_mutex_unlock(&clientlist_mutex); 
        // throw out itself
        pthread_mutex_lock(&clientlist_mutex);
        for(curr = client_list.head; curr != NULL; curr = curr->next){
            if(!compare(&curr->threadinfo, &tthreadinfo)) {
                curr->threadinfo.pairID = 0;
                tthreadinfo.pairID = 0;
                struct PACKET packet;
                memset(&packet, 0, sizeof(struct PACKET));
                strcpy(packet.option, "msg");
                strcpy(packet.nickname, curr->threadinfo.nickname);
                strcpy(packet.buff, "CLIENT: You are thrown out. Current channel is ended.");
                send(curr->threadinfo.sockfd, (void *)&packet, sizeof(struct PACKET), 0);                
                printf("SERVER: Client NO. %d is thrown out. Current channel is ended.\n", i);
            }
        }
        pthread_mutex_unlock(&clientlist_mutex);
    }
}