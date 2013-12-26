
#ifndef _CLD_POLL_H
#define _CLD_POLL_H

#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

enum cld_event
{
    cld_none = 0x00,
    cld_read = 0x01, 
    cld_write = 0x02,
    cld_error = 0x04,
};


class cld_poll;
class cld_fd;

// base class for server/connection/client etc.  class.
class cld_fd
{
public:
    virtual void on_event(cld_poll *poll, int fd, short event) = 0;
    virtual ~cld_fd() { };
};

// class poll for do fs event monitor.
class cld_poll 
{
public:
   cld_poll();
    ~cld_poll();
    int init();
    int add(int fd, cld_fd *cfd);
    int del(int fd);
    int update(int fd);
    int set(int fd, int listener_flags);
    int clear(int fd, int listener_flags);
    int poll(int timeout);
    
    int setnonblock(int fd);
    int stop();
    int run();
private:
    //poll runtime info
    int m_poll_max;
    int m_ep_fd;
    bool m_run;
    bool m_bpolling;
    
    // local class for poll pool description
    class cld_poll_item
    {
    public:
        int m_fd;
        cld_fd *m_cld_fd;
        short m_event;
        bool m_bclosed;
    };

    // fd info cache for poll control
    cld_poll_item **m_alive_fd_pool;// store the poll item class pointer
    cld_poll_item **m_closed_fd_pool;// store closed  item class pointer
    int m_closed_fd_nr;
    int m_closed_fd_cnt;
    int m_fd_max;
};


#endif

