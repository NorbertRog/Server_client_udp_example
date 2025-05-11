#include <stdbool.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>


// this converts to string
#define STR_(X) #X
// this makes sure the argument is expanded before converting to string
#define STR(X) STR_(X)

#define DEFAULT_MAX_FILENAME_LENGTH 128
#define MAXLINE 1000
#define BUFFER_SIZE 8

#define PORT 5000 // default server port 

// CRC8 with polynomial 0x07
unsigned char crc8(const unsigned char *data, size_t len) {
	unsigned char crc = 0;
	while (len--) {
		crc ^= *data++;
		for (int i = 0; i < 8; ++i) {
			if (crc & 0x80)
				crc = (crc << 1) ^ 0x07;
			else
				crc <<= 1;
		}
		crc &= 0xFF;
	}
	return crc;
}

/**
 * Checks if the given string is a valid representation of a uint16_t value.
 * If valid, stores the result in *out and returns 1.
 * Otherwise, returns 0 and *out is not modified.
 */
 int is_valid_uint16(const char *str, uint16_t *out) {
	char *endptr;
	errno = 0;

	// Check for NULL or empty string
	if (str == NULL || *str == '\0') {
		return 0;
	}

	// Optional: skip leading whitespace
	while (isspace((unsigned char)*str)) {
		str++;
	}

	// Check for non-digit characters (optional but stricter validation)
	for (const char *p = str; *p != '\0'; ++p) {
		if (!isdigit((unsigned char)*p)) {
			return 0;
		}
	}

	unsigned long val = strtoul(str, &endptr, 10);

	if (errno != 0 || *endptr != '\0') {
		return 0; // Conversion error or extra characters
	}

	if (val > UINT16_MAX) {
		return 0; // Value too large for uint16_t
	}

	*out = (uint16_t)val;
	return 1;
}

// Formats binary data into "HH:MM:SS;VALUE;STATUS;COUNT;CRC8 (valid|invalid)"
void decode_status_binary(const uint8_t *buffer, char *output_str, size_t max_len) {
	// Extract and verify CRC
	uint8_t computed_crc = crc8(buffer, 7);
	uint8_t received_crc = buffer[7];
	const char *crc_status = (computed_crc == received_crc) ? "valid" : "invalid";

	// Extract fields
	uint8_t hour = buffer[0];
	uint8_t minute = buffer[1];
	uint8_t second = buffer[2];

	int int_part = buffer[3];
	int dec_part = buffer[4] & 0x0F;
	float value = int_part + dec_part / 10.0f;

	uint8_t power_state = (buffer[4] >> 4) & 0x01;
	const char *power_state_str = power_state ? "pluged" : "battery";

	uint16_t call_count = (buffer[5] << 8) | buffer[6];

	// Format output string
	snprintf(output_str, max_len, "%02d:%02d:%02d;%.1f;%s;%d;%02X (%s)",
			 hour, minute, second, value, power_state_str,
			 call_count, received_crc, crc_status);
}


void  print_help()
{				
	printf("-i ipv4 addressof the server\n"
	"-p udpport of the server\n"
	"-f filenme to log data if no file named no data will be loged into file max lenght of this string is:"STR(DEFAULT_MAX_FILENAME_LENGTH)"\n"
	" additional info default server port is:" STR(PORT) "\n");
}

void reciver_main(struct sockaddr_in addr, char *file_path_and_name)
{
	int sockfd;
	char decoded_line[128];
	char buffer[8];
	FILE *file = NULL;
	sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
	  
	// connect to server 
	sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
	if (sockfd < 0) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}
	
	addr.sin_family = AF_INET;
	if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}


	if(file_path_and_name[0] != '\0') {
		file = fopen(file_path_and_name, "a"); // Open for appending
		if (!file) {
			printf("Failed to open or create file\n");
			file=NULL;
		}
	}	

	while(true)
	{
		// waiting for response
		recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)NULL, NULL); 
		decode_status_binary(buffer, decoded_line, sizeof(decoded_line));
		printf("%s\n", decoded_line);
		if(file!=NULL){
			fprintf(file, "%s\n", decoded_line);
			fflush(file);
		}
	}
  
	
}

int main(int argc, char *argv[]) {
	int opt;
	int s;
	uint16_t port;


	//start variables 
	struct sockaddr_in addr;
	char file_path_and_name[DEFAULT_MAX_FILENAME_LENGTH];
	file_path_and_name[0] = '\0';

	bzero(&addr, sizeof(addr));

	  
	// put ':' in the starting of the 
	// string so that program can  
	//distinguish between '?' and ':'  
	while((opt = getopt(argc, argv, "i:f:p:f:h")) != -1)  
	{  
		switch(opt)  
		{  
			case 'h':  
			print_help();
				break;
			case 'i'://ip address of server
				s = inet_pton(AF_INET, optarg, &addr.sin_addr);
				if (s <= 0) {
					if (s == 0)
						printf("wrog address format");
					else
						printf("failed to parse ip address");
					exit(EXIT_FAILURE);
				}
				break;
			case 'p'://udp port of server
			if (!is_valid_uint16(optarg, &port)) {
				printf("failed to parse port number exiting\n");
				exit(EXIT_FAILURE);
			}
			addr.sin_port = htons(port);
				break;
			case 'f'://path to logifile
				strncpy(file_path_and_name,optarg,DEFAULT_MAX_FILENAME_LENGTH);
				break;
			case '?':
				printf("unknown option reffer to help bellow\n");
		}  
	}  
	  
	// optind is for the extra arguments 
	// which are not parsed 
	for(; optind < argc; optind++){	  
		printf("unknown arguments: %s\n", argv[optind]);  
	} 

	if(addr.sin_addr.s_addr != 0) {
		if(addr.sin_port == 0)
		{
			addr.sin_port = htons(PORT);
		}
	reciver_main(addr, file_path_and_name);
	}

   return 0;
}