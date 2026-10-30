APP_NAME     = GoH-Tasks
APP_MIME_SIG = application/x-vnd.GoH-Tasks
SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:src/%.cpp=build/%.o)
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -I/boot/system/develop/headers -Isrc -Iresources -MMD -MP
LDFLAGS = -lbe -lnetwork -lcurl -llocalestub

RDEF = resources/GTasks-icon.rdef
RSRC = build/GTasks-icon.rsrc

# Languages this app ships translations for (besides the English source
# text itself). Each one needs a locales/<lang>.catkeys file.
LOCALES = it es de fr pt
CATALOGS = $(LOCALES:%=build/catalogs/$(APP_MIME_SIG)/%.catalog)
CATALOG_INSTALL_DIR = $(HOME)/config/non-packaged/data/locale/catalogs/$(APP_MIME_SIG)

LANGNAME_it = Italian
LANGNAME_es = Spanish
LANGNAME_de = German
LANGNAME_fr = French
LANGNAME_pt = Portuguese

all: $(APP_NAME) install-catalogs

$(APP_NAME): $(OBJS) $(RSRC)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)
	xres -o $@ $(RSRC)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(RSRC): $(RDEF) | build
	rc -o $@ $<

build:
	mkdir -p build

# ---- Localization -----------------------------------------------------
#
# locales/en.catkeys is the English template, (re)collected from the
# B_TRANSLATE() calls in the source via `make catkeys` whenever strings
# are added/changed. It is a reference for translators, not shipped.
#
# locales/<lang>.catkeys are the translated catalogs (hand-edited copies
# of the template, one row per key). `make` compiles them to binary
# .catalog files and installs them so BCatalog can find them at runtime.

build/catkeys/%.pre: src/%.cpp | build
	mkdir -p build/catkeys
	$(CXX) -std=c++17 -I/boot/system/develop/headers -Isrc -Iresources \
		-DB_COLLECTING_CATKEYS -E -P $< -o $@

catkeys: $(SRCS:src/%.cpp=build/catkeys/%.pre)
	cat $^ > build/catkeys/combined.pre
	collectcatkeys -v -s $(APP_MIME_SIG) -o locales/en.catkeys build/catkeys/combined.pre

build/catalogs/$(APP_MIME_SIG)/%.catalog: locales/%.catkeys
	mkdir -p $(dir $@)
	linkcatkeys -o $@ -s $(APP_MIME_SIG) -l $(LANGNAME_$*) $<

catalogs: $(CATALOGS)

install-catalogs: catalogs
	mkdir -p $(CATALOG_INSTALL_DIR)
	cp $(CATALOGS) $(CATALOG_INSTALL_DIR)/

clean:
	rm -rf build $(APP_NAME)

.PHONY: all clean catkeys catalogs install-catalogs

-include $(OBJS:.o=.d)
