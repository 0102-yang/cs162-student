/*
 * Implementation of the word_count interface using Pintos lists.
 *
 * You may modify this file, and are expected to modify it.
 */

/*
 * Copyright © 2021 University of California, Berkeley
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PINTOS_LIST
#error "PINTOS_LIST must be #define'd when compiling word_count_l.c"
#endif

#include "word_count.h"

void init_words(word_count_list_t *wclist) { list_init(wclist); }

size_t len_words(word_count_list_t *wclist) {
  size_t len = 0;
  for (struct list_elem *e = list_begin(wclist); e != list_end(wclist);
       e = list_next(e)) {
    len++;
  }
  return len;
}

word_count_t *find_word(word_count_list_t *wclist, char *word) {
  for (struct list_elem *e = list_begin(wclist); e != list_end(wclist);
       e = list_next(e)) {
    word_count_t *wc = list_entry(e, word_count_t, elem);
    char *store_word = wc->word;
    if (strcmp(store_word, word) == 0) {
      return wc;
    }
  }
  return NULL;
}

word_count_t *add_word(word_count_list_t *wclist, char *word) {
  word_count_t *word_entry = find_word(wclist, word);
  if (word_entry) {
    /* If present. */
    word_entry->count++;
    return word_entry;
  } else {
    /* Create and insert new word. */
    word_count_t *new_word_entry = (word_count_t *)malloc(sizeof(word_count_t));
    new_word_entry->count = 1;
    size_t word_capacity = strlen(word) + 1;
    new_word_entry->word = strcpy(malloc(sizeof(char) * word_capacity), word);
    list_push_back(wclist, &(new_word_entry->elem));
    return new_word_entry;
  }
}

void fprint_words(word_count_list_t *wclist, FILE *outfile) {
  for (struct list_elem *e = list_begin(wclist); e != list_end(wclist);
       e = list_next(e)) {
    word_count_t *wc = list_entry(e, word_count_t, elem);
    fprintf(outfile, "       %d\t%s\n", wc->count, wc->word);
  }
}

static bool less_list(const struct list_elem *ewc1,
                      const struct list_elem *ewc2, void *aux) {
  word_count_t *wc1 = list_entry(ewc1, word_count_t, elem);
  word_count_t *wc2 = list_entry(ewc2, word_count_t, elem);
  return ((bool (*)(const word_count_t *, const word_count_t *))aux)(wc1, wc2);
}

void wordcount_sort(word_count_list_t *wclist,
                    bool less(const word_count_t *, const word_count_t *)) {
  list_sort(wclist, less_list, less);
}