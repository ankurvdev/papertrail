
function(use_osgplugin1 target)
    if ((NOT EXISTS ${OSGPLUGIN_DEBUG_LIBPATH}) OR (NOT EXISTS ${OSGPLUGIN_RELEASE_LIBPATH}))
        if ((NOT EXISTS ${osg_LIBRARY_DEBUG}) OR (NOT EXISTS ${osg_LIBRARY_RELEASE}))
            message(FATAL_ERROR "Cannot find osg library to detect plugins from")
        endif()
        get_filename_component(debugdir ${osg_LIBRARY_DEBUG} DIRECTORY)
        get_filename_component(releasedir ${osg_LIBRARY_RELEASE} DIRECTORY)
        file(GLOB debugplugindir ${debugdir}/osgPlugins-*)
        file(GLOB releaseplugindir ${releasedir}/osgPlugins-*)
        if ((NOT EXISTS ${debugplugindir}) OR (NOT EXISTS ${releaseplugindir}))
            message(FATAL_ERROR "Cannot find debug and release plugins dir ${debugplugindir}::${releaseplugindir} - ${debugdir}::${releasedir}")
        endif()
        set(OSGPLUGIN_DEBUG_LIBPATH ${debugplugindir} CACHE PATH "" FORCE)
        set(OSGPLUGIN_RELEASE_LIBPATH ${releaseplugindir} CACHE PATH "" FORCE)
    endif()

    foreach (plib ${ARGN})
        find_library(${plib}_LIBRARY_DEBUG ${plib}d PATHS ${OSGPLUGIN_DEBUG_LIBPATH} )
        find_library(${plib}_LIBRARY_RELEASE ${plib} PATHS ${OSGPLUGIN_RELEASE_LIBPATH})
        if ((NOT EXISTS ${${plib}_LIBRARY_DEBUG} ) OR (NOT EXISTS ${${plib}_LIBRARY_RELEASE}))
            message(STATUS "Search Paths :  ${OSGPLUGIN_DEBUG_LIBPATH} :: ${OSGPLUGIN_RELEASE_LIBPATH}")
            message(FATAL_ERROR "Cannot find plugin: ${${plib}_LIBRARY_DEBUG} ${${plib}_LIBRARY_RELEASE}")
        endif()
        target_link_libraries(${target} PRIVATE debug ${${plib}_LIBRARY_DEBUG} optimized ${${plib}_LIBRARY_RELEASE})
    endforeach()
endfunction()

macro (detect_osg_plugin_dir)
    if ((NOT EXISTS ${OSGPLUGIN_DEBUG_LIBPATH}}) OR (NOT EXISTS ${OSGPLUGIN_RELEASE_LIBPATH}))
        if ((NOT EXISTS ${osg_LIBRARY_DEBUG}) OR (NOT EXISTS ${osg_LIBRARY_RELEASE}))
            message(FATAL_ERROR "Cannot find osg library to detect plugins from")
        endif()
        get_filename_component(debugdir ${osg_LIBRARY_DEBUG} DIRECTORY)
        get_filename_component(releasedir ${osg_LIBRARY_RELEASE} DIRECTORY)
        file(GLOB debugplugindir ${debugdir}/osgPlugins-*)
        file(GLOB releaseplugindir ${releasedir}/osgPlugins-*)
        if ((NOT EXISTS ${debugplugindir}) OR (NOT EXISTS ${releaseplugindir}))
            message(FATAL_ERROR "Cannot find debug and release plugins dir ${debugplugindir}::${releaseplugindir} - ${debugdir}::${releasedir}")
        endif()
        set(OSGPLUGIN_DEBUG_LIBPATH ${debugplugindir})
        set(OSGPLUGIN_RELEASE_LIBPATH ${releaseplugindir})
    endif()
endmacro()

function(use_osgplugin target)
    detect_osg_plugin_dir()
    file(GLOB rellibs "${OSGPLUGIN_RELEASE_LIBPATH}/*")
    file(GLOB dbglibs "${OSGPLUGIN_DEBUG_LIBPATH}/*")
    foreach(lib ${rellibs})
        get_filename_component(libname ${lib} NAME_WLE)
        get_filename_component(libext ${lib} LAST_EXT)
        set(dbglibpath ${OSGPLUGIN_DEBUG_LIBPATH}/${libname}d${libext})
        if (NOT EXISTS "${dbglibpath}")
            message(FATAL_ERROR "Cannot find debug lib: ${dbglibpath}")
        endif()
        target_link_libraries(${target} PRIVATE debug ${dbglibpath} optimized ${lib})
    endforeach()
endfunction()

function(use_openscenegraph target)
    vcpkg_download(osg)
    set (OSGLIBLIST
        osg osgDB osgViewer osgGA osgManipulator osgUtil osgText osgTerrain osgSim osgShadow OpenThreads
    )
    set(OSG_PLUGIN_LIST osgdb_zip osgdb_tiff osgdb_jpeg osgdb_osg osgdb_obj osgdb_png osgdb_freetype osgdb_serializers_osg osgdb_scale)
    find_package(OpenSceneGraph REQUIRED ${OSGLIBLIST})
    foreach (libname ${OSGLIBLIST})
        find_vcpkg_library(${libname} ${libname})
        target_link_libraries(${target} PRIVATE ${${libname}})
    endforeach()
    find_package(Iconv REQUIRED QUIET)
    find_package(OpenGL REQUIRED QUIET)
    find_package(Freetype REQUIRED QUIET)
    if (NOT WIN32)
    find_package(Fontconfig REQUIRED QUIET)
    endif()
    find_package(CURL REQUIRED QUIET)
    find_package(TinyXML REQUIRED QUIET)

    if (WIN32)
    foreach (libname charset)
        find_vcpkg_library(${libname} ${libname})
        target_link_libraries(${target} PRIVATE ${${libname}})
    endforeach()
    endif()

    target_link_libraries(${target} PRIVATE ${LIBXML2_LIBRARIES})
    if(WIN32)
        target_link_libraries(${target} PRIVATE OpenGL::GL)
    else()
        find_package(X11 QUIET)
        if (X11_FOUND)
            target_link_libraries(${target} PRIVATE X11::X11 X11::Xrandr)
        endif()
        target_link_libraries(${target} PRIVATE OpenGL::GL GLU dl)
    endif()

    target_link_libraries(${target} PRIVATE Freetype::Freetype unofficial-tinyxml::unofficial-tinyxml)
    target_link_libraries(${target} PRIVATE Iconv::Iconv)
    if (NOT WIN32)
    target_link_libraries(${target} PRIVATE Fontconfig::Fontconfig)
    endif()
    target_link_libraries(${target} PRIVATE CURL::libcurl)
    target_link_libraries(${target} PRIVATE ${OPENSCENEGRAPH_LIBRARIES})
    target_include_directories(${target} PRIVATE ${OPENSCENEGRAPH_INCLUDE_DIRS})
    target_compile_definitions(${target} PUBLIC OSG_LIBRARY_STATIC)
    use_osgplugin(${target} ${OSG_PLUGIN_LIST})
endfunction()

function(use_osgearth_impl target)
    SET(CMAKE_DEBUG_POSTFIX "d" CACHE STRING "add a postfix, usually d on windows")

    vcpkg_download(osgearth)
    use_openscenegraph(${target})
    find_package(unofficial-brotli CONFIG REQUIRED)
    find_file(hdr EarthManipulator)
    get_filename_component(incpath ${hdr} DIRECTORY)
    target_include_directories(${target} PRIVATE ${incpath}/..)
    set(LIBLIST
        osgEarth geos geos_c gdal proj webp gif uuid sqlite3 cfitsio openjp2 xml2 netcdf hdf5 hdf5_hl szip pq geotiff pgport pgcommon
        osgdb_earth osgdb_osgearth_cache_filesystem osgdb_osgearth_engine_rex
    )
    if (WIN32)
        list(APPEND LIBLIST expatMD)
    else()
        list(APPEND LIBLIST expat json-c zstd pcre)
        target_link_options(${target} PRIVATE "-Wl,--allow-multiple-definition")
    endif()

    foreach(libname ${LIBLIST})
        find_vcpkg_library(${libname} ${libname} SUBDIRECTORY osgPlugins-3.6.5)
        target_link_libraries(${target} PRIVATE ${${libname}})
    endforeach()

    find_package(TIFF REQUIRED QUIET)
    target_link_libraries(${target} PRIVATE unofficial::brotli::brotlienc-static TIFF::TIFF)

    target_compile_definitions(${target} PUBLIC OSGEARTH_LIBRARY_STATIC OSGEARTH_HAVE_MVT OSGEARTH_HAVE_SQLITE3)
endfunction()

function(link_to_target target)
    use_osgearth_impl(${target})
    use_osgearth_impl(${target})
    use_osgearth_impl(${target})
    use_osgearth_impl(${target})
    use_vcpkg(${target} pthreads)
endfunction()
