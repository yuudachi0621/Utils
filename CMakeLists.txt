# 最低 CMake 版本：3.16 已完整支持当前使用的目标属性与 Visual Studio 生成器。
cmake_minimum_required(VERSION 3.16)

# 工程信息：保持一致的项目名、版本号和 C++ 语言。
project(Utils VERSION 0.1.0 LANGUAGES CXX)

# 统一使用 C++17，并禁止编译器使用非标准扩展。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成 compile_commands.json，便于 clangd、VSCode 等工具进行代码分析。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 源码与第三方库所在目录。
set(SRC_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(THIRD_PARTY_DIR "${SRC_DIR}/3rd")

# MySQL Connector/C 是 sqlConnPool.cpp 的必需依赖。
# 如果安装在其他位置，可在配置时通过 -DMYSQL_ROOT=<路径> 指定。
set(MYSQL_ROOT "E:/Program Files (x86)/MySQL" CACHE PATH "MySQL Connector/C 安装目录")

find_path(
    MYSQL_INCLUDE_DIR
    NAMES mysql.h
    HINTS "${MYSQL_ROOT}"
    PATH_SUFFIXES include
)

find_library(
    MYSQL_LIBRARY
    NAMES libmysql mysqlclient
    HINTS "${MYSQL_ROOT}"
    PATH_SUFFIXES lib
)

# 查找 MySQL 动态库及其依赖。构建后复制到可执行程序目录，
# 这样直接点击运行或在 VS Code 中“运行但不调试”无需额外配置 PATH。
find_file(
    MYSQL_RUNTIME_LIBRARY
    NAMES libmysql.dll
    HINTS "${MYSQL_ROOT}"
    PATH_SUFFIXES lib bin
)

find_file(
    MYSQL_SSL_RUNTIME_LIBRARY
    NAMES libssl-3-x64.dll libssl-1_1-x64.dll
    HINTS "${MYSQL_ROOT}"
    PATH_SUFFIXES lib bin
)

find_file(
    MYSQL_CRYPTO_RUNTIME_LIBRARY
    NAMES libcrypto-3-x64.dll libcrypto-1_1-x64.dll
    HINTS "${MYSQL_ROOT}"
    PATH_SUFFIXES lib bin
)

set(MYSQL_RUNTIME_DLLS)
foreach(_mysql_runtime
        MYSQL_RUNTIME_LIBRARY
        MYSQL_SSL_RUNTIME_LIBRARY
        MYSQL_CRYPTO_RUNTIME_LIBRARY)
    if(${_mysql_runtime})
        list(APPEND MYSQL_RUNTIME_DLLS "${${_mysql_runtime}}")
    endif()
endforeach()

if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR
        "未找到 MySQL Connector/C。请通过 -DMYSQL_ROOT=<安装目录> 指定正确路径。")
endif()

# Utils 是可复用的静态库，不包含 main.cpp。
# 新增工具源文件时，将对应文件加入此列表即可。
set(UTILS_SOURCES
    "${THIRD_PARTY_DIR}/easyloggingpp/easylogging++.cc"

    "${SRC_DIR}/algorithm/Base64.cpp"
    "${SRC_DIR}/algorithm/EncryptMD5.cpp"
    "${SRC_DIR}/algorithm/URLEncode.cpp"

    "${SRC_DIR}/container/VariableBuffer.cpp"

    "${SRC_DIR}/file/FileUtil.cpp"

    "${SRC_DIR}/http/HttpConnect.cpp"
    "${SRC_DIR}/http/HttpRequest.cpp"
    "${SRC_DIR}/http/HttpResponse.cpp"
    "${SRC_DIR}/http/WebServer_IOCP.cpp"
    "${SRC_DIR}/http/webServer.cpp"

    "${SRC_DIR}/log/log.cpp"

    "${SRC_DIR}/pool/sqlConnPool.cpp"

    "${SRC_DIR}/tool/stringUtil.cpp"
)

# 创建静态库 Utils，对应原 Visual Studio 工程中的 Utils 项目。
add_library(Utils STATIC ${UTILS_SOURCES})

# 将项目 resources 目录注入运行时，避免使用旧的外部绝对路径。
target_compile_definitions(Utils PUBLIC
    UTILS_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/"
)

# 公开所有头文件搜索路径，使依赖 Utils 的可执行程序也能解析其头文件包含。
target_include_directories(Utils
    PUBLIC
        "${SRC_DIR}"
        "${SRC_DIR}/algorithm"
        "${SRC_DIR}/container"
        "${SRC_DIR}/file"
        "${SRC_DIR}/http"
        "${SRC_DIR}/log"
        "${SRC_DIR}/pool"
        "${SRC_DIR}/tool"

        "${THIRD_PARTY_DIR}/easyloggingpp"
        "${THIRD_PARTY_DIR}/ghc"
        "${THIRD_PARTY_DIR}/json"
    PRIVATE
        "${MYSQL_INCLUDE_DIR}"
)

# MySQL 链接信息属于 Utils 的使用要求，需要传递给最终可执行程序。
target_link_libraries(Utils PUBLIC "${MYSQL_LIBRARY}")

# IOCP 网络服务依赖 Winsock；Mswsock.lib 提供 AcceptEx 等扩展接口。
if(WIN32)
    target_link_libraries(Utils PUBLIC ws2_32 mswsock)
endif()

# 屏蔽 Visual Studio 对这些库使用的旧式 CRT/Winsock API 警告。
if(MSVC)
    target_compile_definitions(Utils PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
endif()

# main.cpp 只负责启动示例服务，作为独立可执行程序链接 Utils 静态库。
add_executable(Example
    "${SRC_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/Tests.cpp"
)
target_link_libraries(Example PRIVATE Utils)
target_include_directories(Example PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")

# 将 MySQL 运行库放在 exe 同目录，避免 VS Code 直接运行时出现 0xC0000135。
if(MYSQL_RUNTIME_DLLS)
    add_custom_command(TARGET Example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${MYSQL_RUNTIME_DLLS}
                "$<TARGET_FILE_DIR:Example>"
        COMMENT "Copying MySQL runtime DLLs next to Example.exe")
else()
    message(WARNING "未找到 MySQL 运行库 DLL，Example.exe 可能无法直接运行。")
endif()
