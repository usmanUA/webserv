#include "WebServer.hpp"
#include "ScopedSocket.hpp"
#include "WebErrors.hpp"
#include <algorithm>
#include <csignal>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "Response.hpp"
#include "Request.hpp"

bool WebServer::_running = true;

// CONNECT SOCKETS ALSO TO PROXY SERVERS BEFORE STARTING THE SERVER

WebServer::WebServer(WebParser &parser)
    : _epollFd(-1), _parser(parser), _events(MAX_EVENTS)
{
    try
    {
        _serverSockets = createServerSockets(parser.getServers());
        resolveProxyAddresses(parser.getServers());
        _epollFd = epoll_create(1);
        if (_epollFd == -1)
            throw WebErrors::ServerException("Error creating epoll instance");
        for (const auto& serverSocket : _serverSockets)
            epollController(serverSocket.get(), EPOLL_CTL_ADD, EPOLLIN);
    }
    catch (const std::exception& e)
    {
        for (auto& entry : _proxyInfoMap)
        {
            if (entry.second)
            {
                freeaddrinfo(entry.second);
            }
        }
        if (_epollFd != -1) close(_epollFd);
        throw;
    }
}

void WebServer::resolveProxyAddresses(const std::vector<Server>& server_confs)
{
    for (const auto& server : server_confs)
    {
        for (const auto& location : server.locations)
        {
            if (location.type == PROXY)
            {
                std::string proxyHost;
                std::string proxyPort;

                size_t colonPos = location.target.rfind(':');
                if (colonPos != std::string::npos)
                {
                    proxyHost = location.target.substr(0, colonPos);
                    proxyPort = location.target.substr(colonPos + 1);
                }
                else
                {
                    proxyHost = location.target;
                    proxyPort = std::to_string(server.port);
                }

                std::string key = proxyHost + ":" + proxyPort;
                if (_proxyInfoMap.find(key) == _proxyInfoMap.end())
                {
                    addrinfo hints{};
                    hints.ai_family = AF_UNSPEC;
                    hints.ai_socktype = SOCK_STREAM;

                    addrinfo* proxyInfo = nullptr;
                    int status = getaddrinfo(proxyHost.c_str(), proxyPort.c_str(), &hints, &proxyInfo);
                    if (status != 0)
                    {
                        throw WebErrors::ProxyException("Error resolving proxy address: " 
                            + std::string(gai_strerror(status)));
                    }
                    _proxyInfoMap[key] = proxyInfo;
                }
            }
        }
    }
}



WebServer::~WebServer()
{
    for (auto& entry : _proxyInfoMap)
    {
        if (entry.second)
        {
            freeaddrinfo(entry.second);
            entry.second = nullptr;
        }
    }
    if (_epollFd != -1)
    {
        close(_epollFd);
        _epollFd = -1;
    }
}


std::vector<ServerSocket> WebServer::createServerSockets(const std::vector<Server> &server_confs)
{
    int opt = 1;
    try {
        std::vector<ServerSocket> serverSockets;

        for (const auto& server_conf : server_confs)
        {
            ServerSocket serverSocket(socket(AF_INET, SOCK_STREAM, 0), server_conf);

            if (serverSocket.get() < 0 ||
                setsockopt(serverSocket.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
                throw WebErrors::ServerException("Error opening or configuring server socket for server on port " + std::to_string(server_conf.port));

            std::memset(&_serverAddr, 0, sizeof(_serverAddr));
            _serverAddr.sin_family = AF_INET;
            _serverAddr.sin_addr.s_addr = INADDR_ANY;
            _serverAddr.sin_port = htons(server_conf.port);

            if (bind(serverSocket.get(), (struct sockaddr *)&_serverAddr, sizeof(_serverAddr)) < 0)
                throw WebErrors::ServerException("Error binding server socket on port " + std::to_string(server_conf.port));

            if (listen(serverSocket.get(), SOMAXCONN) < 0)
                throw WebErrors::ServerException("Error listening on server socket on port " + std::to_string(server_conf.port));
   
            serverSockets.push_back(std::move(serverSocket));
        }
        return serverSockets;
    } catch (const std::exception& e)
    {
        throw;
    }
}


void WebServer::epollController(int clientSocket, int operation, uint32_t events)
{
    struct epoll_event  event;

    std::memset(&event, 0, sizeof(event));
    event.data.fd = clientSocket;
    event.events = events;
    if (epoll_ctl(_epollFd, operation, clientSocket, &event) == -1)
    {
        close(clientSocket);
        throw WebErrors::ServerException("Error changing epoll state" + std::string(strerror(errno)));
    }
    if (operation == EPOLL_CTL_DEL)
    {
        close(clientSocket);
        clientSocket = -1;
    }
}

void WebServer::acceptAddClient(int serverSocketFd)
{
    struct sockaddr_in  clientAddr;
    socklen_t           clientLen = sizeof(clientAddr);
    ScopedSocket        clientSocket(accept(serverSocketFd, (struct sockaddr *)&clientAddr, &clientLen));

    if (clientSocket.get() < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        throw WebErrors::ServerException("Error accepting client connection");

    epollController(clientSocket.get(), EPOLL_CTL_ADD, EPOLLIN);
    clientSocket.release();
}


std::string WebServer::getBoundary(const std::string &request)
{
    size_t start = request.find("boundary=");
    if (start == std::string::npos) return "";

    start += 9; // Length of "boundary="
    size_t end = request.find("\r\n", start);
    return (end == std::string::npos) ? request.substr(start) : request.substr(start, end - start);
}

int WebServer::getRequestTotalLength(const std::string &request)
{
    size_t contentLengthPos = request.find("Content-Length: ");
    if (contentLengthPos == std::string::npos) return request.length();

    contentLengthPos += 16; // Length of "Content-Length: "
    size_t end = request.find("\r\n", contentLengthPos);
    const int contentLength = std::stoi(request.substr(contentLengthPos, end - contentLengthPos));

    std::string boundary = getBoundary(request);
    if (!boundary.empty())
    {
        size_t boundaryPos = request.rfind("--" + boundary);
        if (boundaryPos != std::string::npos)
            return boundaryPos + boundary.length() + 4; // +4 for "--\r\n"
    }
    return contentLength;
}

void WebServer::handleIncomingData(int clientSocket)
{
    try {
        char        buffer[1024];
        std::string totalRequest;
        int         totalBytes = -1;
        int         bytesRead = 0;

        while (totalBytes == -1 || totalRequest.length() < static_cast<size_t>(totalBytes))
        {
            bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
            if (bytesRead <= 0)
            {
                if (bytesRead == 0 || (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                WebErrors::printerror("Error reading from client socket");
                break;
            }

            totalRequest.append(buffer, bytesRead);

            if (totalBytes == -1)
                totalBytes = getRequestTotalLength(totalRequest);
        }
        if (!totalRequest.empty())
        {
            _requestMap[clientSocket] = Request(totalRequest, _parser.getServers(), _proxyInfoMap);
            epollController(clientSocket, EPOLL_CTL_MOD, EPOLLOUT);
        }
    }
    catch (const std::exception &e)
    {
        WebErrors::printerror(e.what());
        epollController(clientSocket, EPOLL_CTL_DEL, 0);
    }
}

void WebServer::handleOutgoingData(int clientSocket) {
    try {
        auto it = _requestMap.find(clientSocket);
        if (it != _requestMap.end())
        {
            const Request &request = it->second;
            addrinfo*     proxyInfo = request.getProxyInfo();

            Response    response(proxyInfo);
            std::string responseContent = response.generate(request);

            std::cout << "Sending response to client:\n" << responseContent << std::endl;
            int bytesSent = send(clientSocket, responseContent.c_str(), responseContent.length(), 0);
            if (bytesSent == -1)
            {
                WebErrors::printerror("Error sending response to client");
                epollController(clientSocket, EPOLL_CTL_DEL, 0);
            }
            else
                epollController(clientSocket, EPOLL_CTL_DEL, 0);
        }
        _requestMap.erase(it);
    }
    catch (const std::exception &e) {
        WebErrors::printerror(e.what());
        epollController(clientSocket, EPOLL_CTL_DEL, 0);
    }
}


void WebServer::handleEvents(int eventCount)
{
    auto getCorrectServerSocket = [this](int fd) -> bool {
        return std::any_of(_serverSockets.begin(), _serverSockets.end(),
                           [fd](const ScopedSocket& socket) { return socket.get() == fd; });
    };

    for (int i = 0; i < eventCount; ++i)
    {
        const int fd = _events[i].data.fd;

        if (getCorrectServerSocket(fd))
        {
            fcntl(fd, F_SETFL, O_NONBLOCK | FD_CLOEXEC);
            acceptAddClient(fd);
        }
        else
        {
            if (_events[i].events & EPOLLIN)
                handleIncomingData(fd);
            else if (_events[i].events & EPOLLOUT)
                handleOutgoingData(fd);
        }
    }
}




void WebServer::start()
{
    std::cout << "Server is running. Press Ctrl+C to stop.\n";
    std::signal(SIGINT, [](int signum) { (void)signum; WebServer::_running = false; });

    while (_running)
    {
        int eventCount = epoll_wait(_epollFd, _events.data(), _events.size(), -1);
        if (eventCount == -1)
        {
            if (errno == EINTR) continue;
            WebErrors::printerror("Epoll wait error");
            continue;
        }
        handleEvents(eventCount);
    }
    std::cout << "Server stopped.\n";
}
