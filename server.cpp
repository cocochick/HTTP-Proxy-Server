#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <cstring>
#include <assert.h>
#include <iostream>
#include <climits>
#include <netdb.h>
#include "http.h"
#include <iostream>
#include <string>
#include <thread>
#include <future>
#include <chrono>
#include <unordered_map>
#include <set>
#include <vector>
#include <condition_variable>
#include <mutex>
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


FD create_local_socket(const char* address, int port){
    FD fd = socket(PF_INET, SOCK_STREAM, 0);
	if(fd < 0){
		printf("socket create error\n");
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
		printf("bind error\n");
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
			printf("accept error\n");
			continue;
		}
		printf("GET a socket\n");
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
		printf("socket create error\n");
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
		//printf("website is : %s\n", website);
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
				//printf("get a ip, port is %d\n", atoi(website));
			}
		}
		else{
			hostent* serverinfo = gethostbyname(website);
			if(serverinfo->h_addrtype == AF_INET6){
				get_fd.set_value(-1);
				printf("IPV6 not allowed\n");
				return -1;
			}
			printf("DNS解析结果:%s\n", inet_ntoa(*(in_addr*)serverinfo->h_addr_list[0]));
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
		printf("connect error\n");
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
				printf("send error\n");
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

int main(){
	int err;
	FD proxy = create_local_socket("0.0.0.0", 12345);
	if(proxy < 0){
		printf("quit\n");
		return 0;
	}
    err = listen(proxy, 8);
    if(err < 0){
		printf("listen error\n");
		close(proxy);
		return 0;
	}
	accept_connection(proxy);
    close(proxy);
    return 0;
}

void epoll_func(FD epollfd, std::future<int>& isStop, std::promise<int>& command){
	epoll_event events[MAX_SIZE];
	auto begin = std::chrono::high_resolution_clock::now();
	while(1s > std::chrono::high_resolution_clock::now() - begin && isStop.wait_for(1us) == std::future_status::timeout){
		int ret = epoll_wait(epollfd, events, MAX_SIZE, 1);
		if(ret < 0){
			printf("epoll_wait error\n");
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
				else if(errno == 104){	//104 means Connection closed by peer
					break;
				}
				else if(errno == 110){	//110 means Connection timed out
					break;
				}
				else{
					printf("other error, no is %d, buffer length %d\n", errno, length);
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
			else if(errno == 104){	//104 means Connection closed by peer
				break;
			}
			else if(errno == 110){	//110 means Connection timed out
				break;
			}
			else{
				printf("other error, no is %d", errno);
				break;
			}	
		}
        if(data_read == 0){
            printf("connection break\n");
            break;
        }
        end += length;
		buffer[end] = '\0';
        bzero(origin, BUFFER_SIZE);
		char website[1024] = {'\0'};
		++test;
        HTTP_CODE result = parse_context(buffer, origin, website, begin, end, checkstate, linepos);
        if(result == NO_REQUEST){
            printf("A NO_REQUEST, context is:%s\n", origin);
			continue;
        }
        else if(result == GET_REQUEST){
            //send(remote, msg, strlen(msg), 0);
            //printf("msg send success\n");
			if(network_bridge[host].find(std::string(website)) != network_bridge[host].end()){
				//printf("A known location, context is\n%s\n", origin);
				char* file = origin;
				while(*file != '\0'){
					int temp1 = send(network_bridge[host][std::string(website)], file, strlen(file), 0);
					if(temp1 == -1){
						if(errno != EINTR){
							printf("send1 error, reason is %d\n", errno);
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
            printf("UNKNWON\n");
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

