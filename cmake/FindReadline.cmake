include(FindPackageHandleStandardArgs)

if (Readline_INCLUDE_DIR)
    set(Readline_FOUND TRUE)
else ()
    find_path(
            Readline_ROOT_DIR
            NAMES include/readline/readline.h
    )

    find_path(
            Readline_INCLUDE_DIR
            NAMES readline/readline.h
            HINTS ${Readline_ROOT_DIR}/include
    )

    find_library(
            Readline_LIBRARY
            NAMES readline
            HINTS ${Readline_ROOT_DIR}/lib
    )

    if (Readline_INCLUDE_DIR AND Readline_LIBRARY AND Ncurses_LIBRARY)
        set(Readline_FOUND TRUE)
    else (Readline_INCLUDE_DIR AND Readline_LIBRARY AND Ncurses_LIBRARY)
        FIND_LIBRARY(Readline_LIBRARY NAMES readline)
        include(FindPackageHandleStandardArgs)
        FIND_PACKAGE_HANDLE_STANDARD_ARGS(Readline DEFAULT_MSG Readline_INCLUDE_DIR Readline_LIBRARY)
        MARK_AS_ADVANCED(Readline_INCLUDE_DIR Readline_LIBRARY)
    endif (Readline_INCLUDE_DIR AND Readline_LIBRARY AND Ncurses_LIBRARY)

    mark_as_advanced(
            Readline_ROOT_DIR
            Readline_INCLUDE_DIR
            Readline_LIBRARY
    )
endif ()
