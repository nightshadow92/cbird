include("pre.pri")

FILES += testbase cvutil ioutil

contains(DEFINES, ENABLE_DEPRECATED) {
    LIBS += $$LIBS_LIBPHASH
}

include("post.pri")
