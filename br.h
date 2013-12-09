/* code taken from linux kernel */
#if __GNUC__ == 2 && __GNUC_MINOR__ < 96
#define __builtin_expect(x, expected_value) (x)
#endif
#ifndef __builtin_expect
#define __builtin_expect(x, expected_value) (x)
#endif
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

class buffered_reader
{
private:
        int fd, block_size;
        char *buffer;
        long long int buffer_length, buffer_pointer;
	char *mmap_addr, *cur_offset;
	off64_t size_of_file;

        int number_of_bytes_in_buffer(void);
        int read_into_buffer(void);

public:
        buffered_reader(int fd, int block_size=4096);
        ~buffered_reader();

        int garbage_collect(char shrink_buffer=0);

        char * read_line(void);

	off64_t file_offset(void);
};
