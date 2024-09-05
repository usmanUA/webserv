#pragma once
#include "Request.hpp"
#include <string>

class ErrorHandler
{
public:
    ErrorHandler(const Request& request);
    void handleError(std::string& response) const;

private:
    const Request& _request;

    std::string getErrorMessage(int errorCode) const;
    std::string getErrorPage(int errorCode) const;
    void        readFileContent(const std::string& path, std::string& content) const;
};
