#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/event.h> 

int main()
{
    //int timerfd_create(int clockid, int flags);
    int kq = kqueue();
    if (kq < 0) {
        perror("kqueue create error");
        return -1;
    }
    //int timerfd_settime(int fd, int flags, struct itimerspec *new, struct itimerspec *old);
    struct kevent change;
    EV_SET(&change, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_SECONDS, 1, 0);
    struct kevent event; 
    while(1) {
        int ret = kevent(kq, &change, 1, &event, 1, NULL);
        if (ret < 0) {
            perror("read error");
            return -1;
        }
        uint64_t times = event.data;
        printf("超时了，距离上一次超时了%llu次\n", times); 
    }
    close(kq);
    return 0;
}