#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>a
#include <string.h>
#include <time.h> 
#include <sys/stat.h>
#include <inttypes.h>
#include <math.h>



#define TIMESTAMP_SIZE 18
#define TS_SEC_SIZE 10
#define TS_USEC_SIZE 6

#define BUF_SIZE 2048

#define NUM_SECS_IN_DAY 24*60*60

char single_line[TIMESTAMP_SIZE+1];
char first_time_stamp[TIMESTAMP_SIZE];
char prev_time_stamp [TIMESTAMP_SIZE];

uint32_t ts_counter = 0;
uint8_t max_byte_changeable = 0;

uint8_t half_byte_remaining = 0;
uint8_t last_half_byte = 0;

FILE *log_file = NULL;
FILE *stats_file = NULL;




/*The getNextLine function reads the file and stores the timestamp 
 *in a global variable . The pointer to this string is returned 
 *by this function. 
 *
 *This function returns NULL when the read fails or EOF is reached
 */
char *getNextLine (FILE *fp)
{
  if (fgets (single_line, TIMESTAMP_SIZE+1, fp) == NULL) {
		return NULL;
	}
	single_line[10] = '\0';
	single_line[17] = '\0';
	//fprintf(log_file, "sec %s usec %s\n", single_line, &single_line[11]);
	return single_line;
	
}

/*Custom function to convert string to integer
 * This function will convert only positive integers and so 
 * is faster than the inbuilt atoi
 */
uint32_t custom_str_to_int (char *num_str, uint8_t size) { //Unsafe but fast way of converting
	uint32_t value = 0;
	uint8_t i = 0;

	for ( i = 0; i < size; i++) {
		value = 10 * value + (num_str[i] - 48);
	}

	return value;

}

/*Custom function to give the difference between Micro-Second part of two timestamp strings.
 * Parameters ---
 * The two timestamp strings , size- The number of bytes representing each microsecond
 * value, seconds_diff - The address to store the diff between seconds, diff - Address to store 
 * difference between microseconds 
 * Return value --- 
 * Returns the number of bytes required to store the micro-seconds value 
 *
 * If the previous micro-second value is larger than the current micro-second value, the difference 
 * between the seconds of each timestamp is reduced by 1 and the difference between the two 
 * microsecond values are calculated.
 * */
uint8_t custom_str_to_diff_usec (char *cur_tstamp, char *last_tstamp, uint8_t size,
				uint32_t *diff, uint32_t *seconds_diff) {
	
	
	uint32_t cur_ts = custom_str_to_int (cur_tstamp, size);
	uint32_t last_ts =  custom_str_to_int(last_tstamp, size);
	if (cur_ts >= last_ts) {
		*diff = cur_ts - last_ts;
	} else {
		*diff = (1000000 - last_ts ) + cur_ts;
		if (*seconds_diff != 0) {
			*seconds_diff -= 1;
		}
	}

	
	if (*diff < 255) {
		return 1; 	
	} else if (*diff < 65535) {
		return 2;
	} else if (*diff < 1000000) {
		return 3;
	} else {
		
		printf("Error in timestamp series EXIT\n");
		fprintf(log_file," cur_ts %d last_ts %d diff %d  size = %d\n",
			cur_ts , last_ts, *diff, size);
		exit(0);
	}	
		
	
}


/*Custom function to give the difference between Seconds part of two timestamp strings.
 * Parameters ---
 * The two timestamp strings , size- The number of bytes representing each second
 * value, diff - Address to store difference between microseconds 
 *
 * Return value --- 
 * Returns the number of bytes required to store the seconds value 
 */



uint8_t custom_str_to_diff_sec (char *cur_tstamp, char *last_tstamp, uint8_t size,
				uint32_t *diff) {
	
	
	*diff = custom_str_to_int (cur_tstamp, size) - custom_str_to_int(last_tstamp, size);

	if (*diff < 255) {
		return 1; 	
	} else if (*diff < 65535) {
		return 2;
	} else if (*diff < 86400) {
		return 3;
	} else {
		
		printf("Error in timestamp series EXIT \n");
		exit(0);
	}	
		
	
}

/*htonll function to convert long long type to network byte order*/

uint64_t htonll(uint64_t value) {
    int num = 42;
    if (*(char *)&num == 42) {
        uint32_t high_part = htonl((uint32_t)(value >> 32));
        uint32_t low_part = htonl((uint32_t)(value & 0xFFFFFFFFLL));
        return (((uint64_t)low_part) << 32) | high_part;
    } else {
        return value;
    }
}

/*The codeNextTimestamp implementation
 *
 */
void codeNextTimestamp (char *tstamp, FILE *output_file)
{
	
 	int i = 0;
	uint8_t num_sec_bytes = 0;
	uint8_t num_usec_bytes = 0;
	uint32_t sec_diff = 0;
	uint32_t usec_diff = 0;
	uint32_t stored_half_byte = 0;

	uint32_t flags = 0;
	uint8_t *insert_ptr = NULL;

	uint64_t loc_sec_buf = 0;
	uint64_t loc_usec_buf = 0;
	uint64_t loc_half_buf = 0;
	uint16_t shift_bytes = 0;
	
	/*iterate through the current and previous timestamp from the max_changeable_byte to
 * 	  the end of the seconds part.
 * 	  This will find out which is the largest integer which has changed between the two 
 * 	  second strings*/

	for (i = max_byte_changeable; i < TS_SEC_SIZE; i++) {
		if (tstamp[i] ^ prev_time_stamp[i])
			break;
	}




	/*If seconds have changed between the previous timestamp, the i value will be less than 
 * 	  the length of SECONDS value in the timestamp representation
 * 	  If it has changed then calculate the difference between the seconds part*/

	if ( i != TS_SEC_SIZE ) { 
		num_sec_bytes = custom_str_to_diff_sec (&tstamp[i], &prev_time_stamp[i], TS_SEC_SIZE - i,
					&sec_diff);
	}
	

	/*Similarly find the largest integer changed in the  microsend string representation*/
	for ( i = TS_SEC_SIZE + 1; i < TIMESTAMP_SIZE - 1; i++ ) {
		if (tstamp[i] ^ prev_time_stamp[i])
			break;
	}




	/*If th micro-second part has changed , then calculate the difference between the two microsecond
 * 	parts of current and previous timestamp*/

	if ( i != (TIMESTAMP_SIZE - 1)) {
		num_usec_bytes = custom_str_to_diff_usec (&tstamp[i], &prev_time_stamp[i], (TIMESTAMP_SIZE -i - 1),
					&usec_diff, &sec_diff);
	}


		/*fprintf(log_file, "START usec_diff %d num_usec_bytes %d  sec_bytes %d "
					"sec_diff %d half_byte_remaining %d  last_half_byte %d\n", usec_diff, num_usec_bytes,
				num_sec_bytes, sec_diff, half_byte_remaining, last_half_byte);*/



/*If two timestamps of equal value are sent , we represent that by just writing a 4-Bit flag of 0 value. We
 * write this onto the file and return*/

	if (!(num_usec_bytes | num_sec_bytes) ) {
		//Two equal timestamps  put 0 FLAG
		if (half_byte_remaining == 1 ) {
			flags = last_half_byte << 28;
			flags = htonl (flags);
			fwrite (&flags, sizeof(char), 1, output_file);
			half_byte_remaining =  0;
			return;
		} else if ( half_byte_remaining ==0) {

			last_half_byte = 0x00; 
			half_byte_remaining = 1;
			fprintf(log_file,"last  half %0x \n", last_half_byte);
			return;
		}
	}





/*The main  coding logic starts from here.
 * It checks if seconds_difference is 0 or not 0. It also checks if 4 bits from a previous byte which was not written is remaining 
 * by checking the value half_byte_remaining . The remaining half is stored in stored_half_byte variable
 *
 * Since the flag is represented only by 4 bits , the seconds /useconds bytes following this is shifted 4 bits onto the flag byte to
 * save 4 bits of space.
 *
 * When we shift 4 bits into the flag bytes, there is always a trailing half byte remaining at the end which is not written in this 
 * cycle. It will be stored and written in the next cycle.
 *
 *
 */


	if ( sec_diff == 0 ) {
		if (half_byte_remaining == 0) {
		
			//Bitwise operations to represent the flag and usec(Micro-second) bytes in
			//num_usec_bytes + 0.5 bytes. The 0.5 bytes is acheied by pushing usecs value by 4 bits
			flags |= (num_usec_bytes << 28);
			last_half_byte = usec_diff;
			usec_diff <<= ((8 * (3 - num_usec_bytes)) + 4);
			usec_diff |= flags;
			usec_diff = htonl (usec_diff);


			//Write the exact number of bytes into file, The last Half a byte will not be written and 
			//will be written when the next timestamp is processed.
			//
			fwrite ((char *)&usec_diff, sizeof (char), num_usec_bytes, output_file );


			half_byte_remaining = 1;
			return;


		} else if ( half_byte_remaining == 1) {

			//If half a byte is remaining , then that 4 bits is also adjusted and 
			//the flag and usec bytes are written on to a 32 bit uint and written to
			//the file
			//
			//
			flags |= num_usec_bytes << 24;
			stored_half_byte = last_half_byte << 28;
			usec_diff <<= (8* (3 - num_usec_bytes)) ;
			usec_diff |= stored_half_byte;
			usec_diff |= flags;
			usec_diff = htonl(usec_diff);


			fwrite ((char *)&usec_diff, sizeof (char), num_usec_bytes + 1, output_file);

			half_byte_remaining = 0;
			return;
		}
	} if ( sec_diff != 0) {

		if (half_byte_remaining == 0) {

			//If we have seconds bytes and useconds bytes to represent 
			//We merge everything into a uint64_t variable using bitwise 
			//operations and then write to file
			//
			// THis case does not have any previous half bytes remaining.
			//
			last_half_byte = usec_diff;
			sec_diff <<= ((8 * (3 - num_sec_bytes)) + 4);
			usec_diff <<= (8 * (4 - num_usec_bytes)) ;
			flags |= (num_sec_bytes << 30);
			flags |= (num_usec_bytes << 28);
			sec_diff |= flags;
			loc_sec_buf = sec_diff;
			loc_sec_buf <<= 32;
			loc_usec_buf = usec_diff;
			shift_bytes = 32 - (num_sec_bytes * 8) - 4;
			loc_usec_buf <<= shift_bytes;
			loc_sec_buf |= loc_usec_buf;
			loc_sec_buf = htonll (loc_sec_buf);



			fwrite ((char *)&loc_sec_buf, sizeof(char), (num_sec_bytes + num_usec_bytes), output_file);
			half_byte_remaining = 1;
			return;
		} else if ( half_byte_remaining == 1) {

			//If we have seconds bytes and useconds bytes to represent 
			//We merge everything into a uint64_t variable using bitwise 
			//operations and then write to file
			//
			//THis case does  have previous half bytes remaining.
			//
			
			loc_half_buf = last_half_byte;
			loc_half_buf <<= 60 ;
			
			sec_diff <<= (8 *(3 - num_sec_bytes) + 4);
			usec_diff <<= (8 *(4 - num_usec_bytes) );
			
			flags |= (num_sec_bytes << 30);
			flags |= (num_usec_bytes << 28);
			sec_diff |= flags;

			loc_sec_buf = sec_diff;
			loc_sec_buf <<= 28;
		
			loc_usec_buf = usec_diff;
			loc_usec_buf <<= ( (3-num_sec_bytes) * 8);

			
			loc_sec_buf |= loc_usec_buf;
			loc_sec_buf |= loc_half_buf;

			loc_sec_buf = htonll (loc_sec_buf);


			fwrite ((char *)&loc_sec_buf, sizeof(char), (num_usec_bytes + num_sec_bytes + 1), output_file);


			half_byte_remaining = 0;
			return;
		}
	}
		

}



void add_time_taken_to_file ( struct timeval *before, struct timeval *after)
{
	int useconds_diff = 0;

	useconds_diff = after->tv_usec - before->tv_usec;

	fprintf(log_file, "before %d %d after %d %d", before->tv_sec, before->tv_usec,
			after->tv_sec, after->tv_usec);


	fprintf(stats_file,"%d\n", useconds_diff);
	
}

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
	
	stats_file = fopen ("stats_file", "r");
	
	while (fgets (diff, 8, stats_file) != NULL) {
		diff_count = atoi(diff);
		total_time += diff_count;
		count++;
	}
	count++;
	printf("Total time taken %d Number of timestamps %d\n", total_time, count);
	average_time = (((float)total_time) / count);
	printf("Average Time Taken to compress a single timestamp %f \n", average_time);

	rewind(stats_file);

	while (fgets (diff,8, stats_file) != NULL) {
		diff_count = atoi(diff);
		std_dev += pow ((diff_count - average_time), 2);
	}
	printf("Standard deviation = %g \n", sqrt(std_dev/(count -1 )));
	fclose(stats_file);

}

int main (int argc, char *argv[])
{
	FILE *inp_fp = NULL;
	FILE *out_fp = NULL;

	uint32_t first_tv_sec = 0;
	uint32_t max_tv_sec = 0;
	
	
	char max_tv_sec_str[TS_SEC_SIZE];
	char *time_stamp_str = NULL;

	uint32_t num_of_ts_procd = 0;
	int i = 0;

	struct timeval before;
	struct timeval after;

	
	if (argc != 3) {
		printf("Wrong usage !\n Correct Usage: ./coder <input_file> <output_file>\n");
		return;
	}
	log_file = fopen("coder_logs", "w+");
	stats_file = fopen("stats_file", "w+");

	inp_fp = fopen (argv[1], "r");
	if (inp_fp == NULL) {
		printf("Open timestamps file failed \n");
		return;
	}
	out_fp = fopen (argv[2], "w+");
	if (out_fp == NULL) {
		printf("Open output file failed\n");
		return;
	}
	

	//First timestamp is stored as is in the coded file.
	time_stamp_str = getNextLine (inp_fp);
	fprintf (out_fp,"%s.%s", time_stamp_str, &time_stamp_str[11]);
	memcpy(first_time_stamp, time_stamp_str, TIMESTAMP_SIZE);


	
	memcpy(prev_time_stamp, time_stamp_str, TIMESTAMP_SIZE);
	

	//The first timestamp is used to calculate , what is the max_changeable_byte on
	//the remaining timestamps.
	//
	//THis is calculated by adding the seconds part with the number of seconds in a day
	//and checking which is the highest byte which has changed.
	//
	first_tv_sec = atol(first_time_stamp);
	max_tv_sec = first_tv_sec + NUM_SECS_IN_DAY;
	sprintf(max_tv_sec_str,"%d", max_tv_sec);

	for (i = 0; i < TS_SEC_SIZE; i++) {

		if ((max_tv_sec_str[i] ^ first_time_stamp[i]))
			break;
	}
	max_byte_changeable = i;







	//Loop through the file to code all the timestamps into the coded_file.
	//
	time_stamp_str = getNextLine (inp_fp);
	
	while (time_stamp_str != NULL )  {
		gettimeofday(&before, NULL);
		codeNextTimestamp (time_stamp_str, out_fp);
		gettimeofday(&after, NULL);
		memcpy (prev_time_stamp, time_stamp_str, TIMESTAMP_SIZE);
		time_stamp_str = getNextLine (inp_fp);
		num_of_ts_procd++;
		
		add_time_taken_to_file (&before, &after);

	} 
	
	coder_calculate_stats ( );


	//This hack is to handle the case when the last time_stamp is the repetition of
	//the previous timestamp. In that case we will miss writing this 0 flag into the file.
	if (half_byte_remaining == 1) {
		last_half_byte = 0x05;
		fwrite (&last_half_byte, sizeof(char), 1, out_fp);
		half_byte_remaining =  0;

	}
	
	
	



	fclose(out_fp);
	fclose(inp_fp);
	fclose(log_file);
	return 0;	
	


	
}
