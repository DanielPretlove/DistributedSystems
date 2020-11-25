#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <netdb.h> 
#include <string.h>
#include <arpa/inet.h> 
#include <sys/types.h>
#include <sys/socket.h> 
#include <netinet/in.h>  
#include <unistd.h> 
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <pthread.h> 


#define CHANNELID_MAX 255 /* highest amount of channel ids we can give out */
#define DEFAULT_PORT 12345
#define ARRAY_SIZE 40
#define MAX_DATA_SIZE 1027 /* max number of bytes we can get and send at once */
#define MAX_CLIENTS 50
#define MAX_Threads 10

unsigned char inbuffer[MAX_DATA_SIZE] = {0}; //can store 1024 values between 0-255 buffer for data coming in.
unsigned char outbuffer[MAX_DATA_SIZE] = {0}; //can store 1024 values between 0-255 buffer for data going out.
unsigned char inputbuffer[MAX_DATA_SIZE] = {0}; //Space for user input..
int channel[256] = {0}; //tracks subscription
int running = 1; //Boolean for main while loop, BYE command changes
int clientid; //ID used to communicate with server

//Thread variables
pthread_t tid[MAX_Threads]; //tid for each thread
int tid_index[MAX_Threads] = {0,1,2,3,4,5,6,7,8,9}; //index for each thread, should count up to MAX_Threads-1.
bool tid_active[MAX_Threads] = {false}; //determine if tid is available.
int thread_channelid[MAX_Threads] = {0}; //store channel id being used byy each thread.
bool Stop = false; //Stops LIVEFEEDS
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int sockfd, new_fd, port, id;  

void ServerDisconnectCheck() {
	if (inbuffer[0] == 250) //if disconnect code is found in the char buffer,
	{
		printf("\nServer has disconnected this client.\n");
		bzero(outbuffer, 1027);
		outbuffer[0] = 250; //exit code.
		new_fd = write(sockfd, outbuffer, strlen(outbuffer));
		if(new_fd < 0)
		{
			perror("Writing message to the server failed.");
		}
		sleep(2);
		printf("\nExiting...\n");
		running = 0;
		Stop = true;
		exit(EXIT_SUCCESS);
	}
}

void Server_Listen(){

	int n; //Size of what's currently in inbuffer.
	clock_t start_time = clock();
	int response_time = 1000 * 5; //Replace 5 with the amount of seconds it should wait.
	bzero(inbuffer, MAX_DATA_SIZE);
	while(((n = read(sockfd, inbuffer, MAX_DATA_SIZE)) > 0) && (clock() < start_time+response_time)) //Read buffer for response_time milliseconds.
	{
		ServerDisconnectCheck();
		printf("\t%s\n", inbuffer);
		bzero(inbuffer, MAX_DATA_SIZE);
		break;
	}
}

void subscribe(int i){
	if(i > 255 || i < 0){
		printf("Invalid Channel: %d\n", i);
	}
	else{
		if(channel[i] == 1){printf("Already subscribed to channel %d\n",i);}
		else{
			//printf("Subscribed to channel %d\n",i); //specification specifies that the server messages the client this, might have to change.
			bzero(outbuffer, MAX_DATA_SIZE); //clear the buffer so commands can be input
			
			unsigned char temp = clientid;
			outbuffer[0] = temp; // client id
			outbuffer[1] = 1; //subscribe will be command number 1.
			outbuffer[2] = i; //channel number
			new_fd = write(sockfd, outbuffer, strlen(outbuffer));
			if(new_fd < 0)
			{
				perror("Writing message to the server failed.");
				//return 0;
			}
			//SERVER WILL NOTIFY THE CLIENT THEY ARE SUBSCRIBED.
			channel[i] = 1;
			Server_Listen();
		}
	}
}

void unsubscribe(int i){
	if(i > 255 || i < 0)
	{
		printf("Invalid Channel: %d\n", i);
	}
	else
	{
		if(channel[i] == 0){printf("You were not subscribed to channel %d \n",i);}
		else{	

			bzero(outbuffer, MAX_DATA_SIZE); //clear the buffer so commands can be input
			unsigned char temp = clientid;
			outbuffer[0] = temp; // client id
			outbuffer[1] = 3; //UNSUB will be command number 3.
			outbuffer[2] = i; //channel number to unsub from

			new_fd = write(sockfd, outbuffer, strlen(outbuffer));
			if(new_fd < 0)
			{
				perror("Writing message to the server failed.");
				//return 0;
			}
			channel[i] = 0;
			Server_Listen();
		}
	}
}

void Next(int i){
	//Receive Message from channel
	bzero(outbuffer, MAX_DATA_SIZE); //clear the buffer so commands can be input
	unsigned char temp = clientid;
	outbuffer[0] = temp; // client id
	outbuffer[1] = 4; //NEXT will be command number 4.
	outbuffer[2] = i; //channel number to get message from
	if(i == -1 ){outbuffer[2] = 'A';}
	new_fd = write(sockfd, outbuffer, strlen(outbuffer));
	if(new_fd < 0){perror("Writing message to the server failed.");}
	sleep(3);
	Server_Listen();
}

int Tid(){
	for (int i = 0; i < MAX_Threads; i++)
	{
		if (tid_active[i] == false)
		{
			tid_active[i] = true;
			return i;
		}
	}
	return -1; //ERROR to be handled
}

void* Next_Thread(void *idx){
	int index = *(int *) idx;
	int i = thread_channelid[index];
	if(i > 255 || i < -1){
		printf("Invalid Channel: %d\n", i);
	}
	else{
		if(channel[i] == 0 && i!=-1){
			printf("Not subscribed to channel %d\n",i);
			channel[i] = 0;
		}
		else{
			//Receive Message from channel
			pthread_mutex_lock(&mutex);
			bzero(outbuffer, MAX_DATA_SIZE); //clear the buffer so commands can be input
			unsigned char temp = clientid;
			outbuffer[0] = temp; // client id
			outbuffer[1] = 4; //NEXT will be command number 4.
			outbuffer[2] = i; //channel number to get message from
			if(i == -1 ){outbuffer[2] = 'A';}
			new_fd = write(sockfd, outbuffer, strlen(outbuffer));
			if(new_fd < 0){perror("Writing message to the server failed.");}
			int n; //Size of what's currently in inbuffer.
			bzero(inbuffer, MAX_DATA_SIZE);
			sleep(3);
			Server_Listen();
			pthread_mutex_unlock(&mutex);
		}
	}
	tid_active[index] = false;
	pthread_exit(NULL);
}

void* Livefeed_Thread(void *idx){
	int index = *(int *) idx;
	int i = thread_channelid[index];
	if(i > 255 || i < -1){
		printf("Invalid Channel: %d\n", i);
	}
	else{
		if(channel[i] == 0 && i!=-1){
			printf("Not subscribed to channel %d\n",i);
			channel[i] = 0;
		}
		else{
			while(!Stop){
				sleep(2);
				pthread_mutex_lock(&mutex);
				Next(i);
				pthread_mutex_unlock(&mutex);
			}
		}
	}
	tid_active[index] = false;
	pthread_exit(NULL);
}

void handle_command(char str[]){
	
	char delim[] = " \n"; //Set delimiter for breaking up words from command.
	char *ptr = strtok(str,delim); // Pointer to first word.

	char* command = ptr; // Assign first word as command string.
	ptr = strtok(NULL,delim); // Move token to next word.
	char* param = ptr; //Assign channel value as param string.
	// If statements to determine command
	if(strcmp("SUB", command) == 0)
	{
		if(param != NULL){subscribe(atoi(param));} // If channel input exists.
		else{printf("Please select a channel.\n");}
		return;
	}
	else if(strcmp("CHANNELS", command) == 0)
	{
		if(param != NULL){
			printf("CHANNELS command requires no additional input.\n");
			return;
		}
		// Notify server of command
		bzero(outbuffer, MAX_DATA_SIZE); //clear the buffer so commands can be input
		unsigned char temp = clientid;
		outbuffer[0] = temp; // client id
		outbuffer[1] = 2; //Channels will be command number 2.
		outbuffer[2] = 0; //channel doesn't matter
		new_fd = write(sockfd, outbuffer, strlen(outbuffer));
		if(new_fd < 0)
		{
			perror("Writing message to the server failed.");
		}
		
		//Receive messages from server once for each subbed channel.
		Server_Listen(); //Channels command message.
		for(int i = 0; i <= CHANNELID_MAX; i++){
			if (channel[i] == true){Server_Listen();};
		}
	}
	else if(strcmp("UNSUB", command) == 0)
	{
		if(param != NULL){unsubscribe(atoi(param));} // If channel input exists.
		else{printf("Please select a channel.\n");}
	}
	else if(strcmp("NEXT", command) == 0)
	{
		int id = Tid(); // Determine the first available thread id.
		if(id != -1){ // If thread id is returned.
			if(param == NULL){thread_channelid[id] = -1;}
			else{thread_channelid[id] = atoi(param);}
			Stop = false;
			pthread_create(&tid[id], NULL, Next_Thread, &tid_index[id]); // Create a thread to run NEXT command.
		}
		else{printf("Max threads currently active.\n");}
	}
	else if(strcmp("LIVEFEED", command) == 0)
	{
		int id = Tid(); // Determine the first available thread id.
		if(id != -1){ // If thread id is returned.
			if(param == NULL){thread_channelid[id] = -1;}
			else{thread_channelid[id] = atoi(param);}
			Stop = false;
			pthread_create(&tid[id], NULL, Livefeed_Thread, &tid_index[id]); // Create a thread to run NEXT command.
		}
		else{printf("Max threads currently active.\n");}
	}
	else if(strcmp("SEND", command) == 0)
	{
		if(param == NULL){ // If no channel is selected.
			printf("Please select a channel.\n");
			return;
		}
		if(atoi(param) > 255 || atoi(param) < -1){ // If channel is invalid.
			printf("SEND requires a valid channel\n");
			return;
		}
		outbuffer[2] = atoi(param); //Adding channel number to send to to buffer.
		
		// Create message string.
		ptr = strtok(NULL, delim);
		char *message = malloc(MAX_DATA_SIZE);
		if(ptr != NULL){strcat(message,ptr);}
		else{ // If no message exists.
			printf("Please submit a message with SEND command.\n");
			return;
		}
		ptr = strtok(NULL, delim);
		while(ptr != NULL){
			strcat(message," ");
			strcat(message,ptr);
			ptr = strtok(NULL, delim);
		}
		
		unsigned char temp = clientid;
		outbuffer[0] = temp; //Client ID
		outbuffer[1] = 6; //SEND will be command number 6.

		for(int i = 1; i < strlen(message)+1; i++) //FEED MESSAGE INTO BUFFER...
		{
			outbuffer[2 + i] = message[i - 1]; //START FROM OUTBUFFER 3
		}
		printf("Sent message '%s' to server.\n",message);
		//Send message to server
		new_fd = write(sockfd, outbuffer, strlen(outbuffer));
		if(new_fd < 0)
		{
			perror("Writing message to the server failed.");
		}

		bzero(outbuffer, MAX_DATA_SIZE);
		bzero(message, MAX_DATA_SIZE);
	}
	else if(strcmp("BYE", command) == 0)
	{
		//Notify server of disconnect
		unsigned char temp = clientid;
		outbuffer[0] = temp; // client id
		outbuffer[1] = 7; //bye will be command number 7.
		outbuffer[2] = 0; //channel doesn't matter
		//Send Message
		new_fd = write(sockfd, outbuffer, strlen(outbuffer));
		if(new_fd < 0)
		{
			perror("Writing message to the server failed.");
			return;
		}
		//Receive Disconnect from Server
		running = 0;
		Stop = true;
		sleep(2);
		Server_Listen();
		return;
	}
	else if(strcmp("STOP", command) == 0)
	{
		//Stop running threads.
		Stop = true;
		return;
	}
	else{
		printf("Command '%s' does not exist\n",command);
		return;
	}
}



int main(int argc, char *argv[]) {
	
	//****Server Connection Code****
	time_t t;
	struct hostent *he;
	struct sockaddr_in server; // Connector's address information

	srand((unsigned) time(&t));

	if (argc != 3) // Must be an IP AND Port specified.
    {
        fprintf (stderr, "Usage: %s <IP> <port>\n", argv[0]);
        exit (1);
    }
	else if(argc > 2)
	{
		port = atoi(argv[2]);
	}
	else 
	{
		port = DEFAULT_PORT;
	}

	if ((he=gethostbyname(argv[1])) == NULL) {  /* get the host info */
		herror("gethostbyname");
		exit(1);
	}

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	server.sin_family = AF_INET;      /* host byte order */
	server.sin_port = htons(port);    /* short, network byte order */
	server.sin_addr.s_addr = INADDR_ANY;
	bzero(&(server.sin_zero), 8);     /* zero the rest of the struct */

	new_fd = inet_pton(AF_INET, argv[1], &server.sin_addr);
	if (new_fd < 0)
	{
		perror("IP was not successfully setup on the client");
		return 0;
	}

	//Connect to the server using the setup info
	new_fd = connect(sockfd, (struct sockaddr *)&server, sizeof(struct sockaddr));
	if (new_fd < 0)
	{
		perror("Connecting to the server failed on the client");
		return 0;
	}
	
	bzero(inbuffer, MAX_DATA_SIZE); //zero out the buffer
	read(sockfd, &clientid, sizeof(int)); //Read in the client ID

	// Display group details
	printf("Welcome! Your client ID is %d", clientid);
	printf("\n---------------------------------------------------\n");
	printf("Process Management and Distributing Computing Server: %d\n\n", port);
	printf("Created by Daniel Pretlove: N10193308\n");
	printf("           Justin Vickers:   N9705872\n");
	printf("           Jordan Auld:     N10227750\n");
	printf("---------------------------------------------------\n");
	//****Server Connection Code****
	
	//Main loop
	while(running == 1) //main loop
	{
		//Input Command
		printf("Please issue a command: \t");
		bzero(outbuffer, MAX_DATA_SIZE); //Zero entire buffer
		bzero(inputbuffer, MAX_DATA_SIZE); //Zero entire buffer
		fgets(inputbuffer, MAX_DATA_SIZE, stdin); //Get input
		//Server_Listen();
		if(inputbuffer[0] != '\n'){ //If input is not just enter then run command.
			handle_command(inputbuffer); //MAIN COMMAND HANDLER
		}
	}
	close(sockfd);
  	return 0; 
}