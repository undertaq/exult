vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/FluidSynth/fluidsynth/archive/v2.5.1.tar.gz"
    FILENAME "fluidsynth-2.5.1.tar.gz"
    SHA512 0e897a1a3e1499150c26dc0ce4fc1fa4e5323cd84e0f1b6cdf305b4cfd3fd46cbefddf490241e8645a9bd291e485a830d109b1c7a83e13b816f0345839c4c36c
)

vcpkg_find_acquire_program(7Z)

set(EXTRACT_DIR "${CURRENT_BUILDTREES_DIR}/src/v2.5.1-${TARGET_TRIPLET}.clean")
file(REMOVE_RECURSE "${EXTRACT_DIR}")
file(MAKE_DIRECTORY "${EXTRACT_DIR}")
file(TO_NATIVE_PATH "${EXTRACT_DIR}" EXTRACT_DIR_NATIVE)
file(TO_NATIVE_PATH "${ARCHIVE}" ARCHIVE_NATIVE)

vcpkg_execute_required_process(
    COMMAND "${7Z}" x "${ARCHIVE_NATIVE}" -o${EXTRACT_DIR_NATIVE} -y
    WORKING_DIRECTORY "${EXTRACT_DIR_NATIVE}"
    LOGNAME extract-1
)

file(GLOB TARBALL "${EXTRACT_DIR}/*.tar")
list(LENGTH TARBALL TARBALL_LEN)
if(TARBALL_LEN GREATER 0)
    file(TO_NATIVE_PATH "${TARBALL}" TARBALL_NATIVE)
    vcpkg_execute_required_process(
        COMMAND "${7Z}" x "${TARBALL_NATIVE}" -o${EXTRACT_DIR_NATIVE} -y
        WORKING_DIRECTORY "${EXTRACT_DIR_NATIVE}"
        LOGNAME extract-2
    )
    file(REMOVE ${TARBALL})
endif()

file(GLOB SUBDIRS "${EXTRACT_DIR}/*")
set(SOURCE_PATH "${EXTRACT_DIR}")
foreach(D ${SUBDIRS})
    if(IS_DIRECTORY "${D}")
        get_filename_component(SUB_NAME "${D}" NAME)
        if(EXISTS "${EXTRACT_DIR}/${SUB_NAME}/CMakeLists.txt")
            set(SOURCE_PATH "${EXTRACT_DIR}/${SUB_NAME}")
            break()
        endif()
    endif()
endforeach()

file(GLOB_RECURSE EMPTY_FILES "${SOURCE_PATH}/*")
foreach(F ${EMPTY_FILES})
    file(SIZE "${F}" SIZE_VAL)
    if(SIZE_VAL EQUAL 0)
        file(REMOVE "${F}")
    endif()
endforeach()

file(REMOVE
    "${SOURCE_PATH}/cmake_admin/FindFLAC.cmake"
    "${SOURCE_PATH}/cmake_admin/Findmp3lame.cmake"
    "${SOURCE_PATH}/cmake_admin/Findmpg123.cmake"
    "${SOURCE_PATH}/cmake_admin/FindOgg.cmake"
    "${SOURCE_PATH}/cmake_admin/FindOpus.cmake"
    "${SOURCE_PATH}/cmake_admin/FindSndFileLegacy.cmake"
    "${SOURCE_PATH}/cmake_admin/FindVorbis.cmake"
)

vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        libinstpatch enable-libinstpatch
        sndfile      enable-libsndfile
        pulseaudio   enable-pulseaudio
)

set(WINDOWS_OPTIONS enable-dsound enable-wasapi enable-waveout enable-winmidi HAVE_MMSYSTEM_H HAVE_DSOUND_H HAVE_OBJBASE_H)
set(MACOS_OPTIONS enable-coreaudio enable-coremidi COREAUDIO_FOUND COREMIDI_FOUND)
set(LINUX_OPTIONS enable-alsa ALSA_FOUND)
set(ANDROID_OPTIONS enable-opensles OpenSLES_FOUND)
set(IGNORED_OPTIONS enable-coverage enable-dbus enable-floats enable-fpe-check enable-framework enable-jack
    enable-libinstpatch enable-midishare enable-oboe enable-openmp enable-oss enable-pipewire enable-portaudio
    enable-profiling enable-readline enable-sdl3 enable-systemd enable-trap-on-fpe enable-ubsan)

if(VCPKG_TARGET_IS_WINDOWS)
    set(OPTIONS_TO_ENABLE ${WINDOWS_OPTIONS})
    set(OPTIONS_TO_DISABLE ${MACOS_OPTIONS} ${LINUX_OPTIONS} ${ANDROID_OPTIONS})
elseif(VCPKG_TARGET_IS_OSX)
    set(OPTIONS_TO_ENABLE ${MACOS_OPTIONS})
    set(OPTIONS_TO_DISABLE ${WINDOWS_OPTIONS} ${LINUX_OPTIONS} ${ANDROID_OPTIONS})
elseif(VCPKG_TARGET_IS_LINUX)
    set(OPTIONS_TO_ENABLE ${LINUX_OPTIONS})
    set(OPTIONS_TO_DISABLE ${WINDOWS_OPTIONS} ${MACOS_OPTIONS} ${ANDROID_OPTIONS})
elseif(VCPKG_TARGET_IS_ANDROID)
    set(OPTIONS_TO_ENABLE ${ANDROID_OPTIONS})
    set(OPTIONS_TO_DISABLE ${WINDOWS_OPTIONS} ${MACOS_OPTIONS} ${LINUX_OPTIONS})
endif()

foreach(_option IN LISTS OPTIONS_TO_ENABLE)
    list(APPEND ENABLED_OPTIONS "-D${_option}:BOOL=ON")
endforeach()
    
foreach(_option IN LISTS OPTIONS_TO_DISABLE IGNORED_OPTIONS)
    list(APPEND DISABLED_OPTIONS "-D${_option}:BOOL=OFF")
endforeach()

vcpkg_find_acquire_program(PKGCONFIG)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        ${ENABLED_OPTIONS}
        ${DISABLED_OPTIONS}
        "-Dosal=cpp11" 
        "-DPKG_CONFIG_EXECUTABLE=${PKGCONFIG}"
    MAYBE_UNUSED_VARIABLES
        ${OPTIONS_TO_DISABLE}
        enable-coverage
        enable-framework
        enable-ubsan
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/fluidsynth)
vcpkg_fixup_pkgconfig()

vcpkg_copy_tools(TOOL_NAMES fluidsynth AUTO_CLEAN)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/share/man")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")