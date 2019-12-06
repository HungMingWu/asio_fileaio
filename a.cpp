#if defined(WIN32)
#define _WIN32_WINNT 0x0501
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <unistd.h>
#include <linux/aio_abi.h>
#include <sys/syscall.h>
#include <sys/eventfd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#if defined(WIN32)

static void on_read_done(boost::asio::io_service& serv,const boost::system::error_code& bsec,char* p_buf){

    if (bsec){    
        printf("**** 发生错误 *****\n");
    }
    else{        
        printf("读取到的文件内容:\n%s\n",p_buf);
    }

    serv.stop();
}

int main(){

    HANDLE h_file = CreateFileA(
        "c:\\test_aio.cpp",
        FILE_READ_DATA,FILE_SHARE_READ,NULL,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL
    );

    boost::asio::io_service io_serv;

    boost::asio::windows::stream_handle normal_file(io_serv,h_file);

    char* buf = (char*)malloc(100);
    memset(buf,0,100);

    boost::asio::async_read(
        normal_file,
        boost::asio::buffer(buf,90),
        boost::bind(&on_read_done,boost::ref(io_serv),_1,buf)
        );

    boost::asio::io_service::work idle(io_serv);

    io_serv.run();

    free(buf);

    return 0;
}

#else

static void on_read_done(boost::asio::io_service& serv,aio_context_t& ctx,const boost::system::error_code& bsec,std::size_t bytes){
    if (bytes != sizeof(uint64_t)){
        printf("**** 发生错误 *****\n");
    }
    else{
        struct io_event event_io[1];
        syscall(SYS_io_getevents,ctx,1,1,event_io,0);

	printf("read length = %d\n", uint64_t(event_io[0].res));
        struct iocb* p_operation = (struct iocb*)(event_io[0].obj);

        if (event_io[0].res2 != 0){
            printf("读取数据发生错误\n");
        }
        else if (uint64_t(event_io[0].res) != uint64_t(p_operation->aio_nbytes)){
            printf("读取部分成功: 请求读取/实际读取字节敿 %llu/%lld\n",p_operation->aio_nbytes,event_io[0].res);
        } 
        else{
            printf("读取到的文件内容:\n%s\n",(char*)(p_operation->aio_buf));
        }
    }

    serv.stop();
}

int main(){

    // 准备异步IO上下
    unsigned int max_io_cnt = 10;
    aio_context_t ctx;

    memset(&ctx,0,sizeof(ctx));
    if (syscall(SYS_io_setup,max_io_cnt,&ctx) != 0){
        printf("准备异步IO上下文失败\n");
        return -1;
    }

    // 准备eventfd,关联到boost::asio::posix::stream_descriptor,发起异步读取操作,等待事件发生
    boost::asio::io_service io_serv;
    boost::asio::posix::stream_descriptor io_notify(io_serv);

    int notify_fd = eventfd(0,EFD_NONBLOCK | EFD_CLOEXEC);
    io_notify.assign(notify_fd);

    uint64_t done_io_cnt = 0;

    boost::asio::async_read(
        io_notify,
        boost::asio::buffer(&done_io_cnt,sizeof(done_io_cnt)),
        boost::bind(&on_read_done,boost::ref(io_serv),boost::ref(ctx),_1,_2)
        );

    // 发起异步读取请求    
    int fd = open("./a.cpp",O_RDWR);

    char* buf = (char*)malloc(100);
    memset(buf,0,100);

    struct iocb* p_operation = (struct iocb*)malloc(sizeof(struct iocb));

    memset(p_operation,0,sizeof(struct iocb));
    p_operation->aio_lio_opcode = IOCB_CMD_PREAD;
    p_operation->aio_fildes = fd;
    p_operation->aio_buf = (uint64_t)(uintptr_t)buf;
    p_operation->aio_nbytes = 100;

    p_operation->aio_flags = IOCB_FLAG_RESFD;
    p_operation->aio_resfd = notify_fd;

    // 事件循环
    if (syscall(SYS_io_submit,ctx,1,&p_operation) == 1){
        boost::asio::io_service::work idle(io_serv);
        io_serv.run();
    }
    else{
        printf("发起异步IO请求失败\n");
    }

    close(notify_fd);
    close(fd);
    syscall(SYS_io_destroy,ctx);
    free(buf);

    return 0;
}

#endif
