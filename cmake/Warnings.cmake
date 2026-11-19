# Zero warnings at /W4. Link cortex_warnings into every Cortex target; warnings are errors.

add_library(cortex_warnings INTERFACE)

if(MSVC)
    target_compile_options(cortex_warnings INTERFACE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /MP)
else()
    target_compile_options(cortex_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()
