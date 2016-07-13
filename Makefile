# bolo Makefile
#
# author:  James Hunt <james@niftylogic.com>
# created: 2016-07-12
#

CC       ?= clang
AFLCC    ?= afl-clang
TABLEGEN := util/tablegen

CFLAGS += -I./include

default: all
all:
clean:
	rm -f $(CLEAN_FILES)

# files to remove on `make clean'
CLEAN_FILES :=

# header files required by all object files.
CORE_H := include/bolo.h

# header files that should be distributed.
DIST_H := include/bolo.h


# source files that comprise the Qualified Name implementation.
QNAME_SRC  := src/qname.c
QNAME_OBJ  := $(QNAME_SRC:.c=.o)
QNAME_FUZZ := $(QNAME_SRC:.c=.fuzz.o)
CLEAN_FILES += $(QNAME_OBJ) $(QNAME_FUZZ)

src/qname_chars.inc: src/qname_chars.tbl $(TABLEGEN)
	$(TABLEGEN) >$@ <$<
src/qname.o: src/qname.c $(CORE_H) src/qname_chars.inc
	$(CC) $(CFLAGS) -o $@ -c $<

# scripts that perform Contract Testing.
CONTRACT_TEST_SCRIPTS := t/contract/qname

# binaries that the Contract Tests run.
CONTRACT_TEST_BINS := t/contract/r/qname-string \
                      t/contract/r/qname-equiv \
                      t/contract/r/qname-match
CLEAN_FILES   += $(CONTRACT_TEST_BINS)

contract-tests: $(CONTRACT_TEST_BINS)
t/contract/r/qname-string: t/contract/r/qname-string.o $(QNAME_OBJ)
t/contract/r/qname-equiv:  t/contract/r/qname-equiv.o  $(QNAME_OBJ)
t/contract/r/qname-match:  t/contract/r/qname-match.o  $(QNAME_OBJ)


# binaries that the Fuzz Tests run.
FUZZ_TEST_BINS := t/fuzz/r/qname
CLEAN_FILES   += $(FUZZ_TEST_BINS)

fuzz-tests: $(FUZZ_TEST_BINS)
t/fuzz/r/qname: t/fuzz/r/qname.o $(QNAME_FUZZ)


tests: $(CONTRACT_TEST_BINS)
test: tests
	$(CONTRACT_TEST_SCRIPTS)


%.fuzz.o: %.c
	$(AFLCC) $(CFLAGS) -c -o $@ $<


.PHONY: all clean