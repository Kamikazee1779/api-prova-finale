#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Constantes de hashing*/
#define BASE 2
static const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
static const uint64_t FNV_PRIME = 1099511628211ULL;

static const size_t WORD_INITIAL_CAPACITY = 16;
static const size_t MENU_INITIAL_CAPACITY = 32;
static const size_t CLAUSES_INITIAL_CAPACITY = 32;
static const size_t LITERALS_INITIAL_CAPACITY = 32;

/* Estructuras de datos */
typedef struct{
    size_t offset;
    size_t length;
    bool tautological;
} Clause;

typedef struct{
    char *name;    /* OJO apunta dentro de menu_buffer!*/
    int variable;
}HashEntry;

typedef struct { /*Parte 2*/
    int true_count;
    int unassigned_count;
    uint32_t unassigned_xor;
}ClauseState;


/* Parte implementada para solucionar cuellos de botella en tests 6 y 8: MOMS dinámica + fallback adaptativo por concentración de conflictos y evitar cláusulas que se apoderan de todo el poder de computo... */
static const uint64_t ADAPTIVE_FIRST_CHECK = 10000U;
static const uint64_t ADAPTIVE_RESTART_CONFLICTS = 10U;
static const uint64_t ADAPTIVE_RESTART_LIMIT = 2048U;
static const size_t ADAPTIVE_MAX_CONFLICT_CLAUSES = 64U;
static const double ADAPTIVE_MIN_TOP_SHARE = 0.60;
static const double ACTIVITY_DECAY = 0.95;
static const double ACTIVITY_RESCALE_LIMIT = 1e100;
static const double ACTIVITY_RESCALE_FACTOR = 1e-100;

static double *activity_score = NULL;
static uint32_t *moms_score = NULL;
static int *moms_touched = NULL;
static int8_t *saved_value = NULL;
static uint64_t *adaptive_conflict_hits = NULL;

static int dish_count = 0;
static size_t conflict_hits_size = 0;
static bool adaptive_mode = false;
static bool adaptive_restart_requested = false;
static bool adaptive_restarts_enabled = false;
static bool adaptive_last_conflict_valid = false;
static size_t adaptive_last_conflict_clause = 0;
static double activity_increment = 1.0;
static uint64_t adaptive_decisions = 0;
static uint64_t adaptive_next_check = 10000U;
static unsigned int adaptive_check_stage = 0U;
static uint64_t adaptive_conflicts_since_restart = 0;
static uint64_t adaptive_restarts_since_switch = 0;

/* Funciones de APOYO */
size_t nextPowerOfTwo(size_t n); /* Generales */

size_t literal_slot(int lit); /*Parte 2*/
uint32_t literal_code(int lit);
int code_to_literal(uint32_t code);

uint64_t fnv_update(uint64_t hash, char c); /* De hashing */
void hash_insert(HashEntry *hash_table, size_t capacity, char *name, int variable, uint64_t hash);
int hash_lookup(HashEntry *hash_table, size_t capacity, char *word_buffer, uint64_t hash);

/* Funciones principales --> el músculo */
bool parse_menu(char **menu_buffer, FILE *input, int *n, HashEntry **hash_table, size_t *hash_capacity);
void process_word(int8_t *support, HashEntry *hash_table, size_t hash_capacity, char *word_buffer, uint64_t hash, int sign, size_t *touched_count, int *touched, bool *tautological);
bool finish_clause(int8_t *support, int *touched, size_t *touched_count, bool tautological, Clause **clauses, size_t *clauses_size, size_t *clauses_capacity, int **literals, size_t *literals_size, size_t *literals_capacity);
bool parse_employees(char **word_buffer, size_t *word_capacity, size_t *touched_count, int8_t *support, int *touched, Clause **clauses, size_t *clauses_size, size_t *clauses_capacity, int **literals, size_t *literals_size, size_t *literals_capacity, HashEntry *hash_table, size_t hash_capacity);
bool build_solver_static(int n, size_t m, Clause *clauses, int *literals, size_t literals_size, uint32_t *base_xor, size_t *occ_start, size_t *occ_list, size_t *degree);
bool initialize_prefix(int n, size_t active_prefix, Clause *clauses, uint32_t *base_xor, int8_t *assignment, ClauseState *clause_state, int *pending_units, size_t *trail_size, size_t *pending_size, size_t *unsatisfied_count);

/* DPLL */
bool assign_literal(int lit, size_t active_prefix, size_t *occ_start, size_t *occ_list, int8_t *assignment, ClauseState *clause_state, int *trail, size_t *trail_size, int *pending_units, size_t *pending_size, size_t *unsatisfied_count);
void undo_to_checkpoint(size_t checkpoint, size_t active_prefix, size_t *occ_start, size_t *occ_list, int8_t *assignment, ClauseState *clause_state, int *trail, size_t *trail_size, size_t *unsatisfied_count);
bool propagate_units(size_t active_prefix, size_t *occ_start, size_t *occ_list, int8_t *assignment, ClauseState *clause_state, int *trail, size_t *trail_size, int *pending_units, size_t *pending_size, size_t *unsatisfied_count);
int choose_variable(size_t active_prefix, Clause *clauses, int *literals, size_t *occ_start, size_t *occ_list, size_t *degree, int8_t *assignment, ClauseState *clause_state);
bool dpll_search(size_t active_prefix, Clause *clauses, int *literals, size_t *occ_start, size_t *occ_list, size_t *degree, int8_t *preferred_menu, int8_t *assignment, ClauseState *clause_state, int *trail, size_t *trail_size, int *pending_units, size_t *pending_size, size_t *unsatisfied_count);
bool solve_prefix(int n, size_t active_prefix, Clause *clauses, int *literals, uint32_t *base_xor, size_t *occ_start, size_t *occ_list, size_t *degree, int8_t *assignment, ClauseState *clause_state, int *trail, int *pending_units, int8_t *last_valid_menu);
size_t extend_witness_prefix(size_t start_prefix, size_t limit_prefix, Clause *clauses, int *literals, int8_t *last_valid_menu);
size_t find_best_prefix(int n, size_t m, Clause *clauses, int *literals, uint32_t *base_xor, size_t *occ_start, size_t *occ_list, size_t *degree, int8_t *assignment, ClauseState *clause_state, int *trail, int *pending_units, int8_t *last_valid_menu);
void print_result(size_t m, size_t best_prefix);

int main(void)
{
    int n = 0;
    size_t m = 0;
    size_t best_prefix = 0;
    bool ok = true;

    size_t hash_capacity = 0;
    size_t touched_count = 0;
    size_t literals_size = 0;
    size_t literals_capacity = LITERALS_INITIAL_CAPACITY;
    size_t clauses_size = 0;
    size_t clauses_capacity = CLAUSES_INITIAL_CAPACITY;
    size_t word_capacity = WORD_INITIAL_CAPACITY;

    char *menu_buffer = NULL;
    HashEntry *hash_table = NULL;
    int8_t *support = NULL;
    int *touched = NULL;
    int *literals = NULL;
    Clause *clauses = NULL;
    char *word_buffer = NULL;

    uint32_t *base_xor = NULL;
    size_t *occ_start = NULL;
    size_t *occ_list = NULL;
    size_t *degree = NULL;

    int8_t *assignment = NULL;
    ClauseState *clause_state = NULL;
    int *trail = NULL;
    int *pending_units = NULL;
    int8_t *last_valid_menu = NULL;

    /* Fase 1: menú/universo. */
    if (!parse_menu(&menu_buffer, stdin, &n, &hash_table, &hash_capacity)) {
        ok = false; /*Error fatal*/
    }

    /* Fase 2: estructuras de apoyo, pueden ser creadas con tranquilidad dado que ya disponemos de n */
    if (ok) {
        support = calloc((size_t)n + 1, sizeof *support);
        touched = malloc((size_t)n * sizeof *touched);
        clauses = malloc(clauses_capacity * sizeof *clauses);
        literals = malloc(literals_capacity * sizeof *literals);
        word_buffer = malloc(word_capacity * sizeof *word_buffer);

        if (!support || !touched || !clauses || !literals || !word_buffer) {
            fprintf(stderr, "Fatal malloc error!! (apoyos)\n");
            ok = false;
        }
    }

    if (ok) {
        if (!parse_employees(&word_buffer, &word_capacity, &touched_count, support, touched, &clauses, &clauses_size, &clauses_capacity, &literals, &literals_size, &literals_capacity,hash_table, hash_capacity)) {
            ok = false;
        }
    }

    if (ok) {
        m = clauses_size;
    }

    /* El parser terminó: puedo borrar*/
    free(hash_table);
    free(menu_buffer);
    free(word_buffer);
    free(support);
    free(touched);

    if (ok) {
        base_xor = malloc((m + 1) * sizeof *base_xor);
        occ_start = malloc((2 * (size_t)n + 1) * sizeof *occ_start);
        occ_list = malloc((literals_size + 1) * sizeof *occ_list);
        degree = malloc(((size_t)n + 1) * sizeof *degree);
        assignment = calloc((size_t)n + 1, sizeof *assignment);
        clause_state = malloc((m + 1) * sizeof *clause_state);
        trail = malloc(((size_t)n + 1) * sizeof *trail);
        pending_units = malloc((m + 1) * sizeof *pending_units);
        last_valid_menu = malloc(((size_t)n + 1) * sizeof *last_valid_menu);
        activity_score = calloc((size_t)n + 1, sizeof *activity_score);
        moms_score = calloc((size_t)n + 1, sizeof *moms_score);
        moms_touched = malloc(((size_t)n + 1) * sizeof *moms_touched);
        saved_value = malloc(((size_t)n + 1) * sizeof *saved_value);
        adaptive_conflict_hits = calloc(m + 1, sizeof *adaptive_conflict_hits);
        dish_count = n;
        conflict_hits_size = m + 1;
        if (!base_xor || !occ_start || !occ_list || !degree || !assignment || !clause_state || !trail || !pending_units || !last_valid_menu || !activity_score || !moms_score || !moms_touched || !saved_value || !adaptive_conflict_hits) {
            fprintf(stderr, "Fatal malloc error!! (solver)\n");
            ok = false;
        }
    }

    if (ok){
        ok = build_solver_static(n,m,clauses,literals, literals_size, base_xor,occ_start,occ_list, degree);
    }

    if (ok){
        best_prefix = find_best_prefix(n, m, clauses, literals, base_xor, occ_start, occ_list, degree, assignment,clause_state, trail, pending_units, last_valid_menu);
    }

    if (ok) {
        print_result(m, best_prefix);
    }

    free(adaptive_conflict_hits);
    free(saved_value);
    free(moms_touched);
    free(moms_score);
    free(activity_score);
    free(last_valid_menu);
    free(pending_units);
    free(trail);
    free(clause_state);
    free(assignment);
    free(degree);
    free(occ_list);
    free(occ_start);
    free(base_xor);
    free(literals);
    free(clauses);

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Desarrollo funciones apoyo */
size_t nextPowerOfTwo(size_t n){
    size_t capacity = 1;
    while (capacity < n) {
        capacity *= BASE;
    }
    return capacity;
}

size_t literal_slot(int lit) {
    size_t variable;
    if (lit > 0) {
        variable = (size_t)lit;
        return 2 * (variable - 1);
    }
    variable = (size_t)(-lit);
    return 2 * (variable - 1) + 1;
}

uint32_t literal_code(int lit)
{
    uint32_t variable;
    if (lit > 0) {
        variable = (uint32_t)lit;
        return 2 * variable;
    }
    variable = (uint32_t)(-lit);
    return 2 * variable + 1;
}

int code_to_literal(uint32_t code) {
    int variable = (int)(code / 2U);
    if (code % 2U == 0U) {
        return variable;
    }
    return -variable;
}

uint64_t fnv_update(uint64_t hash, char c){
    hash ^= (unsigned char)c;
    hash *= FNV_PRIME;
    return hash;
}

void hash_insert(HashEntry *hash_table, size_t capacity, char *name, int variable, uint64_t hash) {
    size_t slot = hash & (capacity - 1);
    bool ok = false;

    while (!ok){
        if (hash_table[slot].name == NULL){ /* condición que significa, este slot está vacío*/
            hash_table[slot].variable = variable; /* No uso -> porque paso la hash_table como un puntador simple */
            hash_table[slot].name = name;
            ok = true;
        } else{
            slot = (slot + 1) & (capacity - 1); /* linear probing en caso de falla*/
        }
    }
}

int hash_lookup(HashEntry *hash_table, size_t capacity, char *word_buffer, uint64_t hash){
    size_t slot = hash & (capacity - 1);
    bool found = false;

    while (!found){
        if (hash_table[slot].name != NULL && !strcmp(hash_table[slot].name, word_buffer)){
            found = true; /*Lo encontré*/
        }else{
            slot = (slot + 1) & (capacity - 1); /* linear probing */
        }
    }
    return hash_table[slot].variable;
}

bool parse_menu(char **menu_buffer, FILE *input, int *n, HashEntry **hash_table, size_t *hash_capacity){
    size_t i;
    int variable;
    char *c = NULL;       /* exactamente done comienza el nombre actual*/
    char *tmp_buffer;       /*por si hay problemas de realloc*/
    size_t length = 0;
    size_t menu_capacity = MENU_INITIAL_CAPACITY;
    uint64_t hash = FNV_OFFSET_BASIS;
    bool complete_line = false, error = false, change;

    *menu_buffer = malloc(menu_capacity * sizeof **menu_buffer);
    if (!*menu_buffer){
        fprintf(stderr, "Error malloc menu_buffer\n");
        return false;
    }
    (*menu_buffer)[0] = '\0';

    /* Lectura de TODO el universo*/
    while (!error && !complete_line){
        char *piece = fgets(*menu_buffer + length, (int)(menu_capacity - length), input);
        if (!piece){
            if (ferror(input)){
                error = true;
                fprintf(stderr, "Error de streaming\n");
            }else {
                complete_line = true; /*Es decir EOF sin '\n' final --> ya completé la línea */
            }
        }else{
            length += strlen(piece); /* Para no solapar re-lecturas */
            if ((*menu_buffer)[length - 1] == '\n')
            { /* menú nunca vacío --> length > 0 */
                (*menu_buffer)[length - 1] = '\0';
                if (length >= 2 && (*menu_buffer)[length - 2] == '\r'){ /*Por los errores extraños que no podíamos entender en Windows*/
                    (*menu_buffer)[length - 2] = '\0';
                }
                complete_line = true;
            }else{
                menu_capacity *= BASE;
                tmp_buffer = realloc(*menu_buffer, menu_capacity * sizeof **menu_buffer);
                if (!tmp_buffer){
                    fprintf(stderr, "Error realloc menu_buffer redimensionamiento\n");
                    error = true;
                }else{
                    *menu_buffer = tmp_buffer;
                }
            }
        }
    }
    if (error){
        return false;
    }

    /* Primera pasada: contar nombres recorriendo todo menu_buffer y calcular n de este modo*/
    for (i = 0, *n = 0, change = false; (*menu_buffer)[i] != '\0'; i++){
        if ((*menu_buffer)[i] != ' ' && !change){
            change = true;
            (*n)++;
        }else if ((*menu_buffer)[i] == ' ' && change){
            change = false;
        }
    }

    *hash_capacity = nextPowerOfTwo(2 * (size_t)(*n)); /*Buscando tener un buen factor de carga para evitar clusters demasiado agresivos*/
    *hash_table = calloc(*hash_capacity, sizeof **hash_table); /* Porque nos interesa la inicialización a 0*/
    if (!*hash_table){
        fprintf(stderr, "Error malloc hash\n");
        return false;
    }

    /*Segunda pasada*/
    for (i = 0, change = false, variable = 0; (*menu_buffer)[i] != '\0'; i++){
        if ((*menu_buffer)[i] != ' ' && !change){
            c = *menu_buffer + i;
            variable++;
            change = true;
            hash = FNV_OFFSET_BASIS; /* reset del proceso de hashing por nombre */
            hash = fnv_update(hash, (*menu_buffer)[i]);
        }else if ((*menu_buffer)[i] != ' ' && change) {
            hash = fnv_update(hash, (*menu_buffer)[i]);
        }else if ((*menu_buffer)[i] == ' ' && change){
            (*menu_buffer)[i] = '\0';
            change = false;
            hash_insert(*hash_table, *hash_capacity, c, variable, hash);
        }
    }
    if (c != NULL && variable && change){ /* último nombre*/
        hash_insert(*hash_table, *hash_capacity, c, variable, hash);
    }

    return true;
}

void process_word(int8_t *support, HashEntry *hash_table, size_t hash_capacity, char *word_buffer, uint64_t hash, int sign,size_t *touched_count, int *touched, bool *tautological){
    int v = hash_lookup(hash_table, hash_capacity, word_buffer, hash);
    if (support[v] == 0){
        support[v] = (int8_t)sign;
        touched[(*touched_count)++] = v;
    } else if (support[v] == -sign){
        *tautological = true;
    }
}

bool finish_clause(int8_t *support, int *touched, size_t *touched_count, bool tautological,Clause **clauses, size_t *clauses_size, size_t *clauses_capacity,int **literals, size_t *literals_size, size_t *literals_capacity){
    size_t i, required;
    int lit, v;
    int *literals_tmp;
    Clause *clauses_tmp;

    /* Antes que nada, reviso si hay espacio para la cláusula nueva */
    if (*clauses_size >= *clauses_capacity){
        *clauses_capacity *= BASE;
        clauses_tmp = realloc(*clauses, *clauses_capacity * sizeof **clauses);
        if(!clauses_tmp) {
            fprintf(stderr, "Error realloc clauses redimensionamiento\n");
            return false;
        }
        *clauses = clauses_tmp;
    }

    if (tautological){ /*Tratación más simple... */
        (*clauses)[*clauses_size].offset = *literals_size;
        (*clauses)[*clauses_size].length = 0;
        (*clauses)[*clauses_size].tautological = true;

        for (i = 0; i < *touched_count; i++){
            support[touched[i]] = 0;
        }
    }else{
        required = *literals_size + *touched_count;
        if (*literals_capacity < required){
            while (*literals_capacity < required){
                *literals_capacity *= BASE;
            }
            literals_tmp = realloc(*literals, *literals_capacity * sizeof **literals);
            if (!literals_tmp){
                fprintf(stderr, "Error realloc literals redimensionamiento\n");
                return false;
            }
            *literals = literals_tmp;
        }

        (*clauses)[*clauses_size].offset = *literals_size;
        (*clauses)[*clauses_size].length = *touched_count;
        (*clauses)[*clauses_size].tautological = false;

        for(i = 0; i < *touched_count; i++){
            v = touched[i];
            lit = support[v] > 0 ? v : -v;
            (*literals)[*literals_size + i] = lit;
            support[v] = 0; /* dejo listo para la siguiente cláusula */
        }
        *literals_size += *touched_count;
    }

    (*clauses_size)++;
    *touched_count = 0;
    return true;
}

bool parse_employees(char **word_buffer, size_t *word_capacity,size_t *touched_count, int8_t *support, int *touched,Clause **clauses, size_t *clauses_size, size_t *clauses_capacity,int **literals, size_t *literals_size, size_t *literals_capacity,HashEntry *hash_table, size_t hash_capacity)
{
    /* centro de control del estado de la cláusula actual */
    bool tautological = false;
    bool clause_started = false;

    /* centro de control del estado del token actual */
    bool word_active = false;
    size_t word_length = 0;
    int sign = +1;
    uint64_t hash = FNV_OFFSET_BASIS;

    int c;
    char *word_buffer_tmp;
    *touched_count = 0;

    while ((c = fgetc(stdin)) != EOF){
        if (c == '\n' || c == '\r'){
            if (word_active && !tautological){
                (*word_buffer)[word_length] = '\0';
                process_word(support, hash_table, hash_capacity, *word_buffer, hash, sign,touched_count, touched,&tautological);
            }
            if (clause_started){
                if (!finish_clause(support, touched, touched_count, tautological,clauses, clauses_size, clauses_capacity, literals, literals_size, literals_capacity)) {
                    return false;
                }
            }
            /* reset*/
            tautological = false;
            clause_started = false;
            word_active = false;
            word_length = 0;
            sign = +1;
            hash = FNV_OFFSET_BASIS;

        } else if (c == ' '){
            if (word_active && !tautological){
                (*word_buffer)[word_length] = '\0';
                process_word(support, hash_table, hash_capacity, *word_buffer, hash, sign, touched_count, touched, &tautological);
            }
            /* reset pero esta vez ÚNICAMENTE DEL TOKEN */
            word_active = false;
            word_length = 0;
            sign = +1;
            hash = FNV_OFFSET_BASIS;

        }else if (tautological) { /*No haga un puto culo (fast-skipping)*/

        }else{
            clause_started = true;

            if (!word_active){
                word_active = true; /* Es decir, me encuentro al comienzo de un token */
                if (c == '-'){
                    sign = -1; /* asignación directa, no toggle */
                }else{
                    if (word_length + 1 >= *word_capacity){ /* reservar sitio para '\0' */
                        *word_capacity *= BASE;
                        word_buffer_tmp = realloc(*word_buffer, *word_capacity * sizeof **word_buffer);
                        if (!word_buffer_tmp){
                            fprintf(stderr, "Error realloc token_buffer redimensionamiento\n");
                            return false;
                        }
                        *word_buffer = word_buffer_tmp;
                    }
                    (*word_buffer)[word_length++] = (char)c; /*Porque lo tenía como int para poder detectar bien el EOF... hago casting directo y sale*/
                    hash = fnv_update(hash, (char)c);
                }
            }else{ /* tiempo ordinario*/
                if (word_length + 1 >= *word_capacity) {
                    *word_capacity *= BASE;
                    word_buffer_tmp = realloc(*word_buffer, *word_capacity * sizeof **word_buffer);
                    if (!word_buffer_tmp){
                        fprintf(stderr, "Error realloc token_buffer redimensionamiento\n");
                        return false;
                    }
                    *word_buffer = word_buffer_tmp;
                }
                (*word_buffer)[word_length++] = (char)c;
                hash = fnv_update(hash, (char)c);
            }
        }
    }

    if (word_active && !tautological) {
        (*word_buffer)[word_length] = '\0';
        process_word(support, hash_table, hash_capacity, *word_buffer, hash, sign, touched_count, touched, &tautological);
    }
    if (clause_started){ /*Para no comerme la eventual palabra que acabo de procesar*/
        if(!finish_clause(support, touched, touched_count, tautological, clauses, clauses_size, clauses_capacity, literals, literals_size, literals_capacity)) {
            return false;
        }
    }

    return true;
}

bool build_solver_static(int n, size_t m, Clause *clauses, int *literals, size_t literals_size, uint32_t *base_xor, size_t *occ_start, size_t *occ_list, size_t *degree){
    size_t i, j;
    size_t start, end;
    size_t slot;
    size_t positive_slot, negative_slot;
    size_t total_slots = 2 * (size_t)n;
    size_t *count = NULL;
    size_t *cursor = NULL;
    bool ok = true;

    count = calloc(total_slots, sizeof *count);
    cursor = malloc(total_slots * sizeof *cursor);
    if (!count || !cursor) {
        fprintf(stderr, "Fatal malloc error!! (solver static temporales)\n");
        ok = false;
    }

    /* Primera pasada*/
    if (ok){
        for (i = 0; i < m; i++){
            base_xor[i] = 0;
            if (!clauses[i].tautological){
                start = clauses[i].offset;
                end = start + clauses[i].length;
                for (j = start; j < end; j++){
                    int lit = literals[j];
                    slot = literal_slot(lit);
                    count[slot]++;
                    base_xor[i] ^= literal_code(lit);
                }
            }
        }
    }

    if (ok){
        degree[0] = 0;
        for (i = 1; i <= (size_t)n; i++){
            positive_slot = 2 * (i - 1);
            negative_slot = positive_slot + 1;
            degree[i] = count[positive_slot] + count[negative_slot];
        }
    }

    if (ok){
        occ_start[0] = 0;
        for(slot = 0; slot < total_slots; slot++) {
            occ_start[slot + 1] = occ_start[slot] + count[slot];
            cursor[slot] = occ_start[slot];
        }
    }

    /* Invariante global CSR: cada literal del parser fue contado exactamente una vez */
    if (ok){
        if (occ_start[total_slots] != literals_size){
            fprintf(stderr, "Internal error building occurrences\n");
            ok = false;
        }
    }

    /* Segunda pasada*/
    if (ok){
        for (i = 0; i < m; i++){
            if  (!clauses[i].tautological){
                start = clauses[i].offset;
                end = start + clauses[i].length;
                for (j = start; j < end; j++){
                    int lit = literals[j];
                    slot = literal_slot(lit);
                    occ_list[cursor[slot]] = i;
                    cursor[slot]++;
                }
            }
        }
    }

    /* Sanity check: OJO A QUITARLO DESPUÉS!! */
    slot = 0;
    while (ok && slot < total_slots){
        if (cursor[slot] != occ_start[slot + 1]){
            fprintf(stderr, "Internal error filling occurrences\n");
            ok = false;
        }
        slot++;
    }

    free(count);
    free(cursor);
    return ok;
}

bool initialize_prefix(int n, size_t active_prefix, Clause *clauses, uint32_t *base_xor, int8_t *assignment, ClauseState *clause_state, int *pending_units, size_t *trail_size, size_t *pending_size, size_t *unsatisfied_count){
    size_t i = 0;
    bool consistent = true;
    *trail_size = 0;
    *pending_size = 0;
    *unsatisfied_count = 0;
    memset(assignment, 0, ((size_t)n + 1) * sizeof *assignment);
    while (i < active_prefix){
        if (clauses[i].tautological == true){
            clause_state[i].true_count = 1;
            clause_state[i].unassigned_count = 0;
            clause_state[i].unassigned_xor = 0;
        } else{
            clause_state[i].true_count = 0;
            clause_state[i].unassigned_count = (int)clauses[i].length;
            clause_state[i].unassigned_xor = base_xor[i];
            (*unsatisfied_count)++;
            if (clauses[i].length == 0){
                consistent = false;
            } else if (clauses[i].length == 1){
                pending_units[*pending_size] = code_to_literal(base_xor[i]);
                (*pending_size)++;
            }
        }
        i++;
    }
    return consistent;
}

/* Heurística adaptativa,
    NOTAS PARA EL EMILIO DEL FUTURO:
    - Esta parte fue la que incluímos para intentar solucionar los problemas de potencia de computo que descubirmos con tests 6 y 8
    - Es la soución híbrida que mejores resultados nos dieron!*/
static void adaptive_clear_conflicts(void){
    if (adaptive_conflict_hits && conflict_hits_size > 0){
        memset(adaptive_conflict_hits, 0, conflict_hits_size * sizeof *adaptive_conflict_hits);
    }
}

static bool adaptive_conflicts_concentrated(size_t active_prefix){
    size_t i, j, position;
    uint64_t total = 0;
    uint64_t unique = 0;
    uint64_t top_sum = 0;
    uint64_t hits;
    uint64_t top_hits[6] = {0, 0, 0, 0, 0, 0};

    if (!adaptive_conflict_hits || active_prefix == 0){
        return false;
    }

    for (i = 0; i < active_prefix; i++){
        hits = adaptive_conflict_hits[i];
        total += hits;
        if (hits > 0){
            unique++;
            position = 0;
            while (position < 6 && hits <= top_hits[position]){
                position++;
            }
            if (position < 6){
                j = 5;
                while (j > position){
                    top_hits[j] = top_hits[j - 1];
                    j--;
                }
                top_hits[position] = hits;
            }
        }
    }

    if (total == 0 || unique == 0){
        return false;
    }

    for (i = 0; i < 6; i++){
        top_sum += top_hits[i];
    }

    return unique <= ADAPTIVE_MAX_CONFLICT_CLAUSES &&
           (double)top_sum / (double)total >= ADAPTIVE_MIN_TOP_SHARE;
}

static void adaptive_seed_activity(size_t active_prefix, Clause *clauses, int *literals){
    size_t i, j, start, end;
    uint64_t hits;
    int lit, variable;

    memset(activity_score, 0, ((size_t)dish_count + 1) * sizeof *activity_score);
    activity_increment = 1.0;

    for (i = 0; i < active_prefix; i++){
        hits = adaptive_conflict_hits[i];
        if (hits > 0 && !clauses[i].tautological){
            start = clauses[i].offset;
            end = start + clauses[i].length;
            for (j = start; j < end; j++){
                lit = literals[j];
                variable = lit > 0 ? lit : -lit;
                activity_score[variable] += (double)hits;
            }
        }
    }
}

static void adaptive_schedule_next_check(void){
    if (adaptive_check_stage == 0U){
        adaptive_next_check = 50000U;
    }else if (adaptive_check_stage == 1U){
        adaptive_next_check = 200000U;
    }else if (adaptive_check_stage == 2U){
        adaptive_next_check = 1000000U;
    }else if (adaptive_next_check <= UINT64_MAX / 4U){
        adaptive_next_check *= 4U;
    }else{
        adaptive_next_check = UINT64_MAX;
    }
    adaptive_check_stage++;
}

static void adaptive_bump_conflict(Clause *clauses, int *literals){
    size_t j, start, end;
    int i, lit, variable;

    if (!adaptive_last_conflict_valid){
        return;
    }

    if (adaptive_mode){
        start = clauses[adaptive_last_conflict_clause].offset;
        end = start + clauses[adaptive_last_conflict_clause].length;
        for (j = start; j < end; j++){
            lit = literals[j];
            variable = lit > 0 ? lit : -lit;
            activity_score[variable] += activity_increment;
        }

        activity_increment /= ACTIVITY_DECAY;
        adaptive_conflicts_since_restart++;
        if (adaptive_restarts_enabled && adaptive_conflicts_since_restart >= ADAPTIVE_RESTART_CONFLICTS){
            adaptive_restart_requested = true;
        }

        if (activity_increment > ACTIVITY_RESCALE_LIMIT){
            for (i = 1; i <= dish_count; i++){
                activity_score[i] *= ACTIVITY_RESCALE_FACTOR;
            }
            activity_increment *= ACTIVITY_RESCALE_FACTOR;
        }
    }

    adaptive_last_conflict_valid = false;
}

/*DPLL */

bool assign_literal(int lit, size_t active_prefix, size_t *occ_start, size_t *occ_list, int8_t *assignment, ClauseState *clause_state, int *trail, size_t *trail_size, int *pending_units, size_t *pending_size, size_t *unsatisfied_count){
    int variable;
    int8_t wanted_value;
    int opposite;
    size_t slot, i, occ_end, c;
    uint32_t code;
    bool was_unsatisfied;
    bool conflict = false;

    /* 1A: se interpreta el literal */
    if (lit > 0){
        variable = lit;
        wanted_value = +1;
    }else{
        variable = -lit;
        wanted_value = -1;
    }

    /* 1B: se verifica si ya estaba asignada */
    if (assignment[variable] == wanted_value){
        return true;
    }
    if (assignment[variable] != 0){
        return false;
    }

    /* 1C: se registra una asignación nueva */
    if (adaptive_mode){
        saved_value[variable] = wanted_value;
    }
    assignment[variable] = wanted_value;
    trail[*trail_size] = lit;
    (*trail_size)++;

    /* 1D: ocurrencias de lit, que me lleva a UNASSIGNED -> TRUE */
    slot = literal_slot(lit);
    code = literal_code(lit);
    i = occ_start[slot];
    occ_end = occ_start[slot + 1];
    while (i < occ_end && occ_list[i] < active_prefix){
        c = occ_list[i];
        was_unsatisfied = (clause_state[c].true_count == 0);
        clause_state[c].unassigned_count--;
        clause_state[c].unassigned_xor ^= code;
        clause_state[c].true_count++;
        if (was_unsatisfied){
            (*unsatisfied_count)--;
        }
        i++;
    }

    /* 1E: -lit --> UNASSIGNED -> FALSE*/
    opposite = -lit;
    slot = literal_slot(opposite);
    code = literal_code(opposite);
    i = occ_start[slot];
    occ_end = occ_start[slot + 1];
    while (i < occ_end && occ_list[i] < active_prefix){
        c = occ_list[i];
        clause_state[c].unassigned_count--;
        clause_state[c].unassigned_xor ^= code;
        if (clause_state[c].true_count == 0) {
            if (clause_state[c].unassigned_count == 0){
                if (!adaptive_mode && adaptive_conflict_hits && c < conflict_hits_size){
                    adaptive_conflict_hits[c]++;
                }
                adaptive_last_conflict_clause = c;
                adaptive_last_conflict_valid = true;
                conflict = true; /* pero se sigue acá!!!!*/
            }else if (clause_state[c].unassigned_count == 1){
                pending_units[*pending_size] = code_to_literal(clause_state[c].unassigned_xor);
                (*pending_size)++;
            }
        }
        i++;
    }

    return !conflict;
}

void undo_to_checkpoint(size_t checkpoint, size_t active_prefix, size_t *occ_start, size_t *occ_list, int8_t *assignment, ClauseState *clause_state, int *trail, size_t *trail_size, size_t *unsatisfied_count){
    int lit, opposite, variable;
    size_t slot, i, occ_end, c;
    uint32_t code;

    while (*trail_size > checkpoint){
        lit = trail[*trail_size - 1];
        slot = literal_slot(lit);
        code = literal_code(lit);
        i = occ_start[slot];
        occ_end = occ_start[slot + 1];
        while (i < occ_end && occ_list[i] < active_prefix){
            c = occ_list[i];
            if (clause_state[c].true_count == 1){ /*deja de estar satisfecha */
                (*unsatisfied_count)++;
            }
            clause_state[c].true_count--;
            clause_state[c].unassigned_count++;
            clause_state[c].unassigned_xor ^= code;
            i++;
        }

        opposite = -lit;
        slot = literal_slot(opposite);
        code = literal_code(opposite);
        i = occ_start[slot];
        occ_end = occ_start[slot + 1];
        while (i < occ_end && occ_list[i] < active_prefix){
            c = occ_list[i];
            clause_state[c].unassigned_count++;
            clause_state[c].unassigned_xor ^= code;
            i++;
        }

        variable = lit > 0 ? lit : -lit;
        assignment[variable] = 0;
        (*trail_size)--;
    }
}

bool propagate_units(size_t active_prefix, size_t *occ_start, size_t *occ_list, int8_t *assignment, ClauseState *clause_state, int *trail, size_t *trail_size, int *pending_units, size_t *pending_size, size_t *unsatisfied_count){
    size_t i = 0;
    int lit;
    bool consistent = true;

    while (consistent && i < *pending_size){
        lit = pending_units[i];
        i++;
        consistent = assign_literal(lit, active_prefix, occ_start, occ_list, assignment, clause_state, trail, trail_size, pending_units, pending_size, unsatisfied_count);
    }
    *pending_size = 0;
    return consistent;
}

int choose_variable(size_t active_prefix, Clause *clauses, int *literals, size_t *occ_start, size_t *occ_list, size_t *degree, int8_t *assignment, ClauseState *clause_state){
    size_t i, j, start, end;
    int lit, variable;
    int best_variable = 0;
    size_t best_degree = 0;

    if (!adaptive_mode){ /*Pate  en la que se implementa la nueva MOMS dinámica */
        int min_unassigned = 0;
        uint32_t best_score = 0;
        bool found = false;
        size_t touched_count = 0;
        size_t k;

        for (i = 0; i < active_prefix; i++){
            if (clause_state[i].true_count == 0 && clause_state[i].unassigned_count > 0){
                if (!found || clause_state[i].unassigned_count < min_unassigned){
                    for (k = 0; k < touched_count; k++){
                        moms_score[moms_touched[k]] = 0;
                    }
                    touched_count = 0;
                    min_unassigned = clause_state[i].unassigned_count;
                    found = true;
                }

                if (clause_state[i].unassigned_count == min_unassigned){
                    start = clauses[i].offset;
                    end = start + clauses[i].length;
                    for (j = start; j < end; j++){
                        lit = literals[j];
                        variable = lit > 0 ? lit : -lit;
                        if (assignment[variable] == 0){
                            if (moms_score[variable] == 0){
                                moms_touched[touched_count] = variable;
                                touched_count++;
                            }
                            moms_score[variable]++;
                        }
                    }
                }
            }
        }

        if (found){
            for (k = 0; k < touched_count; k++){
                variable = moms_touched[k];
                if (best_variable == 0 || moms_score[variable] > best_score ||
                    (moms_score[variable] == best_score && degree[variable] > best_degree) ||
                    (moms_score[variable] == best_score && degree[variable] == best_degree && variable < best_variable)){
                    best_variable = variable;
                    best_score = moms_score[variable];
                    best_degree = degree[variable];
                }
            }

            for (k = 0; k < touched_count; k++){
                moms_score[moms_touched[k]] = 0;
            }
        }
    } else { /* fallback: que representa la actividad acumulada por los conflictos  */
        double best_activity = 0.0;
        bool relevant = false;
        size_t slot, position, occ_end, c;

        for (variable = 1; variable <= dish_count; variable++){
            if (assignment[variable] == 0){
                if (best_variable == 0 || activity_score[variable] > best_activity ||
                    (activity_score[variable] == best_activity && degree[variable] > best_degree)){
                    best_variable = variable;
                    best_activity = activity_score[variable];
                    best_degree = degree[variable];
                }
            }
        }

        if (best_variable != 0){
            slot = literal_slot(best_variable);
            position = occ_start[slot];
            occ_end = occ_start[slot + 1];
            while (!relevant && position < occ_end && occ_list[position] < active_prefix){
                c = occ_list[position];
                if (clause_state[c].true_count == 0 && clause_state[c].unassigned_count > 0){
                    relevant = true;
                }
                position++;
            }

            if (!relevant){
                slot = literal_slot(-best_variable);
                position = occ_start[slot];
                occ_end = occ_start[slot + 1];
                while (!relevant && position < occ_end && occ_list[position] < active_prefix){
                    c = occ_list[position];
                    if (clause_state[c].true_count == 0 && clause_state[c].unassigned_count > 0){
                        relevant = true;
                    }
                    position++;
                }
            }
        }

        if (!relevant){
            best_variable = 0;
            best_degree = 0;
            best_activity = 0.0;
            for(i = 0; i < active_prefix; i++){
                if (clause_state[i].true_count == 0 && clause_state[i].unassigned_count > 0){
                    start = clauses[i].offset;
                    end = start + clauses[i].length;
                    for (j = start; j < end; j++){
                        lit = literals[j];
                        variable = lit > 0 ? lit : -lit;
                        if (assignment[variable] == 0){
                            if (best_variable == 0 || activity_score[variable] > best_activity ||
                                (activity_score[variable] == best_activity && degree[variable] > best_degree)){
                                best_variable = variable;
                                best_activity = activity_score[variable];
                                best_degree = degree[variable];
                            }
                        }
                    }
                }
            }
        }
    }

    return best_variable;
}

bool dpll_search(size_t active_prefix, Clause *clauses, int *literals, size_t *occ_start, size_t *occ_list, size_t *degree, int8_t *preferred_menu, int8_t *assignment, ClauseState *clause_state, int *trail, size_t *trail_size, int *pending_units, size_t *pending_size, size_t *unsatisfied_count){
    int variable, first_literal;
    size_t checkpoint;
    bool consistent, sat;

    if (adaptive_restart_requested){
        return false;
    }

    if (*unsatisfied_count == 0){
        return true;
    }

    variable = choose_variable(active_prefix, clauses, literals, occ_start, occ_list, degree, assignment, clause_state);
    if (variable == 0){
        return false;
    }

    adaptive_decisions++;
    if (!adaptive_mode && adaptive_decisions >= adaptive_next_check){
        if (adaptive_conflicts_concentrated(active_prefix)){
            adaptive_seed_activity(active_prefix, clauses, literals);
            adaptive_mode = true;
            adaptive_restarts_since_switch = 0;
            adaptive_restarts_enabled = true;
            adaptive_restart_requested = true;
            adaptive_conflicts_since_restart = 0;
            adaptive_clear_conflicts();
            return false;
        }
        adaptive_clear_conflicts();
        adaptive_schedule_next_check();
    }

    if (adaptive_mode){
        if (saved_value[variable] < 0) {
            first_literal = -variable;
        }else {
            first_literal = variable;
        }
    }else if (preferred_menu[variable] < 0){
        first_literal = -variable;
    } else{
        first_literal = variable;
    }

    checkpoint = *trail_size;

    /* Empiezo proceso con la RAMA 1 */
    adaptive_last_conflict_valid = false;
    consistent = assign_literal(first_literal, active_prefix, occ_start, occ_list, assignment, clause_state, trail, trail_size, pending_units, pending_size, unsatisfied_count);
    if (!consistent){
        adaptive_bump_conflict(clauses, literals);
    }
    if (consistent){
        consistent = propagate_units(active_prefix, occ_start, occ_list, assignment, clause_state, trail, trail_size, pending_units, pending_size, unsatisfied_count);
        if (!consistent){
            adaptive_bump_conflict(clauses, literals);
        }
    }
    if (consistent){
        sat = dpll_search(active_prefix, clauses, literals, occ_start, occ_list, degree, preferred_menu, assignment, clause_state, trail, trail_size, pending_units, pending_size, unsatisfied_count);
        if (sat){
            return true;
        }
    }
    *pending_size = 0;
    undo_to_checkpoint(checkpoint, active_prefix, occ_start, occ_list, assignment, clause_state, trail, trail_size, unsatisfied_count);
    if (adaptive_restart_requested){
        return false;
    }

    /* Ahora sigo con la 2 */
    adaptive_last_conflict_valid = false;
    consistent = assign_literal(-first_literal, active_prefix, occ_start, occ_list, assignment, clause_state, trail, trail_size, pending_units, pending_size, unsatisfied_count);
    if (!consistent){
        adaptive_bump_conflict(clauses, literals);
    }
    if (consistent){
        consistent = propagate_units(active_prefix, occ_start, occ_list, assignment, clause_state, trail, trail_size, pending_units, pending_size, unsatisfied_count);
        if (!consistent) {
            adaptive_bump_conflict(clauses, literals);
        }
    }
    if (consistent){
        sat = dpll_search(active_prefix, clauses, literals, occ_start, occ_list, degree, preferred_menu, assignment, clause_state, trail, trail_size, pending_units, pending_size, unsatisfied_count);
        if (sat){
            return true;
        }
    }
    *pending_size = 0;
    undo_to_checkpoint(checkpoint, active_prefix, occ_start, occ_list, assignment, clause_state, trail, trail_size, unsatisfied_count);

    return false;
}

bool solve_prefix(int n, size_t active_prefix, Clause *clauses, int *literals, uint32_t *base_xor, size_t *occ_start, size_t *occ_list, size_t *degree, int8_t *assignment, ClauseState *clause_state, int *trail, int *pending_units, int8_t *last_valid_menu){
    size_t trail_size = 0;
    size_t pending_size = 0;
    size_t unsatisfied_count = 0;
    bool consistent, sat;
    bool searching = true;
    int i;

    adaptive_mode = false;
    adaptive_restart_requested = false;
    adaptive_restarts_enabled = false;
    adaptive_last_conflict_valid = false;
    adaptive_decisions = 0;
    adaptive_next_check = ADAPTIVE_FIRST_CHECK;
    adaptive_check_stage = 0U;
    adaptive_conflicts_since_restart = 0;
    adaptive_restarts_since_switch = 0;
    activity_increment = 1.0;
    memset(activity_score, 0, ((size_t)n + 1) * sizeof *activity_score);
    memcpy(saved_value, last_valid_menu, ((size_t)n + 1) * sizeof *saved_value);
    adaptive_clear_conflicts();

    sat = false;
    consistent = true;
    while (searching){
        trail_size = 0;
        pending_size = 0;
        unsatisfied_count = 0;
        adaptive_restart_requested = false;
        adaptive_conflicts_since_restart = 0;

        consistent = initialize_prefix(n, active_prefix, clauses, base_xor, assignment, clause_state, pending_units, &trail_size, &pending_size, &unsatisfied_count);
        if (consistent){
            consistent = propagate_units(active_prefix, occ_start, occ_list, assignment, clause_state, trail, &trail_size, pending_units, &pending_size, &unsatisfied_count);
        }

        if (!consistent){
            searching = false;
            sat = false;
        }else if (unsatisfied_count == 0){
            searching = false;
            sat = true;
        }else{
            sat = dpll_search(active_prefix, clauses, literals, occ_start, occ_list, degree, last_valid_menu, assignment, clause_state, trail, &trail_size, pending_units, &pending_size, &unsatisfied_count);
            if (sat){
                searching = false;
            } else if (adaptive_restart_requested){
                adaptive_restarts_since_switch++;
                if (adaptive_restarts_since_switch >= ADAPTIVE_RESTART_LIMIT){
                    adaptive_restarts_enabled = false;
                }
            }else{
                searching = false;
            }
        }
    }

    if (sat){
        for (i = 1; i <= n; i++){
            if (assignment[i] == 0){
                assignment[i] = +1; /*UNASSIGNED -> TRUE, sólo después de SAT  */
            }
        }
        for (i = 0; i <= n; i++){
            last_valid_menu[i] = assignment[i];
        }
    }

    return sat;
}


size_t extend_witness_prefix(size_t start_prefix, size_t limit_prefix, Clause *clauses, int *literals, int8_t *last_valid_menu){
    size_t prefix = start_prefix;
    size_t c, j, start, end;
    int lit, variable;
    bool extending = true;
    bool satisfied;

    while (extending && prefix < limit_prefix){
        c = prefix;
        if (clauses[c].tautological) {
            satisfied = true;
        } else{
            satisfied = false;
            start = clauses[c].offset;
            end = start + clauses[c].length;
            j = start;
            while (!satisfied && j < end){
                lit = literals[j];
                if (lit > 0) {
                    variable = lit;
                    if (last_valid_menu[variable] == +1){
                        satisfied = true;
                    }
                } else {
                    variable = -lit;
                    if (last_valid_menu[variable] == -1){
                        satisfied = true;
                    }
                }
                j++;
            }
        }
        if (satisfied){
            prefix++;
        } else {
            extending = false;
        }
    }

    return prefix;
}

size_t find_best_prefix(int n, size_t m, Clause *clauses, int *literals, uint32_t *base_xor, size_t *occ_start, size_t *occ_list, size_t *degree, int8_t *assignment, ClauseState *clause_state, int *trail, int *pending_units, int8_t *last_valid_menu){
    size_t low, high, mid;
    int i;
    bool sat;

    /*acá entra nuestro witness inicial*/
    last_valid_menu[0] = 0;
    for (i = 1; i <= n; i++){
        last_valid_menu[i] = +1;
    }

    sat = solve_prefix(n, m, clauses, literals, base_xor, occ_start, occ_list, degree, assignment, clause_state, trail, pending_units, last_valid_menu);
    if (sat){
        return m;
    }

    low = 0;
    high = m;
    while (high > low + 1){
        if (low == 0){
            mid = low + (high - low) / 2;
        }else{
            mid = low + (high - low) / 4;
            if (mid == low){
                mid = low + 1;
            }
        }
        sat = solve_prefix(n, mid, clauses, literals, base_xor, occ_start, occ_list, degree, assignment, clause_state, trail, pending_units, last_valid_menu);
        if(sat) {
            low = mid;
            low = extend_witness_prefix(low, high, clauses, literals, last_valid_menu);
        }else{
            high = mid;
        }
    }

    return low;
}

/* Ya la parte final hijueputaaaa */
void print_result(size_t m, size_t best_prefix){
    size_t i;

    if (best_prefix == m){
        printf("OK\n");
    } else{
        printf("KO\n");
        i = 1;
        while (i <= m - best_prefix){
            printf("-%zu\n", i);
            i++;
        }
        printf("OK\n");
    }
}
