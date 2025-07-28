#include "WebServer_IOCP.h"
#include <algorithm>

int main()
{
    WebServer_IOCP server(
        1315,
        false,
        3306,
        "remote",
        "remote",
        "yourdb",
        1,
        1);
    server.Start();
    return 0;
}