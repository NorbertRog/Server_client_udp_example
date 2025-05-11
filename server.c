#include <stdbool.h>
#include <pthread.h>
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


#define DEFAULT_DATARATE 5
#define DEFAULT_DATALOG 5
#define DEFAULT_MAX_FILENAME_LENGTH 128
#define MAXLINE 8
#define BUFFER_SIZE 8

#define PORT 5000 // default server port 

struct loging_data_vars {
	int lograte_seconds;
	char* file_path_and_name;
	char* buffer;
};

void  print_help()
{				
	printf("-i ipv4 addressof the server\n"
	"-p udpport of the server\n"
	"-d datarate sending in seconds default value:" STR(DEFAULT_DATARATE) "\n"
	"-l datalog rate in seconds default value:" STR(DEFAULT_DATALOG) "\n"
	"-f filenme to log data if no file named no data will be loged into file max lenght of this string is:"STR(DEFAULT_MAX_FILENAME_LENGTH)"\n"
	" additional info default server port is:" STR(PORT) "\n");
}


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

void encode_status_binary(uint8_t *buffer) {
	static uint16_t call_count = 0;
	call_count++;

	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);

	uint8_t hour = tm_info->tm_hour;
	uint8_t minute = tm_info->tm_min;
	uint8_t second = tm_info->tm_sec;

	// Random float value from 20.0 to 120.9
	int int_part = 20 + rand() % 101;
	int dec_part = rand() % 10; // 0-9

	// Encode as single byte: int in bits [7:1], decimal in [3:0] (low nibble)
	uint8_t value_byte = int_part;
	uint8_t decimal_nibble = (uint8_t)dec_part;

	// Power state: 0 = battery, 1 = pluged
	uint8_t power_state = (rand() % 2 == 0) ? 0 : 1;

	// Fill the binary buffer
	buffer[0] = hour;
	buffer[1] = minute;
	buffer[2] = second;
	buffer[3] = value_byte;
	buffer[4] = (decimal_nibble & 0x0F) | (power_state << 4);  // combine decimal + power bit
	buffer[5] = (call_count >> 8) & 0xFF;  // high byte
	buffer[6] = call_count & 0xFF;		 // low byte

	// Compute CRC8 on first 7 bytes
	buffer[7] = crc8(buffer, 7);
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

void* loging_data(void *arg) {
    struct loging_data_vars *data_vars = (struct loging_data_vars *)arg;

	char decoded_line[128];
	FILE *file;

	

	if(data_vars->file_path_and_name[0]!='\0') {
		file = fopen(data_vars->file_path_and_name, "a"); // Open for appending
		if (!file) {
			printf("Failed to open or create file\n");
		}
	}

	while(true)
	{
		sleep(data_vars->lograte_seconds);
		decode_status_binary(data_vars->buffer, decoded_line, sizeof(decoded_line));
		printf("%s\n", decoded_line);
		if(file){
			fprintf(file, "%s\n", decoded_line);
		}
	}
	fclose(file);
}

void sender_main(struct sockaddr_in addr, int datarate_seconds, int lograte_seconds, char *file_path_and_name)
{
    ssize_t sent;
    pthread_t thread0;
    uint8_t buffer[BUFFER_SIZE];

    int listenfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listenfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    addr.sin_family = AF_INET; // Add this line

    struct loging_data_vars data_vars;
    data_vars.lograte_seconds = lograte_seconds;
    data_vars.file_path_and_name = file_path_and_name;
    data_vars.buffer = buffer;

    pthread_create(&thread0, NULL, loging_data, (void *)&data_vars);

    while (true) {
        encode_status_binary(buffer);

        sent = sendto(listenfd, buffer, MAXLINE, 0, 
                      (struct sockaddr*)&addr, sizeof(addr)); 
        if (sent < 0) {
            perror("sending error");
        }

        sleep(datarate_seconds);
    }

    close(listenfd);
}

int main(int argc, char *argv[]) {
	int opt;
	int s;


	//start variables 
	uint16_t port;
	struct sockaddr_in addr;
	int datarate_seconds = DEFAULT_DATARATE;
	int lograte_seconds = DEFAULT_DATALOG;
	char file_path_and_name[DEFAULT_MAX_FILENAME_LENGTH];
	file_path_and_name[0]='\0';

	bzero(&addr, sizeof(addr));

	  
	// put ':' in the starting of the 
	// string so that program can  
	//distinguish between '?' and ':'  
	while((opt = getopt(argc, argv, "i:f:d:p:l:f:h")) != -1)  
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
				
				if (is_valid_uint16(optarg, &port) != 1) {
					printf("failed to parse port number exiting\n");
					exit(EXIT_FAILURE);
				}
				addr.sin_port = htons(port);
				break;
			case 'd'://sendling datarate in seconds
				s=atoi(optarg);
				if(s>0){
					datarate_seconds=s;
				}
				break;  
			case 'l'://lograte of data in seconds  
				s=atoi(optarg);
				if(s>0){
					lograte_seconds=s;
				}
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
	sender_main(addr,datarate_seconds,lograte_seconds,file_path_and_name);
	}
   return 0;
}
