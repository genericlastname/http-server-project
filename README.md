# HTTP Server Project
## Authors: Kyle Brown, Blair Newman
#### CSC 375 - Prof. Sazegarnejad
A simple HTTP server written in C.

## Features
- Serves HTML files and automatically loads index.html if found
- Generates a directory listing if no index.html is found
- Serves images and text files
- Supports multiple concurrent requests

## Usage
To use the server normally run
```
./httpserver -files [FILES] -port [PORT]
```
where [FILES] is the path to the root directory of a website and [PORT] is the port the server will use (typically 8000 for testing).

The server can also be used in proxy mode
```
./httpserver -proxy [HOSTNAME:PORT]
```
where [HOSTNAME:PORT] can be some web address like `morehouse.edu:80`
