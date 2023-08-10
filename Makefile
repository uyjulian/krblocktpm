
PLTHOOK_SOURCES += external/plthook/plthook_win32.c

SOURCES += main.cpp

SOURCES += $(PLTHOOK_SOURCES)

INCFLAGS += -Iexternal/plthook

LDLIBS += -ldbghelp

PROJECT_BASENAME = krblocktpm

include external/tp_stubz/Rules.lib.make
