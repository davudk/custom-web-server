
# Custom Web Server

This is a basic web server written in C. This was made for assignment 1 of the grad class "Internet & Higher Layer Protocols" (CS656 at NJIT).

This program demonstrates the usage of the C Posix library to achieve web communication using stream-based sockets. The incoming HTTP request is parsed to ensure correctness, and a basic response is sent back. The response is a basic HTML page containing the original request for the user to see.

The connection is kept alive or closed, depening on the incoming request.

**Note:** I've tried not to stray too far from the specification, mentioned here: [RFC2616](https://tools.ietf.org/html/rfc2616).