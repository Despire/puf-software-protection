set(LLVM_PUF_PLUGINS PufPatcher)

set(PufPatcher_SOURCES PufPatcher.cpp Checksum.cpp Crossover.cpp CtorDtor.cpp Dependencies.cpp)

foreach (plugin ${LLVM_PUF_PLUGINS})
    add_library(${plugin} SHARED ${${plugin}_SOURCES})
    target_include_directories(${plugin} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
    target_link_libraries(${plugin} "$<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>")
endforeach ()
