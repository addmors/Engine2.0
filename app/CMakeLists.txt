# The candy executable: the 2017 scene resurrected, one milestone at a time.

add_executable(candy
    src/main.cpp
)

target_link_libraries(candy PRIVATE
    engine
    candy_warnings
)

# Assets (resources/, assets/) are referenced relative to the repository root.
set_target_properties(candy PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
