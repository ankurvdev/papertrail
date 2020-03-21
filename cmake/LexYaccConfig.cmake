
# Lex and Yacc
if(WIN32)
    SET(BISON_EXECUTABLE ${CMAKE_CURRENT_LIST_DIR}/winflexbison3-latest/win_bison.exe CACHE PATH "Bison executable")
    SET(FLEX_EXECUTABLE  ${CMAKE_CURRENT_LIST_DIR}/winflexbison3-latest/win_flex.exe CACHE PATH "Flex executable")
    SET(FLEX_INCLUDE     ${CMAKE_CURRENT_LIST_DIR}/winflexbison3-latest)
endif()

set(LEXYACC_CPP ${CMAKE_CURRENT_LIST_DIR}/LexYacc.cpp)

function(build_bison)
    set(builddir        ${PROJECT_BINARY_DIR}/tools/bison)
    set(srcdir          ${builddir}/bison-3.5)
    file(MAKE_DIRECTORY ${builddir})

    if (NOT EXISTS ${srcdir})
        file(DOWNLOAD http://ftp.gnu.org/gnu/bison/bison-3.5.tar.gz ${builddir}/bison.tar.gz)
        myexec(${builddir} tar zxf ${builddir}/bison.tar.gz)
    endif()

    if (NOT EXISTS ${srcdir}/Makefile)
        myexec(${srcdir} ./configure --prefix=${builddir})
    endif()

    if (NOT EXISTS ${builddir}/bin/bison)
        myexec(${srcdir} make install)
    endif()

    find_program(BISON_EXECUTABLE bison HINTS ${builddir}/bin)
endfunction()

function(find_or_create_lexyacc)
    if (EXISTS LEXYACC_BINARY)
        return()
    endif()
    set(outdir ${PROJECT_BINARY_DIR}/lexyacc)
    file(MAKE_DIRECTORY ${outdir})
    file(WRITE ${outdir}/CMakeLists.txt "project(lexyacc)\nset(CMAKE_CXX_STANDARD 20)\nadd_executable(lexyacc ${LEXYACC_CPP})\ninstall(TARGETS lexyacc RUNTIME DESTINATION bin)")
    myexec(${outdir} ${CMAKE_COMMAND} .)
    myexec(${outdir} ${CMAKE_COMMAND} --build . --config Debug)
    myexec(${outdir} ${CMAKE_COMMAND} --install . --config Debug --prefix ${outdir})
    find_program(LEXYACC_BINARY lexyacc REQUIRED HINTS ${outdir}/bin)
endfunction()

function(target_add_lexyacc target lyfile)
    find_or_create_lexyacc()
    if (NOT EXISTS FLEX_EXECUTABLE)
        find_package(FLEX REQUIRED)
    endif()

    if (NOT EXISTS BISON_EXECUTABLE)
        find_package(BISON QUIET)
        if(NOT BISON_FOUND)
            build_bison()
        endif()
    endif()

    cmake_parse_arguments(lexyacc "" "NAME" "" ${ARGN})

    if (NOT lexyacc_NAME)
        get_filename_component(lexyacc_NAME ${lyfile} NAME_WE)
    endif()

    get_filename_component(srcdir ${lyfile} DIRECTORY)

    set(lytgt ${target}_lexyacc_${lexyacc_NAME})
    set(outdir ${PROJECT_BINARY_DIR}/${lytgt})
    file(MAKE_DIRECTORY ${outdir})
    set(yh ${outdir}/${lexyacc_NAME}.yacc.h)
    set(yc ${outdir}/${lexyacc_NAME}.yacc.cpp)
    set(lc ${outdir}/${lexyacc_NAME}.flex.cpp)
    set(yy ${outdir}/${lexyacc_NAME}.y)
    set(ll ${outdir}/${lexyacc_NAME}.l)
    set(hh ${outdir}/${lexyacc_NAME}.ly.h)

    set(outputs ${yy} ${yh} ${yc} ${ll} ${lc} ${hh})
    message(STATUS  "${FLEX_EXECUTABLE} ${ll} -o${lc} --c++ --prefix=${lexyacc_NAME}")

    add_custom_command(
        OUTPUT  ${yy} ${yh} ${yc} ${ll} ${lc} ${hh}
        DEPENDS ${LEXYACC_BINARY} ${BISON_EXECUTABLE} ${FLEX_EXECUTABLE} ${lyfile}
        WORKING_DIRECTORY ${outdir}
        COMMAND ${LEXYACC_BINARY} ${lyfile} --outdir ${outdir} --prefix ${lexyacc_NAME}
        COMMAND ${BISON_EXECUTABLE} -o ${yc} --name-prefix=${lexyacc_NAME} --language=c++ --defines=${yh} ${yy}
        COMMAND ${FLEX_EXECUTABLE} -o${lc} --c++ --prefix=${lexyacc_NAME} ${ll}
        )
    target_sources(${target} PRIVATE ${lyfile} ${outputs})
    target_include_directories(${target} PRIVATE ${FLEX_INCLUDE} ${outdir})

    if (MSVC)
        set_source_files_properties(${lc} PROPERTIES COMPILE_FLAGS "-wd4005 -wd4065")
        set_source_files_properties(${yc} PROPERTIES COMPILE_FLAGS "-wd4065 -wd4127")
    endif()
    # TODO
    #add_library(${lytgt} ${outputs})
    #target_include_directories(${lytgt} PRIVATE ${FLEX_INCLUDE} ${srcdir})
    #target_include_directories(${lytgt} PUBLIC ${outdir})
    #target_link_libraries(${target} PRIVATE ${lytgt})
endfunction()
