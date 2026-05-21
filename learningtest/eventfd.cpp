#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("eventfd failed!!");
        return -1;
    }

    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    fcntl(pipefd[1], F_SETFL, O_NONBLOCK);

    uint64_t val = 1;
    write(pipefd[1], &val, sizeof(val));
    write(pipefd[1], &val, sizeof(val));
    //write(efd, &val, sizeof(val));
    uint64_t res = 0;
    uint64_t temp = 0;
    if (read(pipefd[0], &temp, sizeof(temp)) == sizeof(temp)) res += temp;
    if (read(pipefd[0], &temp, sizeof(temp)) == sizeof(temp)) res += temp;
    printf("%llu\n", res);

    close(pipefd[0]);
    close(pipefd[1]);
    return 0;
}