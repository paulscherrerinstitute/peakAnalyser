#include <string.h>

#include <string>
#include <vector>

#include "HTTPCommon.h"

/*
 * URL
 *
 */

URL::URL(const std::string& url)
    : scheme("http://"), host("127.0.0.1"), port(80), path("/")
{
    size_t address_begin, pos;

    /* Extract <scheme>:// prefix */
    pos = url.find("://");
    if (pos != std::string::npos) {
        scheme = url.substr(0, pos + 3);
        address_begin = scheme.size();
    } else {
        address_begin = 0;
    }
    /* Separate <address>/<path> by "/" */
    pos = url.find('/', address_begin);
    std::string address;
    if (pos == std::string::npos) {
        address = url.substr(address_begin);
    } else {
        address = url.substr(address_begin, pos - address_begin);
        path = url.substr(pos);
    }
    
    /* Separate <host>:<port> by ":" */
    pos = address.find(':');
    if (pos == std::string::npos) {
        host = address;
    } else {
        host = address.substr(0, pos);
        port = std::stoi(address.substr(pos+1));
    }
}

/*
 * Helper functions
 *
 */

/*! If header exists return its value,
 * otherwise return the default value.
 */
std::string getHeaderValue(
        std::map<std::string, std::string>headers,
        std::string header,
        std::string defaultValue/* "" */)
{
    auto search = headers.find(header);
    if (search == headers.end())
        return defaultValue;
    return search->second;
}

/* Parse header text to a map */
bool parseHeaderLine (std::string header, std::map<std::string, std::string>& headers)
{
    size_t n = header.find_first_of(':');
    if (n == std::string::npos)
        return false;
    std::string key = header.substr(0, n);

    // strip off leading/trailing white spaces
    size_t d1 = header.find_first_not_of(" \t\r\n", n+1);
    size_t d2 = header.find_last_not_of(" \t\r\n");
    std::string value = header.substr(d1, d2+1-d1);

    headers[key] = value;

    return true;
}

/* Read a single line from socket */
bool readLine (SOCKET s, std::string &line)
{
    const char *functionName = "readLine";

    line.clear();
    while (true) {
        char b;
        int n = recv(s, &b, 1, 0);
        if (n < 0) {
            char error[MAX_BUF_SIZE];
            epicsSocketConvertErrnoToString(error, sizeof(error));
            ERR_ARGS("recv error %d:%s\n", SOCKERRNO, error);
            return false;
        } else if (n == 0) {
            if (line.length() == 0)
                return false;
            else
                return true;
        } else {
            line.push_back(b);
            if (b == '\n')
                break;
        }
    }

    return true;
}

/*! Read HTTP response content until connection is closed */
bool readIndefiniteContent(SOCKET s, std::string &content)
{
    char buf[4096];
    while (true) {
        int received = recv(s, buf, sizeof(buf), 0);
        if (received < 0) {
            return false;
        } else if (received == 0) {
            return true;
        } else {
            content.append(buf, received);
        }
    }
}

/*! Read HTTP response content with a specified length */
bool readFixedContent(SOCKET s, std::string &content, size_t length)
{
    std::vector<char> buf(length);
    char *p = buf.data();
    size_t bufSize = length;
    while (bufSize) {
        int received = recv(s, p, (int)bufSize, 0);
        if (received <= 0) {
            return false;
        }
        p += received;
        bufSize -= received;
    }
    content.append(buf.data(), length);

    return true;
}

/*! Read HTTP response chunked content */
bool readChunkedContent(SOCKET s, std::string &content)
{
    while (true) {
        // chunk size
        std::string lengthLine;
        if (!readLine(s, lengthLine))
            return false;
        size_t length = std::strtoul(lengthLine.c_str(), NULL, 16);

        // chunk data
        if (length) {
            std::string chunk;
            if (!readFixedContent(s, chunk, length))
                return false;
            content.append(chunk);
        }

        // end of chunk
        std::string line;
        if (!readLine(s, line) || line != "\r\n")
            return false;

        // end of content
        if (length == 0)
            return true;
    }
}

/* Send given data over socket */
bool write(SOCKET s, const std::string& data)
{
    size_t remaining = data.size();
    const char *p = data.c_str();

    while (remaining) {
        int written = send(s, p, (int)remaining, 0);
        if (written < 0)
            return false;
        remaining -= written;
        p += written;
    }
    return true;
}