#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <assert.h>
#include <syslog.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <future>
#include <unordered_map>
#include <set>
#include <vector>
#include <fstream>
#include "http.h"
#include "thread_pool.h"
#include "proxy.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif



using FD = int;
using namespace std::chrono_literals;
thread_pool th_pool;
//static FD epollfd;


bool inBlackList(sockaddr_in& host_address){
	std::ifstream in;
	in.open("./blacklist.txt");
	std::string list;
	while(in >> list){
		if(strcmp(inet_ntoa(host_address.sin_addr), list.c_str()) == 0){
			in.close();
			return true;
		}
	}
	in.close();
	return false;
}

void accept_connection(FD proxy){
	sockaddr_in host_address;
	bzero(&host_address, sizeof(host_address));
    socklen_t length = sizeof(host_address);
	while(1){
		int host = accept(proxy, (sockaddr*)&host_address, &length);
		if(host < 0){
			syslog(LOG_ERR, "accept error\n");
			continue;
		}/*
		if(inBlackList(host_address)){
			close(host);
			syslog(LOG_INFO, "Throw a package\n");
			continue;
		}*/
		syslog(LOG_INFO, "Get a socket\n");
		fcntl(host, F_SETFL, fcntl(host, F_GETFL) | O_NONBLOCK);
		Proxy p(host);
		//std::thread th(func, host);
		//th.detach();
	}
}

int main(int argc, char* argv[]){
	int err;
	openlog(argv[0], LOG_PID | LOG_NDELAY, LOG_LOCAL0);	//notice: add (signal_name.*  log_pos) to /etc/rsyslog.conf
	//closelog();
	//setlogmask(LOG_DEBUG);
	syslog(LOG_DEBUG, "test log1\n");
	FD proxy = Proxy::create_local_socket("0.0.0.0", 12345);
	if(proxy < 0){
		syslog(LOG_ERR, "Proxy create error\n");
		return 0;
	}
    err = listen(proxy, 16);	
    if(err < 0){
		syslog(LOG_ERR, "Listen error\n");
		close(proxy);
		return 0;
	}
	th_pool.submit(Proxy::ioctl);
	accept_connection(proxy);
    close(proxy);
    return 0;
}