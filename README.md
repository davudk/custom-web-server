
# Custom Web Server

This is a basic web server written in C. This was made for assignment 1 of the grad class "Internet & Higher Layer Protocols" (CS656 at NJIT).

This program demonstrates the usage of the C Posix library to achieve web communication using stream-based sockets. The incoming HTTP request is parsed to ensure correctness, and a basic response is sent back. The response is a basic HTML page containing the original request for the user to see. The response page contains the host, port, and path of the requested URI (within the HTTP header's status line). The IP address of the host is also displayed. This can be demonstrated by running the server and setting the server as the proxy (on another device, or on a VM).

Upto 31 simultaneous connections are allowed. Connections can be kept alive with `Connection: keep-alive` or by default default if using HTTP version 1.1.

Find there specification [here](https://tools.ietf.org/html/rfc2616). (RFC2616)
