SHELL := /bin/bash

# ============================================================
# ARCHIVOS PRINCIPALES
# ============================================================

CC := gcc
SRC := src/main.c
BUILD_DIR := build

PROGRAM := $(BUILD_DIR)/program
PROGRAM_DEBUG := $(BUILD_DIR)/program_debug
PROGRAM_ASAN := $(BUILD_DIR)/program_asan
PROGRAM_VERIFIER := $(BUILD_DIR)/program_verifier


# ============================================================
# PAQUETES OFICIALES
# ============================================================

PUBLIC_ZIP := tests/public_tests.zip
GEN_ZIP := tests/test_gen_2026.zip
ORACLE_ZIP := tests/sol_gen_2026_v2.zip
PROFILER_ZIP := tests/esempi_per_test_profiler.zip

PUBLIC_DIR := tests/public
GEN_DIR := tools/test_gen
ORACLE_DIR := tools/oracle
PROFILER_DIR := tools/profiler_examples

GENERATOR := $(GEN_DIR)/test_gen_2026_linux
ORACLE := $(ORACLE_DIR)/sol_2026_linux


# ============================================================
# OPCIONES DE COMPILACION
# ============================================================

BASE_FLAGS := -std=gnu11 -Wall -Werror

RELEASE_FLAGS := -DEVAL $(BASE_FLAGS) -O2 -pipe
DEBUG_FLAGS := $(BASE_FLAGS) -O0 -g3
ASAN_FLAGS := $(BASE_FLAGS) -O1 -g3 \
              -fsanitize=address \
              -fno-omit-frame-pointer

VERIFIER_FLAGS := -DEVAL $(BASE_FLAGS) -O2 -pipe -static -s

LDLIBS := -lm


# ============================================================
# PARAMETROS MODIFICABLES DESDE LA TERMINAL
# ============================================================

TEST ?= example1
INPUT ?= $(PUBLIC_DIR)/example1.txt

GEN_ARGS ?=
GEN_INPUT ?= $(BUILD_DIR)/generated.txt
GEN_EXPECTED ?= $(BUILD_DIR)/generated.expected.txt
GEN_ACTUAL ?= $(BUILD_DIR)/generated.actual.txt


# ============================================================
# TARGETS DECLARADOS COMO ACCIONES
# ============================================================

.PHONY: all help setup release verifier debug asan \
        test test-one generate oracle check-generated \
        asan-run memcheck gdb time callgrind massif \
        clean distclean


# ============================================================
# TARGET POR DEFECTO
# ============================================================

all: release


# ============================================================
# AYUDA
# ============================================================

help:
	@printf '%s\n' \
	  'make setup                 Extrae tests y herramientas oficiales' \
	  'make release               Compila para el trabajo cotidiano' \
	  'make verifier              Replica los flags del verificador' \
	  'make debug                 Compila para GDB y Valgrind' \
	  'make asan                  Compila con AddressSanitizer' \
	  'make test                  Ejecuta todos los tests publicos' \
	  'make test-one TEST=test9   Ejecuta un test concreto' \
	  "make generate GEN_ARGS='-i 1 -I 16 -n 20 -m 100'" \
	  'make oracle                Calcula la salida oficial generada' \
	  'make check-generated       Compara programa y oraculo' \
	  'make asan-run INPUT=...    Ejecuta bajo ASan' \
	  'make memcheck INPUT=...    Ejecuta bajo Valgrind' \
	  'make gdb                   Abre GDB' \
	  'make time INPUT=...        Mide tiempo y memoria' \
	  'make callgrind INPUT=...   Genera perfil temporal' \
	  'make massif INPUT=...      Genera perfil de memoria' \
	  'make clean                 Borra build/' \
	  'make distclean             Borra tambien archivos extraidos'


# ============================================================
# EXTRACCION DE RECURSOS OFICIALES
# ============================================================

setup: $(PUBLIC_DIR)/.ready \
       $(GENERATOR) \
       $(ORACLE) \
       $(PROFILER_DIR)/.ready


$(PUBLIC_DIR)/.ready: $(PUBLIC_ZIP)
	rm -rf $(PUBLIC_DIR)
	mkdir -p $(PUBLIC_DIR)
	unzip -oq $< -d $(PUBLIC_DIR)
	touch $@


$(GENERATOR): $(GEN_ZIP)
	rm -rf $(GEN_DIR)
	mkdir -p $(GEN_DIR)
	unzip -oq $< -d $(GEN_DIR)
	chmod +x $@


$(ORACLE): $(ORACLE_ZIP)
	rm -rf $(ORACLE_DIR)
	mkdir -p $(ORACLE_DIR)
	unzip -oq $< -d $(ORACLE_DIR)
	chmod +x $@


$(PROFILER_DIR)/.ready: $(PROFILER_ZIP)
	rm -rf $(PROFILER_DIR)
	mkdir -p $(PROFILER_DIR)
	unzip -oq $< -d $(PROFILER_DIR)
	touch $@


# ============================================================
# DIRECTORIO DE BUILD
# ============================================================

$(BUILD_DIR):
	mkdir -p $@


# ============================================================
# COMPILACIONES
# ============================================================

release: $(PROGRAM)


$(PROGRAM): $(SRC) | $(BUILD_DIR)
	$(CC) $(RELEASE_FLAGS) $< -o $@ $(LDLIBS)


verifier: $(PROGRAM_VERIFIER)


$(PROGRAM_VERIFIER): $(SRC) | $(BUILD_DIR)
	$(CC) $(VERIFIER_FLAGS) $< -o $@ $(LDLIBS)


debug: $(PROGRAM_DEBUG)


$(PROGRAM_DEBUG): $(SRC) | $(BUILD_DIR)
	$(CC) $(DEBUG_FLAGS) $< -o $@ $(LDLIBS)


asan: $(PROGRAM_ASAN)


$(PROGRAM_ASAN): $(SRC) | $(BUILD_DIR)
	$(CC) $(ASAN_FLAGS) $< -o $@ $(LDLIBS)


# ============================================================
# TEST PUBLICO INDIVIDUAL
# ============================================================

test-one: setup release
	@test -f "$(PUBLIC_DIR)/$(TEST).txt" || { \
	  echo "No existe $(PUBLIC_DIR)/$(TEST).txt"; \
	  exit 2; \
	}
	@$(PROGRAM) \
	  < "$(PUBLIC_DIR)/$(TEST).txt" \
	  > "$(BUILD_DIR)/$(TEST).actual.txt"
	@diff -u \
	  "$(PUBLIC_DIR)/$(TEST).output.txt" \
	  "$(BUILD_DIR)/$(TEST).actual.txt"
	@echo "[OK] $(TEST)"


# ============================================================
# TODOS LOS TESTS PUBLICOS
# ============================================================

test: setup release
	@fail=0; count=0; passed=0; \
	for input in $(PUBLIC_DIR)/*.txt; do \
	  [[ "$$input" == *.output.txt ]] && continue; \
	  stem="$${input%.txt}"; \
	  expected="$$stem.output.txt"; \
	  name="$$(basename "$$stem")"; \
	  actual="$(BUILD_DIR)/$$name.actual.txt"; \
	  diff_file="$(BUILD_DIR)/$$name.diff"; \
	  count=$$((count + 1)); \
	  if $(PROGRAM) < "$$input" > "$$actual" && \
	     diff -u "$$expected" "$$actual" > "$$diff_file"; then \
	    printf '[OK]   %s\n' "$$name"; \
	    rm -f "$$diff_file"; \
	    passed=$$((passed + 1)); \
	  else \
	    printf '[FAIL] %s  (ver %s)\n' \
	      "$$name" "$$diff_file"; \
	    fail=1; \
	  fi; \
	done; \
	printf '\nResultado: %d/%d tests publicos superados.\n' \
	  "$$passed" "$$count"; \
	exit $$fail


# ============================================================
# GENERADOR Y ORACULO
# ============================================================

generate: setup | $(BUILD_DIR)
	$(GENERATOR) $(GEN_ARGS) > "$(GEN_INPUT)"
	@echo "Input generado: $(GEN_INPUT)"


oracle: setup | $(BUILD_DIR)
	@test -f "$(GEN_INPUT)" || { \
	  echo "Primero ejecuta make generate"; \
	  exit 2; \
	}
	$(ORACLE) < "$(GEN_INPUT)" > "$(GEN_EXPECTED)"
	@echo "Salida esperada: $(GEN_EXPECTED)"


check-generated: release oracle
	$(PROGRAM) < "$(GEN_INPUT)" > "$(GEN_ACTUAL)"
	diff -u "$(GEN_EXPECTED)" "$(GEN_ACTUAL)"
	@echo "[OK] El caso generado coincide con el oraculo."


# ============================================================
# ADDRESSSANITIZER
# ============================================================

asan-run: asan
	@test -f "$(INPUT)" || { \
	  echo "No existe INPUT=$(INPUT)"; \
	  exit 2; \
	}
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	  $(PROGRAM_ASAN) \
	  < "$(INPUT)" \
	  > "$(BUILD_DIR)/asan.actual.txt"
	@echo "ASan termino sin detectar errores."


# ============================================================
# VALGRIND / MEMCHECK
# ============================================================

memcheck: debug
	@test -f "$(INPUT)" || { \
	  echo "No existe INPUT=$(INPUT)"; \
	  exit 2; \
	}
	valgrind \
	  --leak-check=full \
	  --show-leak-kinds=all \
	  --track-origins=yes \
	  --error-exitcode=99 \
	  $(PROGRAM_DEBUG) \
	  < "$(INPUT)" \
	  > "$(BUILD_DIR)/memcheck.actual.txt"


# ============================================================
# GDB
# ============================================================

gdb: debug
	gdb $(PROGRAM_DEBUG)


# ============================================================
# MEDICION GLOBAL
# ============================================================

time: release
	@test -f "$(INPUT)" || { \
	  echo "No existe INPUT=$(INPUT)"; \
	  exit 2; \
	}
	/usr/bin/time -v \
	  $(PROGRAM) \
	  < "$(INPUT)" \
	  > "$(BUILD_DIR)/time.actual.txt"


# ============================================================
# CALLGRIND
# ============================================================

callgrind: debug
	@test -f "$(INPUT)" || { \
	  echo "No existe INPUT=$(INPUT)"; \
	  exit 2; \
	}
	valgrind \
	  --tool=callgrind \
	  --callgrind-out-file="$(BUILD_DIR)/callgrind.out" \
	  $(PROGRAM_DEBUG) \
	  < "$(INPUT)" \
	  > "$(BUILD_DIR)/callgrind.actual.txt"
	@echo "Abrir con: kcachegrind build/callgrind.out"


# ============================================================
# MASSIF
# ============================================================

massif: debug
	@test -f "$(INPUT)" || { \
	  echo "No existe INPUT=$(INPUT)"; \
	  exit 2; \
	}
	valgrind \
	  --tool=massif \
	  --massif-out-file="$(BUILD_DIR)/massif.out" \
	  $(PROGRAM_DEBUG) \
	  < "$(INPUT)" \
	  > "$(BUILD_DIR)/massif.actual.txt"
	@echo "Abrir con: massif-visualizer build/massif.out"


# ============================================================
# LIMPIEZA
# ============================================================

clean:
	rm -rf $(BUILD_DIR)


distclean: clean
	rm -rf \
	  $(PUBLIC_DIR) \
	  $(GEN_DIR) \
	  $(ORACLE_DIR) \
	  $(PROFILER_DIR)
