BUILDDIR = build
OBJS     = $(addprefix $(BUILDDIR)/,main.o parser.o scanner.o citip.o)
CPPFLAGS = -MMD -MP
CXXFLAGS = -std=c++11 -I. -I$(BUILDDIR) -Wno-deprecated-register
LDLIBS   = -lglpk

BISON    = bison
FLEX     = flex

# Homebrew installs GLPK and bison outside the default search paths. Its
# bison is also keg-only because macOS ships bison 2.3, which is too old
# for parser.y (needs >= 3.0). Use them if they are installed. Any of
# these can be overridden, e.g. 'make BISON=/path/to/bison'.
HOMEBREW := $(shell brew --prefix 2>/dev/null)
ifneq ($(HOMEBREW),)
    ifneq ($(wildcard $(HOMEBREW)/opt/bison/bin/bison),)
        BISON = $(HOMEBREW)/opt/bison/bin/bison
    endif
    ifneq ($(wildcard $(HOMEBREW)/opt/glpk/include/glpk.h),)
        GLPK_PREFIX = $(HOMEBREW)/opt/glpk
    endif
endif
ifdef GLPK_PREFIX
    CPPFLAGS += -I$(GLPK_PREFIX)/include
    LDFLAGS  += -L$(GLPK_PREFIX)/lib
endif

all: oXitipLen

oXitipLen: $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BUILDDIR)/%.o: %.cpp | $(BUILDDIR)
	$(CXX) -o $@ -c $< $(CPPFLAGS) $(CXXFLAGS)

$(BUILDDIR)/%.o: $(BUILDDIR)/%.cxx
	$(CXX) -o $@ -c $< $(CPPFLAGS) $(CXXFLAGS)

$(BUILDDIR)/parser.cxx: parser.y | $(BUILDDIR)
	$(BISON) -o $@ --defines=$(BUILDDIR)/parser.hxx $<

$(BUILDDIR)/scanner.cxx: scanner.l | $(BUILDDIR)
	$(FLEX) -o $@ --header-file=$(BUILDDIR)/scanner.hxx $<

$(OBJS): $(BUILDDIR)/scanner.cxx $(BUILDDIR)/parser.cxx

$(BUILDDIR):
	@mkdir -p $@

.PHONY: all clean clobber
clean:
	rm -rf $(BUILDDIR)

clobber: clean
	rm -f oXitipLen

-include $(OBJS:%.o=%.d)
