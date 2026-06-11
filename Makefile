CC ?= cc
PREFIX ?= /usr
CFLAGS ?= -O2
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?= -lm

BIN := RBrubik
SRC := src/main.c
ALL_CFLAGS := -std=c99 -Wall -Wextra $(CPPFLAGS) $(CFLAGS)

.PHONY: all clean run install uninstall

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $(SRC) $(LDLIBS)

run: $(BIN)
	./$(BIN)

install: $(BIN)
	install -Dm755 $(BIN) "$(DESTDIR)$(PREFIX)/bin/$(BIN)"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/$(BIN)"

clean:
	rm -f $(BIN) rbrubik
