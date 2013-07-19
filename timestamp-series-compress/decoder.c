#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <time.h> 
#include <sys/stat.h>
#include <math.h>

#define TIMESTAMP_SIZE 18

#define USEC_1 0x10

#define FLAG_MASK 0x0f
#define FLAG_USEC_MASK 0x03
#define BYTE_MASK 0x00

char prev_time_stamp [TIMESTAMP_SIZE];

uint8_t half_left = 0;
uint8_t half_buf = 0;


uint32_t prev_time_sec = 0;
uint32_t prev_time_usec = 0;

FILE *log_file = NULL;
FILE *stats_file = NULL;

/*Function to calculate the stats of the time taken 
 * Finds average time taken and standard deviation and
 * prints to stdout*/
void coder_calculate_stats () {
  char diff[8];
	uint8_t diff_count = 0;
	uint32_t total_time = 0;
	uint32_t count = 0;
	float average_time = 0;
	double std_dev = 0;

	if (stats_file) {
		fclose(stats_file);
	}


	stats_file = fopen ("decode_stats", "r");
	
	while (fgets (diff, 8, stats_file) != NULL) {
		diff_count = atoi(diff);
		total_time += diff_count;
		count++;
	}
	count++;
	printf("Total time taken to decode %d Number of timestamps %d\n", total_time, count);
	average_time = (((float)total_time) / count);
	printf("Average Time Taken to decode a single timestamp %f \n", average_time);

	rewind(stats_file);

	while (fgets (diff,8, stats_file) != NULL) {
		diff_count = atoi(diff);
		std_dev += pow ((diff_count - average_time), 2);
	}
	printf("Standard deviation = %g \n", sqrt(std_dev/(count -1 )));
	fclose(stats_file);

}





//This function exits program after cleanup
void program_exit ( FILE *i_fp, FILE *o_fp)
{
	coder_calculate_stats();
	fprintf(log_file, "reached EOF \n");
	fclose (log_file);
	fclose (o_fp);
	fclose (i_fp);
	exit (0);
}
	

void parseAndDecode (FILE *input_file, FILE *output_file)
{
	uint8_t sec_bytes = 0;
	uint8_t usec_bytes = 0;
	uint8_t flags = 0;
	uint32_t loc_sec_buf = 0;
	uint32_t sec_diff = 0;
	uint32_t usec_diff = 0;
	
	size_t read_bytes = 0;
	
		
	if (half_left == 0 ) {

//If there is no previous half byte remaining , Read 1 byte which contains the 4 bit flag
//and the starting 4 bit of the next byte representation
//
		read_bytes = fread (&flags, sizeof (char), 1, input_file);

	
		if (read_bytes == 0 ) { //EOF Reached
			
			program_exit(input_file, output_file );
		}


//Store half buf which is the starting of the next info.
		half_buf = flags & FLAG_MASK;

//The flags are the first 4 bits of the first byte 
		flags = (flags >> 4) ;

		if (flags == 0 ) { 
// If flags == 0 , it represent that we have a repeat of the timestamp
			fprintf(output_file, "%0d.%06d\n",prev_time_sec, prev_time_usec );
			half_left = 1;
			return;
		}
//Get the number of byte of seconds and useconds used to represent this timestamp
//diff from the the previous timestamp.
		sec_bytes = flags >> 2;
		usec_bytes = flags & FLAG_USEC_MASK;


		


//If only usec bytes are present , ready that many bytes from the file and 
//and interpret the value
		if (sec_bytes == 0 ) {

			read_bytes = fread (&usec_diff, sizeof (char), usec_bytes, input_file); 


			usec_diff = ntohl (usec_diff);
			usec_diff >>= ((4 - usec_bytes) * 8);
			loc_sec_buf = half_buf;
			half_buf = usec_diff & FLAG_MASK;
			usec_diff = usec_diff >> 4;
			usec_diff |= (loc_sec_buf << (((usec_bytes - 1) * 8) + 4));
		


//If both the sec_bytes and usec_bytes are used to represent the diff then 
//read both from file and interpret the values using bitwise operations
//
		} else if ( sec_bytes != 0) {
			read_bytes = fread (&sec_diff, sizeof (char), sec_bytes, input_file);
			sec_diff = ntohl(sec_diff);
			sec_diff >>= ((4 - sec_bytes) * 8);
			loc_sec_buf = half_buf;
			half_buf = sec_diff & FLAG_MASK;
			sec_diff = sec_diff >> 4;
			sec_diff |= (loc_sec_buf << (((sec_bytes -1) * 8) + 4));
			
			read_bytes = fread (&usec_diff, sizeof (char), usec_bytes, input_file);
			usec_diff = ntohl (usec_diff);
			usec_diff >>= ((4 - usec_bytes) * 8);
			loc_sec_buf = half_buf;
			half_buf = usec_diff & FLAG_MASK;
			usec_diff = usec_diff >> 4;
			usec_diff |= (loc_sec_buf << (((usec_bytes -1) * 8) + 4));
		
		}
		
		half_left = 1;

	} else if (half_left == 1) {
//If there is a previous half remaining , then use that half along with the read bytes and 
//form the timestamp diff 	
//
//
//
//Same timestamp condition: If the flag is 0 then we just write the previous timestamp onto the file
//
		flags = half_buf;		
		half_buf = 0;
		if (flags == 0) { 
			fprintf(output_file, "%0d.%06d\n", prev_time_sec, prev_time_usec);
			half_left = 0;
			return;
		}
		sec_bytes = flags >> 2;
		usec_bytes = flags & FLAG_USEC_MASK;





		
//Based on if the seconds bytes is 0 or not 0 we ready the number of bytes required from the file.
//Then write use the values to reverse parse the diff values along with the remaing half from 
//previous cycle
		if ( sec_bytes == 0) {

			read_bytes = fread (&usec_diff, sizeof (char), usec_bytes, input_file); 
			usec_diff = ntohl (usec_diff);
			
			
			usec_diff >>= ((4 - usec_bytes) * 8);

		} else if (sec_bytes != 0) {
			
			if (fread (&sec_diff, sizeof (char), sec_bytes, input_file) == 0) {
				program_exit (input_file, output_file);
			}
			sec_diff = ntohl (sec_diff);
			
			sec_diff >>= ((4 - sec_bytes) * 8);
			
			read_bytes = fread (&usec_diff, sizeof (char), usec_bytes, input_file); 
			usec_diff = ntohl (usec_diff);
			
			usec_diff >>= ((4 - usec_bytes) * 8);
			
		}
		
		half_left = 0;
	}




	
//Calculate the actual timestamp value from the diff interpreted
	if ((prev_time_usec + usec_diff ) >= 1000000) {
		sec_diff += 1;
		prev_time_usec = (prev_time_usec + usec_diff) - 1000000 ;
		usec_diff = 0;

		prev_time_sec += sec_diff;
	} else {
		prev_time_usec += usec_diff;
		prev_time_sec += sec_diff;
	}

	fprintf(output_file, "%0d.%06d\n", prev_time_sec, prev_time_usec);

	
	return;
	
}






void add_time_to_sum ( struct timeval *before, struct timeval *after)
{
	int useconds_diff = 0;

	useconds_diff = after->tv_usec - before->tv_usec;

	fprintf(log_file, "before %d %d after %d %d", before->tv_sec, before->tv_usec,
			after->tv_sec, after->tv_usec);

	fprintf(log_file, "useconds diff %d ", useconds_diff);

		
	fprintf(stats_file, "%d\n", useconds_diff);
}



int main (int argc, char *argv[])
{
	FILE *inp_fp = NULL, *out_fp = NULL;
	char first_time_stamp[TIMESTAMP_SIZE];
	
	struct timeval before;
	struct timeval after;

	
	if (argc != 3) {
		printf("Wrong Usage! \n: Correct Usage: ./decoder <input_file> <output_file>\n");
	}
	
	log_file = fopen ("decoder_log", "w+");
	stats_file = fopen ("decode_stats", "w+");


	inp_fp = fopen(argv[1], "r");
	if (inp_fp == NULL) {
		printf("Opening coded file failed \n");
		return;
	}



	out_fp = fopen (argv[2], "w+");
	if (out_fp == NULL) {
		printf("Opening output file failed\n");
		return;
	}

	

//Read first time stamp which is represented as it is and write it onto the output file

	fread (first_time_stamp, sizeof(char),  TIMESTAMP_SIZE - 1, 
			inp_fp);
	first_time_stamp[10] = '\0';
	first_time_stamp[17] = '\0';


//Store the previous time stamp in a variable to calculate the next timestamp using the next diff value
//
	memcpy (prev_time_stamp, first_time_stamp, TIMESTAMP_SIZE);
	prev_time_sec = atol (prev_time_stamp);
	prev_time_usec = atol (&prev_time_stamp[11]);
	fprintf(out_fp, "%0d.%06d\n", prev_time_sec, prev_time_usec);
	
	
//Loop through the coded file until EOF is reached. 
//Since the signature of the function was returning void, the function exits the program 
//if EOF is detected			
//
	while (1) {
		gettimeofday (&before, NULL);

		parseAndDecode ( inp_fp, out_fp);

		gettimeofday (&after, NULL);
		add_time_to_sum (&before, &after);

	}
	
	coder_calculate_stats ();


		
	fclose (inp_fp);
	fclose (out_fp);
	fclose (log_file);

	return 0;
	
}
