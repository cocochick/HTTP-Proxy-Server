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
#include <vector>
#include <fstream>
#include "http.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif


constexpr int BUFFER_SIZE = 10485760;
constexpr int MAX_SIZE = 65535;
using FD = int;
using namespace std::chrono_literals;
//static FD epollfd;

void func(FD);
int i = 10086;
std::unordered_map<FD, std::unordered_map<std::string, FD>> network_bridge;
std::unordered_map<std::string, in_addr_t> dns;

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

bool inWhiteList(std::string server_address){
	std::ifstream in("./whitelist.txt");
	std::string list;
	while(in >> list){
		if(server_address.find(list) != std::string::npos){
			in.close();
			return true;
		}
	}
	in.close();
	return false;
}


FD create_local_socket(const char* address, int port){
    FD fd = socket(PF_INET, SOCK_STREAM, 0);
	if(fd < 0){
		syslog(LOG_ERR | LOG_LOCAL0, "socket create error\n");
		return -1;
	}
	int reuse = 1;
    setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    sockaddr_in local_address;
    bzero(&local_address, sizeof(local_address));
    local_address.sin_family = AF_INET;
    inet_pton(AF_INET, address, &local_address.sin_addr);
	local_address.sin_port = htons(port);
	int ret = bind(fd, (sockaddr*)&local_address, sizeof(local_address));
	if(ret < 0){
		syslog(LOG_ERR | LOG_LOCAL0, "Bind error\n");
		close(fd);
		return -1;
	}
	return fd;
}

void accept_connection(FD proxy){
	sockaddr_in host_address;
	bzero(&host_address, sizeof(host_address));
    socklen_t length = sizeof(host_address);
	while(1){
		int host = accept(proxy, (sockaddr*)&host_address, &length);
		if(host < 0){
			syslog(LOG_ERR | LOG_LOCAL0, "accept error\n");
			continue;
		}
		if(inBlackList(host_address)){
			close(host);
			syslog(LOG_INFO | LOG_LOCAL0, "Throw a package\n");
			continue;
		}
		syslog(LOG_INFO | LOG_LOCAL0, "Get a socket\n");
		fcntl(host, F_SETFL, fcntl(host, F_GETFL) | O_NONBLOCK);
		network_bridge[host];
		std::thread th(func, host);
		th.detach();
	}
}

int create_connection(FD host, FD epollfd, char* origin, char* website, std::future<int> isStop, std::promise<int>& command, std::promise<FD>& get_fd){
	FD server = create_local_socket("0.0.0.0", i++);
	char origin_website[1024];
	strcpy(origin_website, website);
	epoll_event event;
	event.events = EPOLLRDHUP | EPOLLET | EPOLLIN | EPOLLOUT;
	event.data.fd = server;
	if(server < 0){
		get_fd.set_value(-1);
		syslog(LOG_ERR | LOG_LOCAL0, "Socket create error\n");
		return -1;
	}
	sockaddr_in server_address;
	bzero(&server_address, sizeof(server_address));
    socklen_t length = sizeof(server_address);
	server_address.sin_family = AF_INET;
	//todo 判断域名是否为ip地址
	network_bridge[host][std::string(website)] = server;
	if(strchr(website, ':') == nullptr && dns.find(std::string(website)) != dns.end()){
		server_address.sin_addr.s_addr = dns[std::string(website)];
	}
	else{
		if(strchr(website, ':') != nullptr){
			if(strstr(website, ":443") != nullptr){
				*(strchr(website, ':')) = '\0';
				hostent* serverinfo = gethostbyname(website);
				if(serverinfo == nullptr){
					inet_pton(AF_INET, website, &server_address.sin_addr);
				}
				else{
					server_address.sin_addr.s_addr = inet_addr(inet_ntoa(*(in_addr*)serverinfo->h_addr_list[0]));
				}
				website += strlen(website) + 1;
				server_address.sin_port = htons(atoi(website));
			}
			else{
				*(strchr(website, ':')) = '\0';
				inet_pton(AF_INET, website, &server_address.sin_addr);
				website += strlen(website) + 1;
				server_address.sin_port = htons(atoi(website));
			}
		}
		else{
			hostent* serverinfo = gethostbyname(website);
			if(serverinfo->h_addrtype == AF_INET6){
				get_fd.set_value(-1);
				syslog(LOG_ERR | LOG_LOCAL0, "Get an IPV6 package\n");
				return -1;
			}
			//printf("DNS解析结果:%s\n", inet_ntoa(*(in_addr*)serverinfo->h_addr_list[0]));
			//inet_pton(AF_INET, inet_ntop(AF_INET, host->h_addr_list[0],str, sizeof(str)), &server_address.sin_addr);
			server_address.sin_addr.s_addr = inet_addr(inet_ntoa(*(in_addr*)serverinfo->h_addr_list[0]));
			server_address.sin_port = htons(80);
		}
	}
/*	47.102.114.67的测试
    inet_pton(AF_INET, "47.102.114.67", &server_address.sin_addr);
    server_address.sin_port = htons(9000);
*/
	int err = connect(server, (sockaddr*)&server_address, length);
	if(err < 0){
		get_fd.set_value(-1);
		network_bridge[host].erase(std::string{origin_website});
		syslog(LOG_ERR | LOG_LOCAL0, "Connect error\n");
		close(server);
		return -1;
	}
	fcntl(server, F_SETFL, fcntl(server, F_GETFL) | O_NONBLOCK);
	err = epoll_ctl(epollfd, EPOLL_CTL_ADD, server, &event);
	assert(err != -1);
	get_fd.set_value(server);
	//fcntl(server, F_SETFL, fcntl(server, F_GETFL) | O_NONBLOCK);
	int pipefd[2];
	int ret = pipe(pipefd);
	assert(ret != -1);
	//assert(ret != -1);
	char* file = origin;
	int temp;
	while(*file != '\0'){
		temp = send(server, file, strlen(file), 0);
		if(temp == -1){
			if(errno != EINTR){
				syslog(LOG_ERR | LOG_LOCAL0, "Send error\n");
				close(server);
				return -1;
			}
			else{
				break;
			}
		}
		else if(temp == 0){
			close(server);
			return -1;
		}
		file += temp;
	}
	char* buffer = new char[BUFFER_SIZE];
	char* buffer1 =new char[BUFFER_SIZE];
	while(isStop.valid() != false && isStop.wait_for(1us) == std::future_status::timeout){
		ret = splice(server, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
		//printf("errno is %s\n", strerror(errno));
		//assert(ret != -1);
		ret = splice(pipefd[0], NULL, host, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
	}
	close(server);
	network_bridge[host].erase(std::string(origin_website));
	delete[] buffer;
	delete[] buffer1;
	return 0;
}

int main(int argc, char* argv[]){
	int err;
	openlog(argv[0], LOG_PID | LOG_ODELAY, LOG_USER);	//notice: add (signal_name.*  log_pos) to /etc/rsyslog.conf
	setlogmask(LOG_DEBUG);
	FD proxy = create_local_socket("0.0.0.0", 12345);
	if(proxy < 0){
		syslog(LOG_ERR | LOG_LOCAL0, "Proxy create error\n");
		return 0;
	}
    err = listen(proxy, 8);
    if(err < 0){
		syslog(LOG_ERR | LOG_LOCAL0, "Listen error\n");
		close(proxy);
		return 0;
	}
	accept_connection(proxy);
    close(proxy);
	closelog();
    return 0;
}

void epoll_func(FD epollfd, std::future<int>& isStop, std::promise<int>& command){
	epoll_event events[MAX_SIZE];
	auto begin = std::chrono::high_resolution_clock::now();
	while(1s > std::chrono::high_resolution_clock::now() - begin && isStop.wait_for(1us) == std::future_status::timeout){
		int ret = epoll_wait(epollfd, events, MAX_SIZE, 1);
		if(ret < 0){
			syslog(LOG_ERR | LOG_LOCAL0, "Epoll wait error\n");
			break;
		}
		else if(ret > 0){
			begin = std::chrono::high_resolution_clock::now();
		}
	}
	if(isStop.valid()){
		command.set_value(1);
	}
	return;
}


void func(FD host){
	FD epollfd = epoll_create(1);
	assert(epollfd != -1);
	char* buffer = new char[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
	std::vector<std::future<int>> thread_box;
	std::vector<std::promise<int>> message_box;
	std::promise<int> flag;
	std::future<int> isStop(flag.get_future());
	std::unordered_map<FD, int> pos;
	epoll_event event;
	event.events = EPOLLRDHUP | EPOLLET | EPOLLIN | EPOLLOUT;
	event.data.fd = host;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, host, &event);
	std::thread th(epoll_func, epollfd, ref(isStop), ref(flag));
	char* origin = new char[BUFFER_SIZE];
	int test = 0;
    while(isStop.wait_for(1us) == std::future_status::timeout){
        CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;
		int data_read = 0;
		int begin = 0;
		int end = 0;
		int linepos = 0;
		int length = 0;
		while(1){
			data_read = recv(host, buffer + length, BUFFER_SIZE - length, 0);
			if(data_read <= 0){
				if(errno == EAGAIN){
					break;
				}
				else if(errno == 104 || errno == 110){	//104 means Connection closed by peer
					break;
				}
				else{
					syslog(LOG_INFO | LOG_LOCAL0, "other error, no is %d, buffer length %d\n", errno, length);
					break;
				}	
			}
			length += data_read;
		}
        if(data_read < 0){
			if(errno == EAGAIN){
				if(length == 0){
					continue;
				}	
			}
			else{
				break;
			}
		}
        if(data_read == 0){
            syslog(LOG_INFO | LOG_LOCAL0, "Host close a conncet\n");
            break;
        }
        end += length;
		buffer[end] = '\0';
        bzero(origin, BUFFER_SIZE);
		char website[1024] = {'\0'};
		++test;
        HTTP_CODE result = parse_context(buffer, origin, website, begin, end, checkstate, linepos);
        if(result == NO_REQUEST){
            syslog(LOG_WARNING | LOG_LOCAL0, "Get a NO_REQUEST, context is:%s\n", origin);
			continue;
        }
        else if(result == GET_REQUEST){
            //send(remote, msg, strlen(msg), 0);
            //printf("msg send success\n");
			if(!inWhiteList(std::string{website})){
				delete[] buffer;
				delete[] origin;
				th.join();
				close(epollfd);
				close(host);
				return;
			}
			if(network_bridge[host].find(std::string(website)) != network_bridge[host].end()){
				//printf("A known location, context is\n%s\n", origin);
				char* file = origin;
				while(*file != '\0'){
					int temp1 = send(network_bridge[host][std::string(website)], file, strlen(file), 0);
					if(temp1 == -1){
						if(errno != EINTR){
							syslog(LOG_ERR | LOG_LOCAL0, "Send to server by known host error, reason is %s\n", strerror(errno));
						}
						break;
					}
					file += temp1;
				}
				if(*file != 0){
					continue;
				}
			}
			else{
				std::promise<int> message;
				std::future<int> isStop = message.get_future();
				std::promise<FD> server_fd;
				std::future<FD> get_server_fd = server_fd.get_future();
				std::future<int> res = std::async(std::launch::async, create_connection, host, epollfd, origin, website, std::move(isStop), ref(flag), ref(server_fd));
				FD server = get_server_fd.get();
				thread_box.push_back(std::move(res));
				message_box.push_back(std::move(message));
				if(server > 0){
					pos[server] = message_box.size() - 1;
				}
			}
            continue;
        }
        else{
            syslog(LOG_ERR | LOG_LOCAL0, "Package has a problem\n");
            break;
        }
    }
	for(int i = 0; i < thread_box.size(); ++i){
		if(thread_box[i].wait_for(1us) == std::future_status::timeout){
			message_box[i].set_value(1);
			thread_box[i].get();
		}
	}
	delete[] buffer;
	delete[] origin;
	th.join();
	close(epollfd);
	close(host);
}

