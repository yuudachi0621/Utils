#include "WebServer_IOCP.h"
#include "Tests.h"

int TestFunction()
{
    return tests::RunTests();
}
void RunIOCPServer()
{
    WebServer_IOCP server(
        1315,
        false,
        3306,
        "remote",
        "remote",
        "yourdb",
        1,
        8);
    server.Start();
    while (1) {}
}
int main(int argc, char* argv[])
{
    if (argc > 1 && std::strcmp(argv[1], "--test") == 0)
        return TestFunction();

    RunIOCPServer();
    return 0;
}
