#pragma once

#include <string>
#include <netinet/in.h>

class WebServer {
public:
    WebServer(const std::string &proxyPass, int port);
    ~WebServer();
    void start();

private:
    static bool         _running;
    int                 _serverSocket;
    std::string         _proxyPass;
    struct sockaddr_in  _serverAddr;

    int     createServerSocket(int port);
    void    handleClient(int clientSocket);
    void    setServerSocketFlags(int socket);
};
