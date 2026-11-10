# Shared warning settings, propagated as an INTERFACE target.
add_library(candy_warnings INTERFACE)

if(MSVC)
    target_compile_options(candy_warnings INTERFACE
        /W4
        /permissive-
        /utf-8
        /MP
        # Common pitfalls promoted even above /W4 defaults:
        /w14242 # conversion: possible loss of data
        /w14254 # operator conversion: possible loss of data
        /w14265 # class has virtual functions, but destructor is not virtual
        /w14296 # buffer overrun
        /w14640 # thread-unsafe static member initialization
    )
else()
    target_compile_options(candy_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()
