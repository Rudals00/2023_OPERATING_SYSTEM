#include "types.h"
#include "user.h"
#include "fcntl.h"

#define SIZE 1024
#define SIX_MB (6 * 1024 * 1024)
#define SIXTEEN_MB (16 * 1024 * 1024)

char buffer[1024];

void write_to_file(char *filename, int target_size) {
    int fd = open(filename, O_CREATE | O_RDWR);
    if (fd < 0) {
        printf(2, "Failed to open file: %s\n", filename);
        exit();
    }

    int bytes_written = 0;
    while(bytes_written < target_size) {
        int res = write(fd, buffer, SIZE);
        if (res < 0) {
            printf(2, "Error writing file: %s\n", filename);
            close(fd);
            exit();
        }
        bytes_written += res;
    }

    printf(1, "Successfully written %dMB to file: %s\n", target_size / (1024 * 1024), filename);
    close(fd);
}

void test_read(char *filename, int target_size) {
    int fd = open(filename, 0); // read-only
    if (fd < 0) {
        printf(2, "Failed to open file: %s\n", filename);
        exit();
    }

    int bytes_read = 0;
    while(bytes_read < target_size) {
        int res = read(fd, buffer, SIZE);
        if (res < 0) {
            printf(2, "Error reading file: %s\n", filename);
            close(fd);
            exit();
        }
        bytes_read += res;
    }

    printf(1, "Successfully read %dMB from file: %s\n", target_size / (1024 * 1024), filename);
    close(fd);
}

int main(void) {
    memset(buffer, '0', SIZE); // Fill the buffer with '0'

    write_to_file("6mbfile", SIX_MB);
    test_read("6mbfile", SIX_MB);

    write_to_file("16mbfile", SIXTEEN_MB);
    test_read("16mbfile", SIXTEEN_MB);

    exit();
}