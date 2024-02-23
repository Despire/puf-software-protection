set(LLVM_PUF_PLUGINS
        PufParser
)

set(PufParser_SOURCES PufParser.cpp)

foreach (plugin ${LLVM_PUF_PLUGINS})
    add_library(${plugin} SHARED ${${plugin}_SOURCES})
    target_include_directories(${plugin} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
    target_link_libraries(${plugin} "$<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>")
endforeach ()
