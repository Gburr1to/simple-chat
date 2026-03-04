cmake_minimum_required(VERSION 3.10)
project(SimpleChat)


set(CMAKE_CXX_STANDARD 20)

# Iskanje Boost knjižnice 
find_package(Boost REQUIRED COMPONENTS system thread)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    

    add_executable(server server.cpp)
    target_link_libraries(server ${Boost_LIBRARIES} Threads::Threads)


    add_executable(client client.cpp)
    target_link_libraries(client ${Boost_LIBRARIES} Threads::Threads)
    
    message(STATUS "Boost found and targets configured.")
else()
    message(FATAL_ERROR "Boost library not found. Please install it.")
endif()
