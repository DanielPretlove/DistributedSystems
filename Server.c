#include <stdio.h> 
#include <stdlib.h> 
#include <stdint.h> 
#include <stdbool.h> 
#include <string.h> 
#include <sys/types.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <sys/wait.h>   
#include <unistd.h> 
#include <ctype.h>
#include <time.h>
#include <pthread.h> 
#include <arpa/inet.h>
#include <errno.h>

#define CHANNELID_MAX 255 /* highest amount of channel ids we can give out */
#define DEFAULT_PORT 12345
#define BACKLOG 5
#define ARRAY_SIZE 40
#define MAX_DATA_SIZE 1027 /* max number of bytes we can get at once */ //TODO: 1024 to be updated to corresponding with a string of 1024 bits.
#define MAX_CLIENTS 10 //Set to 10 because blackboard clarifies this is high enough
#define MAX_MESSAGES 1000

//For storing and reading messages
char messages[CHANNELID_MAX][MAX_MESSAGES][1024] = {'\0'}; //Messages address to be updated by server.
bool message_written[CHANNELID_MAX][MAX_MESSAGES] = {false}; //determines whether there is a message present for an index.
int message_Index[CHANNELID_MAX] = {0}; //Index for the next message to be added for each channel.
int message_next[CHANNELID_MAX] = {0}; //Index of next message to be read.

//For CHANNELS command
int readmsgs[CHANNELID_MAX] = {0}; //Tracks total read messages from each channel.
int remaining[CHANNELID_MAX] = {0}; //Track yet to be read messages from each channel.

//For ALL channels NEXT command
int message_next_all_channel[MAX_MESSAGES] = {0}; //Stores the next channel that the all NEXT command should use message should be.
int next_index = 0; //Index for what is to be read next from 'next_all' arrays.
int add_index = 0; //Index for where to add next value in 'next_all' arrays.
bool message_read[CHANNELID_MAX][MAX_MESSAGES] = {false}; //determine if message should be skipped.

//Client variables
bool subbed_clients[CHANNELID_MAX] = {false}; //Stores clients subscribed and to what channel number
//For example, If a client 1 is subbed at channel 5    subscribed[1][5] = true;
bool ActiveClientIDs[MAX_CLIENTS] = {false}; //if a Client connects to the server it's ID will be stored in here
int sockfd, new_fd; 

// Enum for determining command sent by client
typedef enum
{
	SUB = 1, 
	CHANNELS = 2, 
	UNSUB = 3, 
	NEXT = 4, 
	LIVEFEED = 5, 
	SEND = 6, 
	BYE = 7
} CommandType; 

/* Returns a integer value of the lowest free client ID
Client should inform server when disconnecting to free up it's client ID from thed array
Should also have a way to find clients that have crashed or forced closed and free their IDs.
*/
int ClientIDReturner()
{
	for (int i = 1; i < MAX_CLIENTS+1; i++)
	{
		if (ActiveClientIDs[i] == false)
		{
			ActiveClientIDs[i] = true;
			return i;
		}
	}
	return -1; //ERROR to be handled
}

// Ctrl-C close command
void sigintHandler(int signal)
{
  if (signal == SIGINT)
  {
	printf("\nReceived SIGINT\n");
	printf("\nRemoving clients...\n");

	//unsigned char exitstr[6] = "DISCON";
	unsigned char str[1027] = {0};
	unsigned char rcv[1027] = {0};
	str[0] = 250;
	

	while (rcv[0] != 250)
	{
		write(new_fd, str, sizeof(str));
		printf("Waiting for clients to disconnect.\n");
		read(new_fd, rcv, sizeof(rcv));
		//printf("rcv 0 != 250...");
	}
	if (rcv[0] == 250)
	{
		close(sockfd);
		close(new_fd);
		sleep(2);
		printf("\nServer Closing.\n");
		exit(EXIT_SUCCESS);
	}
  }
}

void add_message(int channelid, char buffer[1027])
{
	char str[1024] = {0}; //read buffer message into here.
	for (int i = 1; i < strlen(buffer) - 2; i++)
	{
		str[i - 1] = buffer[i + 2]; //This should read the message of 1024 bytes into the str char variable.
	}

	printf("Message '%s' into channel %d\n", str, channelid);

	sprintf(messages[channelid][message_Index[channelid]], "%s",str); // Add message to string array.
	message_written[channelid][message_Index[channelid]] = true; // say message is written to.
	if(subbed_clients[channelid] == true){remaining[channelid]++;} //Increment messages a client needs to read from that channel
	else{message_read[channelid][message_Index[channelid]] = true;}
	message_Index[channelid]++; //
	
	//For all channels variables
	message_next_all_channel[add_index] = channelid; //Stores for each the next channel that the message should be.
	add_index++;
	
	bzero(buffer, 1027);
	bzero(str, 1024);
}

void next_message(int channelid)
{
	//use channelid = -1 to run all channels
	unsigned char outbuffer[MAX_DATA_SIZE];//create a buffer of the maximum size for data going out
	if(channelid == -1){ //Running for all channels
		if(message_next_all_channel[next_index] == -1){
			sprintf(outbuffer, "ALL: There are no more messages at this time...\n");
			printf("There was no message to be recieved from any channel.\n");
			write(new_fd, outbuffer, sizeof(outbuffer));
			bzero(outbuffer, MAX_DATA_SIZE);
			return;
		}
		if(message_read[message_next_all_channel[next_index]][message_next[message_next_all_channel[next_index]]]){
			next_index++;
			next_message(-1);
			return;
		}
		if(messages[message_next_all_channel[next_index]][message_next[message_next_all_channel[next_index]]][0] == '\0'){ // If there is no message next.
			sprintf(outbuffer, "ALL: There is no more messages at this time...\n");
			printf("There was no message to be recieved from any channel.\n"); //No messages the client hasn't seen.
		}
		else{
			sprintf(outbuffer, "%d: %s", message_next_all_channel[next_index], messages[message_next_all_channel[next_index]][message_next[message_next_all_channel[next_index]]]); //This may not work properly
			message_read[message_next_all_channel[next_index]][message_next[message_next_all_channel[next_index]]] = true;
			message_next[message_next_all_channel[next_index]]++;
			readmsgs[message_next_all_channel[next_index]]++;
			remaining[message_next_all_channel[next_index]]--;
			printf("Sending '%s' to channel #%d.\n", outbuffer, message_next_all_channel[next_index]);
			next_index++;
		}
	}
	else{ //Specified Channel
		if(message_read[channelid][message_next[channelid]]){
			printf("message not already read or is unavailable to this client.\n");
			message_next[channelid]++;
			next_message(channelid);
			return;
		}
		if(messages[channelid][message_next[channelid]][0] == '\0'){ // If there is no message next.
			sprintf(outbuffer, "%d: There is no more messages at this time...\n", channelid);
			printf("There was no message to be recieved from channel %d.\n",channelid); //No messages the client hasn't seen.
		}
		else{
			sprintf(outbuffer, "%d: %s", channelid, messages[channelid][message_next[channelid]]); //This may not work properly
			message_read[channelid][message_next[channelid]] = true;
			message_next[channelid]++;
			readmsgs[channelid]++;
			remaining[channelid]--;
			printf("Sending '%s' to channel #%d.\n",outbuffer,channelid);
		}
	}
	write(new_fd, outbuffer, sizeof(outbuffer));
	bzero(outbuffer, MAX_DATA_SIZE);
}

int main(int argc, char* argv[])
{
	int port, id;
	struct sockaddr_in my_address;
	struct sockaddr_in their_address;

	unsigned char inbuffer[MAX_DATA_SIZE];//create a buffer of the maximum size for data coming in
	unsigned char outbuffer[MAX_DATA_SIZE];//create a buffer of the maximum size for data going out
	socklen_t sin_size;

	
	if (argc > 1) //If the user provides a port
	{
		port = atoi(argv[1]);
	}
	else //use default port if user does not provide a port
	{
		port = DEFAULT_PORT;
	}

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		exit(EXIT_SUCCESS);
	}

	my_address.sin_family = AF_INET;
	my_address.sin_port = htons(port);
	my_address.sin_addr.s_addr = INADDR_ANY;


	if (bind(sockfd, (struct sockaddr*) & my_address, sizeof(struct sockaddr)) == -1)
	{
		perror("bind error");
		exit(EXIT_SUCCESS);
	}


	printf("---------------------------------------------------\n");
	printf("Process Management and Distributing Computing Server listening on port: %d\n\n", port);
	printf("Created by Daniel Pretlove: N10193308\n");
	printf("           Justin Vickers:   N9705872\n");
	printf("           Jordan Auld:     N10227750\n");
	printf("---------------------------------------------------\n");
	printf("server starts listening...\n\n");

	listen(sockfd, BACKLOG);

	while (new_fd = accept(sockfd, (struct sockaddr*)NULL, NULL))
	{
		int childpid, n;
		sin_size = sizeof(struct sockaddr_in); //What is sin_size?

		//SEND ID TO THE CLIENT.
		int tempidstore = ClientIDReturner();
		printf("Server sending id: %d\n", tempidstore); //TESTING REMOVE LATER
		//htonl(tempidstore); //converting to network byte order
		if (write(new_fd, &tempidstore, sizeof(tempidstore)) == -1)
		{
			perror("Sending id error");
		}

		//if ((childpid = fork()) == 0) //Split off a new child process which is PID 0...
		//{
			//Server recieves connection from this IP ADDRESS
			//printf("Server: got connection from %s\n", inet_ntoa(their_address.sin_addr));

			close(sockfd); //close listening socket.

			bzero(inbuffer, MAX_DATA_SIZE); //zero out the buffer


			if (signal(SIGINT, sigintHandler) == SIG_ERR)	printf("\nCouldn't catch sigint\n");
			

			//Main While loop only progresses while there is something to accept in the buffer...
			while ((n = read(new_fd, inbuffer, MAX_DATA_SIZE)) > 0) //This needs to be changed... 
			{
				//printf("Server Recieved: %c", inbuffer); //Test code

				int clientid = inbuffer[0];
				int command = inbuffer[1];
				int channelnum = inbuffer[2];
				if(inbuffer[2] == 'A'){channelnum = -1;}
				//printf("Channel: %c\n",inbuffer[2]);
				//printf("str len: %ld\n",strlen(inbuffer));

				int SubCount; //For channels command

				switch(command)
				{
					case SUB: //SUB
						subbed_clients[channelnum] = true;
						printf("Client %d subscribed to channel %d \n", clientid, channelnum);
						bzero(outbuffer, MAX_DATA_SIZE);
						//Tell the client they have subscribed successfully.
						sprintf(outbuffer, "Successfully subscribed to channel %d \n", channelnum);
						write(new_fd, outbuffer, sizeof(outbuffer));
						bzero(inbuffer, MAX_DATA_SIZE);
						bzero(outbuffer, MAX_DATA_SIZE);
						break;
						
					case CHANNELS: //CHANNELS
						//Make sure client has at least one subscription.
						SubCount = 0;
						for(int i = 0; i <= CHANNELID_MAX; i++){
							if (subbed_clients[i] == true){SubCount += 1;};
						}
						//If client is not subscribed to anyone.
						if(SubCount == 0){
							sprintf(outbuffer,"You are not subscribed to any channels currently.\n");
							write(new_fd, outbuffer, sizeof(outbuffer));
							bzero(outbuffer, MAX_DATA_SIZE);
						}
						//If client is subscribed to at least one channel.
						else{
							sprintf(outbuffer,"You are subscribed to the following channels...\n\n");
							write(new_fd, outbuffer, sizeof(outbuffer));
							bzero(outbuffer, MAX_DATA_SIZE);
							for (int i = 0; i < 256; i++)
							{
								if (subbed_clients[i] == true) //check every channel for this clientid
								{
									printf("clientid %d is subscribed to: %d\n", clientid, i);
									//send this to client, including the messages remaining etc etc etc
									sprintf(outbuffer,"Channel: %d,  Total message: %d, Messages read: %d, Yet to read: %d\n",
									i,
									message_Index[i]+1,/*plus one because index starts at zero.*/
									readmsgs[i],
									remaining[i]);
									//Total messages should be index of channels latest message;
									//Messages read array should be incremented by Next/Livefeed commands
									//Yet to read array should be incremented when the SEND command is used on every subscribed channel
									//and decremented when a message is read by Next/Livefeed
									write(new_fd, outbuffer, sizeof(outbuffer));
									sleep(1);
								}
								bzero(inbuffer, MAX_DATA_SIZE);
								bzero(outbuffer, MAX_DATA_SIZE);
							}
						}
						break; 
						
					case UNSUB: //UNSUB
						//For server
						subbed_clients[channelnum] = false; //Set client to unsubscribed status.
						printf("client %d unsubscribed to channel %d \n", clientid, channelnum); //Submit message to server.
						bzero(outbuffer, MAX_DATA_SIZE);
						
						//For client
						//Tell the client they have subscribed successfully.
						sprintf(outbuffer, "Successfully unsubscribed client %d from channel %d \n", clientid, channelnum);
						write(new_fd, outbuffer, sizeof(outbuffer));
						
						bzero(outbuffer, MAX_DATA_SIZE);
						bzero(inbuffer, MAX_DATA_SIZE);
						break; 
						
					case NEXT: 
						printf("Getting message from channel %d\n",channelnum);
						next_message(channelnum);
						break; 
					case LIVEFEED:
						//Livefeed function is handled client side.
						printf("\nThis code should not be reached.");
						break; 
					case SEND: //6
						add_message(channelnum, inbuffer);
						break;
					case BYE:
						//TO DO: shutdown server.
						//Send close message to client
						printf("client %d has disconnected.\n", clientid);
						bzero(outbuffer, MAX_DATA_SIZE);
						//Tell the client they have subscribed successfully.
						sprintf(outbuffer, "Good bye...\n");
						write(new_fd, outbuffer, sizeof(outbuffer));
						bzero(outbuffer, MAX_DATA_SIZE);
						bzero(inbuffer, MAX_DATA_SIZE);
						break;
					default:
						//Error message should not be reachable.
						printf("Client %d sent an invalid command.\n", clientid);
						break;  
				}
			}		
			
			//Send close request to clients
			close(new_fd);
			exit(EXIT_SUCCESS);
			
		//} //FORK COMMENTED OUT FOR NOW
	}
}