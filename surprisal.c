// surprisal.c: text classifier with a hand-rolled lz77 cost counter
// gcc -O2 surprisal.c -o surprisal -lm
//
// ./surprisal -d en=data/en.txt -d de=data/de.txt "some text"
// ./surprisal -d en=data/en.txt -d de=data/de.txt --test
// ./surprisal -d en=data/en.txt -d de=data/de.txt --test --split 0.5
//
// -d [name=]file   reference text for one class (name defaults to the file name)
// --test           measure accuracy: first part of each file is the reference,
//                  sentences from the rest are the queries
// --split x        how much of each file is reference in test mode (default 0.75)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

typedef struct {
    char* name;
    char* text;
    size_t size;
    double lit[256];  // price of each raw byte in bits
} Class;

double now_ms() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

char* read_file(const char* path, size_t* n) {
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char* buf = malloc(size);
    if (fread(buf, 1, size, f) != (size_t)size) { perror("fread"); exit(1); }
    fclose(f);
    *n = size;
    return buf;
}

// only touches A-Z, utf-8 bytes are left alone
void lower(char* s, size_t n) {
    for (size_t i = 0; i < n; i++) s[i] = tolower((unsigned char)s[i]);
}

// ---- compression side ----

// raw byte price = 1 flag bit + -log2(p), p from this class's byte counts
// +1 / +256 so a byte never seen still has a finite price
void build_lit(Class* c) {
    double count[256] = {0};
    for (size_t i = 0; i < c->size; i++) count[(unsigned char)c->text[i]]++;
    for (int b = 0; b < 256; b++)
        c->lit[b] = 1 - log2((count[b] + 1.0) / (c->size + 256.0));
}

// bits to encode s given the class text, greedy lz77
// matches can point into the text or into the part of s already encoded
double cost_bits(Class* c, char* s, size_t sn) {
    size_t dn = c->size;
    char* buf = malloc(dn + sn);
    memcpy(buf, c->text, dn);
    memcpy(buf + dn, s, sn);

    double bits = 0;
    size_t i = 0;
    while (i < sn) {
        size_t pos = dn + i, best = 0;
        for (size_t j = 0; j < pos; j++) {
            size_t l = 0;
            while (i + l < sn && buf[j + l] == buf[pos + l]) l++;
            if (l > best) best = l;
        }
        if (best > 127) best = 127;  // 7 length bits

        // 1 flag + distance bits + 7 length bits
        double match_bits = 1 + ceil(log2((double)pos)) + 7;
        double raw_bits = 0;
        for (size_t t = 0; t < best; t++) raw_bits += c->lit[(unsigned char)s[i + t]];

        if (best >= 3 && match_bits < raw_bits) {
            bits += match_bits;
            i += best;
        } else {
            bits += c->lit[(unsigned char)s[i]];
            i++;
        }
    }
    free(buf);
    return bits;
}

// cheapest class wins, returns its index
int classify(Class* classes, int n, char* s, double* cost) {
    int best = 0;
    for (int k = 0; k < n; k++) {
        cost[k] = cost_bits(&classes[k], s, strlen(s));
        if (cost[k] < cost[best]) best = k;
    }
    return best;
}

// ---- demo ----

// the query isn't from the files, so every class uses its whole file
void demo(Class* classes, int n, char* query) {
    double cost[n];
    lower(query, strlen(query));

    for (int k = 0; k < n; k++) {
        printf("%-12s %zu bytes\n", classes[k].name, classes[k].size);
    }
    printf("\n");

    double start = now_ms();
    int p = classify(classes, n, query, cost);
    double query_ms = now_ms() - start;

    for (int k = 0; k < n; k++)
        printf("%-12s %7.0f bits%s\n", classes[k].name, cost[k], k == p ? "  <--" : "");
    printf("\nquery %.1f ms\n", query_ms);
}

// ---- test (just for info) ----

// TEST
const double default_split = 0.75;
// TEST
const int min_len = 40;
// TEST
const int max_len = 160;

// first part of each file is the reference, sentences from the rest are the queries
void test(Class* classes, int n, double split) {
    int* conf = calloc(n * n, sizeof(int));
    size_t full[n];
    double cost[n];
    int ok = 0, total = 0;

    for (int k = 0; k < n; k++) {
        full[k] = classes[k].size;
        classes[k].size = full[k] * split;
        build_lit(&classes[k]);
        printf("%-12s %zu bytes reference, %zu bytes to test on\n",
               classes[k].name, classes[k].size, full[k] - classes[k].size);
    }
    printf("\n");

    double start = now_ms();
    for (int t = 0; t < n; t++) {
        char* rest = classes[t].text + classes[t].size;
        size_t rn = full[t] - classes[t].size;

        // skip the half sentence at the start
        size_t st = 0;
        while (st < rn && rest[st] != '.') st++;
        st++;

        for (size_t i = st; i < rn; i++) {
            if (rest[i] != '.' && rest[i] != '!' && rest[i] != '?') continue;

            size_t a = st;
            while (a < i && rest[a] == ' ') a++;
            size_t len = i - a;
            st = i + 1;
            if (len < (size_t)min_len || len > (size_t)max_len) continue;

            char buf[256];
            memcpy(buf, rest + a, len);
            buf[len] = 0;

            int p = classify(classes, n, buf, cost);
            conf[t * n + p]++;
            ok += (p == t);
            total++;
        }
    }
    double ms = now_ms() - start;

    printf("accuracy: %d/%d = %.1f%%\n\n", ok, total, 100.0 * ok / total);
    printf("%-12s", "true\\pred");
    for (int k = 0; k < n; k++) printf("%8s", classes[k].name);
    printf("\n");
    for (int t = 0; t < n; t++) {
        printf("%-12s", classes[t].name);
        for (int p = 0; p < n; p++) printf("%8d", conf[t * n + p]);
        printf("\n");
    }
    printf("\n%d queries in %.0f ms, %.2f ms per query\n", total, ms, ms / total);
    free(conf);
}

// ---- args ----

// "name=file" or just "file", name defaults to the file name without its folders
void split_arg(char* arg, char** name, char** file) {
    char* eq = strchr(arg, '=');
    if (eq) {
        *eq = 0;
        *name = arg;
        *file = eq + 1;
    } else {
        char* slash = strrchr(arg, '/');
        *name = slash ? slash + 1 : arg;
        *file = arg;
    }
}

int main(int argc, char** argv) {
    Class* classes = calloc(argc, sizeof(Class));
    char* query = NULL;
    int n = 0, testing = 0;
    double split = default_split;

    double start = now_ms();
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            char *name, *file;
            split_arg(argv[++i], &name, &file);
            classes[n].name = name;
            classes[n].text = read_file(file, &classes[n].size);
            lower(classes[n].text, classes[n].size);
            n++;
        } else if (strcmp(argv[i], "--test") == 0) {
            testing = 1;
        } else if (strcmp(argv[i], "--split") == 0 && i + 1 < argc) {
            split = atof(argv[++i]);
        } else {
            query = argv[i];
        }
    }
    double load_ms = now_ms() - start;

    if (n < 2 || (!testing && !query) || split <= 0 || split >= 1) {
        printf("usage: %s -d [name=]file -d [name=]file ... \"text\"\n", argv[0]);
        printf("       %s -d [name=]file -d [name=]file ... --test [--split 0.75]\n", argv[0]);
        return 1;
    }

    if (testing) {
        test(classes, n, split);
    } else {
        for (int k = 0; k < n; k++) build_lit(&classes[k]);
        demo(classes, n, query);
    }
    printf("load %.1f ms\n", load_ms);

    for (int k = 0; k < n; k++) free(classes[k].text);
    free(classes);
    return 0;
}
