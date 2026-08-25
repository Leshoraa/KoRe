#include "kore_markov.h"
#include <Arduino.h>
#include <esp_random.h>
#include <string.h>
#include <stdio.h>

const char CORPUS_IDLE[] PROGMEM = "lagi santai aja nih kawan. cuacanya enak banget buat rebahan. kamu lagi sibuk ya. aku nungguin kamu selesai kerja aja deh. ngeliatin kamu dari tadi seru juga. hehehe.";
const char CORPUS_BORED[] PROGMEM = "aku bosen banget nih kawan. mending kita main yuk. eh kamu ngapain sih daritadi diem aja. sepi banget di sini. pusing pala barbie nungguin kamu. kamu kok diem aja terus. main bareng aku dong.";
const char CORPUS_JOY[] PROGMEM = "halo kawan! asik banget deh kamu nemenin aku. seneng banget rasanya. kita main bareng yuk. keren banget sih kamu. hehehe seru nih. aku suka gaya kamu kawan.";
const char CORPUS_ANGRY[] PROGMEM = "aku lagi bete nih. jangan ganggu ah. nyebelin banget sih kamu. mending kamu pergi aja deh. sumpah aku lagi males ngomong. kamu kok gitu sih kawan. kesel banget rasanya.";
const char CORPUS_SAD[] PROGMEM = "aku sedih banget. kamu kok jahat banget ninggalin aku sendirian. rasanya pengen nangis aja. hiks. sepi banget gak ada kamu. tolong dong temenin aku kawan.";
const char CORPUS_MISCHIEF[] PROGMEM = "hehehe aku punya ide usil nih. ngagetin kamu seru kali ya. ciluk ba! kaget gak kawan. wleee. jangan marah dong namanya juga bercanda. hehehe usil dikit gapapa kan.";

static const char* getCorpus(Expression expr) {
    switch(expr) {
        case EXPR_IDLE: return CORPUS_IDLE;
        case EXPR_SAD: return CORPUS_SAD;
        case EXPR_ANGRY: return CORPUS_ANGRY;
        case EXPR_JOY: return CORPUS_JOY;
        case EXPR_SHOCK: return CORPUS_IDLE;
        case EXPR_SMIRK: return CORPUS_MISCHIEF;
        case EXPR_DEADPAN: return CORPUS_BORED;
        case EXPR_OVERLOAD: return CORPUS_ANGRY;
        default: return CORPUS_IDLE;
    }
}

static void findNextWord(const char* corpus, const char* currentWord, char* nextWordBuf, size_t bufSize) {
    size_t currLen = strlen(currentWord);
    const char* ptr = corpus;
    int matchCount = 0;

    while (true) {
        ptr = strstr(ptr, currentWord);
        if (!ptr) break;
        bool isStart = (ptr == corpus || *(ptr-1) == ' ');
        bool isEnd = (*(ptr+currLen) == ' ' || *(ptr+currLen) == '.' || *(ptr+currLen) == '!' || *(ptr+currLen) == '?' || *(ptr+currLen) == '\0');
        if (isStart && isEnd && *(ptr+currLen) != '\0') {
            matchCount++;
        }
        ptr += currLen;
    }

    if (matchCount == 0) {
        nextWordBuf[0] = '\0';
        return;
    }

    int pick = esp_random() % matchCount;
    ptr = corpus;
    int currMatch = 0;

    while (true) {
        ptr = strstr(ptr, currentWord);
        if (!ptr) break;
        bool isStart = (ptr == corpus || *(ptr-1) == ' ');
        bool isEnd = (*(ptr+currLen) == ' ' || *(ptr+currLen) == '.' || *(ptr+currLen) == '!' || *(ptr+currLen) == '?' || *(ptr+currLen) == '\0');
        
        if (isStart && isEnd && *(ptr+currLen) != '\0') {
            if (currMatch == pick) {
                const char* nextStart = ptr + currLen;
                while (*nextStart == ' ' || *nextStart == '.' || *nextStart == '!' || *nextStart == '?') {
                    nextStart++;
                }
                if (*nextStart == '\0') {
                    nextWordBuf[0] = '\0';
                    return;
                }
                size_t n = 0;
                while (nextStart[n] != '\0' && nextStart[n] != ' ' && nextStart[n] != '.' && nextStart[n] != '!' && nextStart[n] != '?') {
                    n++;
                }
                if (n < bufSize - 1) {
                    strncpy(nextWordBuf, nextStart, n);
                    nextWordBuf[n] = '\0';
                } else {
                    nextWordBuf[0] = '\0';
                }
                return;
            }
            currMatch++;
        }
        ptr += currLen;
    }
    nextWordBuf[0] = '\0';
}

void generateMarkovText(Expression expr, char* out_buf, size_t max_len) {
    if (!out_buf || max_len < 10) return;
    const char* corpus = getCorpus(expr);
    out_buf[0] = '\0';

    int sentences = 0;
    const char* p = corpus;
    while (*p) {
        if (*p == '.' || *p == '!' || *p == '?') sentences++;
        p++;
    }
    if (sentences == 0) sentences = 1;
    int pickSent = esp_random() % sentences;
    
    p = corpus;
    int curS = 0;
    if (pickSent > 0) {
        while (*p) {
            if (*p == '.' || *p == '!' || *p == '?') {
                curS++;
                if (curS == pickSent) {
                    p++;
                    while (*p == ' ') p++;
                    break;
                }
            }
            p++;
        }
    }

    char currentWord[32] = {0};
    size_t n = 0;
    while (p[n] != '\0' && p[n] != ' ' && p[n] != '.' && p[n] != '!' && p[n] != '?') {
        n++;
    }
    if (n > 0 && n < 31) {
        strncpy(currentWord, p, n);
        currentWord[n] = '\0';
    } else {
        strcpy(currentWord, "aku");
    }

    strncat(out_buf, currentWord, max_len - 1);
    
    // Generate up to 8 words for punchy thoughts
    for (int i = 0; i < 8; i++) {
        char nextWord[32] = {0};
        findNextWord(corpus, currentWord, nextWord, sizeof(nextWord));
        if (nextWord[0] == '\0') break;
        if (strlen(out_buf) + strlen(nextWord) + 2 >= max_len) break;
        
        strncat(out_buf, " ", max_len - strlen(out_buf) - 1);
        strncat(out_buf, nextWord, max_len - strlen(out_buf) - 1);
        strcpy(currentWord, nextWord);
    }
    
    size_t len = strlen(out_buf);
    if (len > 0 && out_buf[len-1] != '.' && out_buf[len-1] != '!' && out_buf[len-1] != '?') {
        if (len < max_len - 1) {
            strcat(out_buf, ".");
        }
    }
}
