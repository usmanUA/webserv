#include "WebErrors.hpp"
#include <cstring>

namespace WebErrors
{
    BaseException::BaseException(const std::string &message) : _message(message) {}

    const char* BaseException::what() const noexcept
    {
        return _message.c_str();
    }

    FileOpenException::FileOpenException(const std::string &filename)
        : BaseException("Error opening config file: " + filename) {}

    int printerror(const std::string &e)
    {
        if (errno != 0)
            std::cerr << e << ": " << strerror(errno) << std::endl;
        else
            std::cerr << e << std::endl;
        return -1;
    }
}
