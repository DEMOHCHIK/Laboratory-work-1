CC     ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
BIN    := bin

.PHONY: all test demo clean

all: $(BIN)/fsbuild $(BIN)/fsremove

$(BIN)/%: src/%.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN):
	mkdir -p $@

# автопроверка: C-программа и sh-скрипт должны давать одинаковый результат
test: all
	@for s in structure.spec examples/*.spec; do sh scripts/verify.sh $$s || exit 1; done

# демонстрация: строим структуру в demo/ и показываем ls -lR
demo: all
	rm -rf demo && mkdir demo
	cd demo && ../$(BIN)/fsbuild ../structure.spec && ls -liR .

clean:
	rm -rf $(BIN) demo
