/**
 * @file words_display.c
 * @brief Built-in CET-4 word display subsystem
 */

#include "words/words_display.h"
#include "utils/string_convert.h"
#include "log.h"
#include "../../resource/resource.h"
#include <windows.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Globals (config persisted)
 * ============================================================================ */
BOOL WORD_DISPLAY_ENABLED = FALSE;
int  WORD_SWITCH_INTERVAL_SEC = 20;
BOOL WORD_SHOW_PHONETIC = TRUE;
int  WORD_PHONETIC_MODE = 0; /* 0=UK,1=US,2=BOTH */
BOOL WORD_SHOW_CHINESE = TRUE;
int  WORD_CHINESE_MAX_LEN = 10;

/* ============================================================================
 * Internal data
 * ============================================================================ */

typedef struct {
    wchar_t* name;
    wchar_t* uk;
    wchar_t* us;
    wchar_t* trans;
} WordEntry;

static WordEntry* g_words = NULL;
static int g_wordCount = 0;
static int g_currentIndex = -1;

static DWORD g_nextSwitchTick = 0;
static BOOL g_initialized = FALSE;

/* ============================================================================
 * Resource loading helper
 * ============================================================================ */

static BOOL LoadResourceToUtf8Buffer(UINT resourceId, char** outBuffer, DWORD* outSize) {
    if (!outBuffer) return FALSE;
    *outBuffer = NULL;
    if (outSize) *outSize = 0;

    HRSRC hResInfo = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hResInfo) return FALSE;

    DWORD size = SizeofResource(NULL, hResInfo);
    if (size == 0) return FALSE;

    HGLOBAL hResData = LoadResource(NULL, hResInfo);
    if (!hResData) return FALSE;

    const void* pData = LockResource(hResData);
    if (!pData) return FALSE;

    char* buf = (char*)malloc(size + 1);
    if (!buf) return FALSE;

    memcpy(buf, pData, size);
    buf[size] = '\0';

    *outBuffer = buf;
    if (outSize) *outSize = size;
    return TRUE;
}

static void FreeWords(void) {
    if (!g_words) return;
    for (int i = 0; i < g_wordCount; i++) {
        free(g_words[i].name);
        free(g_words[i].uk);
        free(g_words[i].us);
        free(g_words[i].trans);
    }
    free(g_words);
    g_words = NULL;
    g_wordCount = 0;
    g_currentIndex = -1;
}

/* ============================================================================
 * TSV parsing
 * ============================================================================ */

static wchar_t* DupWide(const wchar_t* s) {
    if (!s) return NULL;
    size_t n = wcslen(s);
    wchar_t* out = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
    if (!out) return NULL;
    wcscpy(out, s);
    return out;
}

static void TrimWideInPlace(wchar_t* s) {
    if (!s) return;
    size_t len = wcslen(s);
    while (len > 0 && (s[len-1] == L' ' || s[len-1] == L'\t' || s[len-1] == L'\r' || s[len-1] == L'\n')) {
        s[len-1] = 0;
        len--;
    }
    wchar_t* p = s;
    while (*p == L' ' || *p == L'\t') p++;
    if (p != s) {
        memmove(s, p, (wcslen(p)+1) * sizeof(wchar_t));
    }
}

static BOOL ParseTsvToWords(const char* utf8) {
    if (!utf8) return FALSE;

    /* Convert whole buffer to wide for simpler splitting */
    wchar_t* wide = Utf8ToWideAlloc(utf8);
    if (!wide) return FALSE;

    /* Count lines */
    int lines = 0;
    for (wchar_t* p = wide; *p; p++) if (*p == L'\n') lines++;
    if (lines < 10) {
        free(wide);
        return FALSE;
    }

    WordEntry* arr = (WordEntry*)calloc(lines + 1, sizeof(WordEntry));
    if (!arr) {
        free(wide);
        return FALSE;
    }

    int count = 0;
    wchar_t* ctx = NULL;
    wchar_t* line = wcstok_s(wide, L"\n", &ctx);
    while (line) {
        wchar_t* fields[4] = {0};
        int fi = 0;

        wchar_t* p = line;
        fields[fi++] = p;
        while (*p && fi < 4) {
            if (*p == L'\t') {
                *p = 0;
                fields[fi++] = p + 1;
            }
            p++;
        }
        if (fi >= 1 && fields[0]) {
            for (int k = 0; k < fi; k++) TrimWideInPlace(fields[k]);
            if (fields[0][0] != 0) {
                arr[count].name  = DupWide(fields[0]);
                arr[count].uk    = (fi > 1 && fields[1] && fields[1][0]) ? DupWide(fields[1]) : DupWide(L"");
                arr[count].us    = (fi > 2 && fields[2] && fields[2][0]) ? DupWide(fields[2]) : DupWide(L"");
                arr[count].trans = (fi > 3 && fields[3] && fields[3][0]) ? DupWide(fields[3]) : DupWide(L"");
                if (arr[count].name && arr[count].uk && arr[count].us && arr[count].trans) {
                    count++;
                } else {
                    free(arr[count].name); free(arr[count].uk); free(arr[count].us); free(arr[count].trans);
                    memset(&arr[count], 0, sizeof(WordEntry));
                }
            }
        }
        line = wcstok_s(NULL, L"\n", &ctx);
    }

    free(wide);

    if (count <= 0) {
        free(arr);
        return FALSE;
    }

    g_words = arr;
    g_wordCount = count;
    g_currentIndex = 0;
    return TRUE;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

BOOL WordsDisplay_Init(void) {
    if (g_initialized) return TRUE;
    g_initialized = TRUE;

    char* buf = NULL;
    DWORD size = 0;
    if (!LoadResourceToUtf8Buffer(IDR_WORDS_CET4_TSV, &buf, &size)) {
        LOG_WARNING("WordsDisplay: failed to load embedded TSV resource");
        return FALSE;
    }

    BOOL ok = ParseTsvToWords(buf);
    free(buf);

    if (!ok) {
        LOG_WARNING("WordsDisplay: failed to parse TSV");
        return FALSE;
    }

    /* Start on a random-ish index based on tick count to avoid always first word */
    DWORD tick = GetTickCount();
    if (g_wordCount > 0) {
        g_currentIndex = (int)(tick % (DWORD)g_wordCount);
    }

    g_nextSwitchTick = tick + (DWORD)(WORD_SWITCH_INTERVAL_SEC > 0 ? WORD_SWITCH_INTERVAL_SEC * 1000 : 0);
    LOG_INFO("WordsDisplay initialized with %d words", g_wordCount);
    return TRUE;
}

void WordsDisplay_Shutdown(void) {
    FreeWords();
    g_initialized = FALSE;
}

static BOOL SetCurrentIndex(int idx) {
    if (!g_words || g_wordCount <= 0) return FALSE;
    if (idx < 0) idx = 0;
    if (idx >= g_wordCount) idx = 0;
    if (g_currentIndex != idx) {
        g_currentIndex = idx;
        return TRUE;
    }
    return FALSE;
}

BOOL WordsDisplay_Next(void) {
    if (!g_initialized) WordsDisplay_Init();
    if (!g_words || g_wordCount <= 0) return FALSE;
    BOOL changed = SetCurrentIndex(g_currentIndex + 1);
    DWORD now = GetTickCount();
    if (WORD_SWITCH_INTERVAL_SEC > 0) {
        g_nextSwitchTick = now + (DWORD)WORD_SWITCH_INTERVAL_SEC * 1000;
    }
    return changed;
}

BOOL WordsDisplay_Tick(DWORD nowTick) {
    if (!WORD_DISPLAY_ENABLED) return FALSE;
    if (!g_initialized) WordsDisplay_Init();
    if (!g_words || g_wordCount <= 0) return FALSE;
    if (WORD_SWITCH_INTERVAL_SEC <= 0) return FALSE;

    if (nowTick >= g_nextSwitchTick) {
        BOOL changed = SetCurrentIndex(g_currentIndex + 1);
        g_nextSwitchTick = nowTick + (DWORD)WORD_SWITCH_INTERVAL_SEC * 1000;
        return changed;
    }
    return FALSE;
}

static void AppendWithLimit(wchar_t* out, size_t outChars, const wchar_t* s) {
    if (!out || outChars == 0 || !s) return;
    size_t cur = wcslen(out);
    if (cur >= outChars - 1) return;
    wcsncat(out, s, outChars - 1 - cur);
}

static void AppendCnTruncated(wchar_t* out, size_t outChars, const wchar_t* cn) {
    if (!cn || !cn[0]) return;
    if (WORD_CHINESE_MAX_LEN <= 0) {
        AppendWithLimit(out, outChars, cn);
        return;
    }
    int max = WORD_CHINESE_MAX_LEN;
    int len = (int)wcslen(cn);
    if (len <= max) {
        AppendWithLimit(out, outChars, cn);
        return;
    }
    /* Copy up to max chars and add ellipsis */
    wchar_t tmp[256] = {0};
    int copy = max;
    if (copy > 240) copy = 240;
    wcsncpy(tmp, cn, copy);
    tmp[copy] = 0;
    AppendWithLimit(out, outChars, tmp);
    AppendWithLimit(out, outChars, L"…");
}

void WordsDisplay_FormatSuffix(wchar_t* out, size_t outChars) {
    if (!out || outChars == 0) return;
    out[0] = 0;

    if (!WORD_DISPLAY_ENABLED) return;
    if (!g_initialized) WordsDisplay_Init();
    if (!g_words || g_wordCount <= 0 || g_currentIndex < 0 || g_currentIndex >= g_wordCount) return;

    WordEntry* e = &g_words[g_currentIndex];

    /* Leading spacing to keep time readable */
    AppendWithLimit(out, outChars, L"  ");

    AppendWithLimit(out, outChars, e->name ? e->name : L"");

    if (WORD_SHOW_PHONETIC) {
        if (WORD_PHONETIC_MODE == 2) {
            if (e->uk && e->uk[0]) {
                AppendWithLimit(out, outChars, L" [");
                AppendWithLimit(out, outChars, e->uk);
                AppendWithLimit(out, outChars, L"]");
            }
            if (e->us && e->us[0]) {
                AppendWithLimit(out, outChars, L" [");
                AppendWithLimit(out, outChars, e->us);
                AppendWithLimit(out, outChars, L"]");
            }
        } else if (WORD_PHONETIC_MODE == 1) {
            if (e->us && e->us[0]) {
                AppendWithLimit(out, outChars, L" [");
                AppendWithLimit(out, outChars, e->us);
                AppendWithLimit(out, outChars, L"]");
            }
        } else {
            if (e->uk && e->uk[0]) {
                AppendWithLimit(out, outChars, L" [");
                AppendWithLimit(out, outChars, e->uk);
                AppendWithLimit(out, outChars, L"]");
            }
        }
    }

    if (WORD_SHOW_CHINESE) {
        if (e->trans && e->trans[0]) {
            AppendWithLimit(out, outChars, L" · ");
            AppendCnTruncated(out, outChars, e->trans);
        }
    }
}
