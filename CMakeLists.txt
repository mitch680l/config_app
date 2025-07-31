cmake_minimum_required(VERSION 3.16)
project(ConfigurationGUI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6 components (without SerialPort)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Set up Qt6
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Create executable
add_executable(ConfigGUI
    main.cpp
    mainwindow.h
    mainwindow.cpp
    serialport.h
    serialport.cpp
)

# Link Qt6 libraries
target_link_libraries(ConfigGUI
    Qt6::Core
    Qt6::Widgets
)

# Windows-specific settings
if(WIN32)
    set_target_properties(ConfigGUI PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# Copy Qt DLLs on Windows (for deployment)
if(WIN32)
    add_custom_command(TARGET ConfigGUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt6::Core>
        $<TARGET_FILE_DIR:ConfigGUI>
    )
    add_custom_command(TARGET ConfigGUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt6::Widgets>
        $<TARGET_FILE_DIR:ConfigGUI>
    )
endif() 