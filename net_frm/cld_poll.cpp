#include "cld_poll.h"
#include <sys/epoll.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <string.h>

cld_poll::cld_poll()
{    
    m_ep_fd = -1;
    m_poll_max = 10240;//defaulte  poll fd max
}

ez_poll::~cld_poll()
{

}

int cld_poll::init()
{
    assert(m_ep_fd == -1);

    m_ep_fd = epoll_create(m_poll_max);
    
    return m_ep_fd;
}

int cld_poll::setnonblock(int fd)
{
    int flag = fcntl(fd, F_GETFL);
    if (flag == -1)
        return -1;
    if (fcntl(fd, F_SETFL, flag | O_NONBLOCK) == -1)
        return -1;
    return 0;
}

int cld_poll::add(int fd, cld_fd *cfd)
{
    assert(fd >= 0);
    assert(cfd);
    assert(m_ep_fd != -1);
    assert(fd > m_fd_max || (fd <= m_fd_max && !m_alive_fd_pool[fd]));
    
    if (setnonblock(fd) == -1)
        return -1;

    cld_poll_item *data = new cld_poll_item();
    data->m_fd = fd;// set fs fd
    data->m_cld_fd = cfd;// set cld fd instance(base clasee for server/connection/client)
    data->m_event = cld_none;// default do nothing, set/clear interface could set event listener configure
    data->m_bclosed = false;

    // adjust the fd pool size for new fd.(increase pool),  //hashtable is  better?
    if (fd > m_fd_max)
    {
        cld_poll_item **tmp = (cld_poll_item **)realloc(m_alive_fd_pool, (fd + 1) * sizeof( cld_poll_item * ));
        if (!tmp)
        {
            delete data;
            return -1;
        }
        m_alive_fd_pool = tmp;
        memset(m_alive_fd_pool + m_fd_max + 1, 0, sizeof( cld_poll_item * ) * (fd - m_fd_max));
        m_fd_max = fd;
    }
    //set pool configure
    m_alive_fd_pool[fd] = data;

    // register the epoll event listener to kernel
    struct epoll_event event = {0};
    event.data.ptr = (void *)(data);
    if (epoll_ctl(m_ep_fd, EPOLL_CTL_ADD, fd, &event) == -1)
    {
        delete data;
        m_alive_fd_pool[fd] = NULL;
        return -1;
    }
    return 0;
}


int cld_poll::del(int fd)
{
    assert(fd <= m_fd_max);
    assert(fd >= 0);
    assert(m_alive_fd_pool[fd]);

    // if poll action is runing, just move the fd to close_fd_pool and unreigster later.
    if (m_bpolling)
    {
        if (m_closed_fd_nr >= m_closed_fd_cnt)
        {
            cld_poll_item **tmp = (cld_poll_item **)realloc(m_closed_fd_pool, \
                                                (m_closed_fd_cnt + 1) * sizeof( cld_poll_item * ));
            if (!tmp)
                return -1;
                
            m_closed_fd_pool = tmp;
            m_closed_fd_cnt ++;
        }
        
        m_closed_fd_pool[m_closed_fd_nr++] = m_alive_fd_pool[fd];
        m_alive_fd_pool[fd]->m_bclosed = true;
    } 
    else 
    {
        delete m_alive_fd_pool[fd];
    }
    
    // unregister the epoll event listener
    epoll_ctl(m_ep_fd, EPOLL_CTL_DEL, fd, NULL); 
    m_alive_fd_pool[fd] = NULL;
    return 0;
}



int cld_poll::update(int fd)
{
    cld_poll_item *item = m_alive_fd_pool[fd];
    struct epoll_event event = {0};
    
    event.data.ptr = item;
    event.events = (item->event_ & cld_read ? EPOLLIN : 0) | (item->event_ & cld_write ? EPOLLOUT : 0);
    
    return epoll_ctl(m_ep_fd, EPOLL_CTL_MOD, fd, &event);
}


int cld_poll::set(int fd, int event_flags)
{
    assert(fd >= 0);
    assert(fd <= m_fd_max);
    assert(m_alive_fd_pool[fd]);
    
    m_alive_fd_pool[fd]->event_ |= event_flags;//cld_read;

    return 0;
}

int cld_poll::clear(int fd, int event_flags)
{
    assert(fd >= 0);
    assert(fd <= m_fd_max);
    assert(m_alive_fd_pool[fd]);
    
    m_alive_fd_pool[fd]->event_ &= ~event_flags;//cld_write;

    return 0;
}


int cld_poll::poll(int timeout)
{
    assert(m_ep_fd != -1);
    
    m_bpolling = true;
    
    struct epoll_event events[32];
    
    int numfd = epoll_wait(m_ep_fd, events, 32, timeout);// here maybe blocked.
    if (numfd <= 0)
        return 0;
        
    for (int i = 0; i < numfd; ++i)
    {
        cld_poll_item *item = (cld_poll_item *)events[i].data.ptr;
        cld_fd *cld_fd = item->m_cld_fd;
        
        if (item->m_bclosed) 
            continue;
            
        short event = cld_none; 
        
        if ((item->event_ & cld_read) && (events[i].events & EPOLLIN))
            event |= cld_read;
            
        if ((item->event_ & cld_write) && (events[i].events & EPOLLOUT))
            event |= cld_write;
            
        if (events[i].events & (EPOLLERR | EPOLLHUP))
            event |= cld_error;
            
        cld_fd->on_event(this, item->m_fd, event);
    }

    // lookup the close fd pool wheter exist the deactive fd;
    for (int i = 0; i < m_closed_fd_nr; ++i)
    {
        assert(m_closed_fd_pool[i]->m_bclosed);
        delete m_closed_fd_pool[i];
        m_closed_fd_pool[i] = NULL;
    }
    
    m_closed_fd_nr = 0;
    m_bpolling = false;
    return 0;
}

int cld_poll::run()
{
    m_run = true;
    while (m_run)
    {
        this->poll(1);
    }
    this->poll(0); // clear unfinish tasks
    return 0;
}

int cld_poll::stop()
{
    assert(m_run);
    // here need add mutex for mutitask access
    m_run = false;
    return 0;
}




