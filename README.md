timestamp-series-compress
=========================

Code to compress and decompress timestamp series
coder -- compresses a series of timestamps
decoder -- uncompress the series of timestamps encoded by the coder



compilation
-------------
gcc -lm coder.c -o coder
gcc -lm decoder.c -o decoder



Usage
-------------
./coder <input_timestamp_file> <coded_time_stamp_file>
./decoder <coded_time_stamp_file> <decoded_time_stamp_file>

