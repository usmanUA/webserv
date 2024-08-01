#include "WebServer.hpp"
#include "RequestHandler/RequestHandler.hpp"
#include "ScopedSocket.hpp"
#include "WebErrors.hpp"
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

bool WebServer::_running = true;
RequestHandler WebServer::_requestHandler(-1);

WebServer::WebServer(WebParser &parser, int port)
    : _serverSocket(createServerSocket(port)), _parser(parser)
{
    _epollFd = epoll_create(1);
    if (_epollFd == -1)
        throw WebErrors::ServerException("Error creating epoll instance");

    epoll_event event;
    event.events = EPOLLIN | EPOLLET | EPOLLEXCLUSIVE;
    event.data.fd = _serverSocket.get();
    if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, _serverSocket.get(), &event) == -1)
        throw WebErrors::ServerException("Error adding server socket to epoll");
}

WebServer::~WebServer() { close(_epollFd); }

int WebServer::createServerSocket(int port)
{
    int             opt = 1;
    ScopedSocket    serverSocket(socket(AF_INET, SOCK_STREAM, 0));

    if (serverSocket.get() < 0 || 
        setsockopt(serverSocket.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        throw WebErrors::ServerException("Error opening or configuring server socket");

    std::memset(&_serverAddr, 0, sizeof(_serverAddr));
    _serverAddr.sin_family = AF_INET;
    _serverAddr.sin_addr.s_addr = INADDR_ANY;
    _serverAddr.sin_port = htons(port);
    if (bind(serverSocket.get(), (struct sockaddr *)&_serverAddr, sizeof(_serverAddr)) < 0)
        throw WebErrors::ServerException("Error binding server socket");

    if (listen(serverSocket.get(), 1000) < 0)
        throw WebErrors::ServerException("Error listening on server socket");

    return serverSocket.release();
}

void WebServer::removeClientSocket(int clientSocket)
{
    epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientSocket, nullptr);
    close(clientSocket);
}

// ACCEPT THE CLIENT CONNECTION AND ADD IT TO THE EPOLL POOL
void WebServer::acceptAddClient()
{
    struct sockaddr_in  clientAddr;
    epoll_event         event;
    socklen_t           clientLen = sizeof(clientAddr);
    ScopedSocket        clientSocket(accept(_serverSocket.get(), (struct sockaddr *)&clientAddr, &clientLen));

    if (clientSocket.get() < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        throw WebErrors::ServerException("Error accepting client connection");

    event.events = EPOLLIN | EPOLLET | EPOLLEXCLUSIVE;
    event.data.fd = clientSocket.get();
    if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientSocket.get(), &event) == -1)
        throw WebErrors::ServerException("Error adding client socket to epoll");

    clientSocket.release();
}

void WebServer::handleIncomingData(int clientSocket)
{
    try
    {
        if (clientSocket == _serverSocket.get()) // New clients fd matches the server socket fd in first event then added to the pool
                acceptAddClient();
        else
            _requestHandler.handleRequest(clientSocket); // Handle existing user request
    }
    catch (const WebErrors::ClientException &e)
    {
        std::cerr << e.what() << std::endl;
    }
}

//void WebServer::handleOutgoingData(int clientSocket) { TO DO }

void WebServer::handleEvents(int eventCount)
{
    for (int i = 0; i < eventCount; ++i)
    {
        if (_events[i].events & EPOLLIN) // EPOLLIN is set when there is data to read from client socket (recv())
        {
            handleIncomingData(_events[i].data.fd);
        }
        /*else if (_events[i].events & EPOLLOUT) // EPOLLOUT is set when the socket is ready to send data to the client (send())
        {
            handleOutgoingData(_events[i].data.fd);
        }*/
    }
}

void WebServer::start()
{
    std::cout << "Server is running. Press Ctrl+C to stop.\n";
    std::signal(SIGINT, [](int signum) { (void)signum; WebServer::_running = false; });

    while (_running)
    {
        int eventCount = epoll_wait(_epollFd, _events, MAX_EVENTS, -1);
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