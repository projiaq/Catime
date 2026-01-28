/**
 * @file words_display.h
 * @brief Built-in word display subsystem (CET-4 vocabulary)
 *
 * Provides lightweight word cycling and formatted suffix for clock display.
 * Uses embedded TSV resource for fast startup and simple parsing.
 */

#ifndef WORDS_DISPLAY_H
#define WORDS_DISPLAY_H

#include <windows.h>
#include <stdbool.h>

/* ============================================================================
 * Public configuration globals (persisted in config.ini)
 * ============================================================================ */

/** Enable/disable word display appended to the main clock text */
extern BOOL WORD_DISPLAY_ENABLED;

/** Auto switch interval in seconds. 0 disables auto-switch. */
extern int WORD_SWITCH_INTERVAL_SEC;

/** Show phonetic. */
extern BOOL WORD_SHOW_PHONETIC;

/** Phonetic mode: 0=UK, 1=US, 2=BOTH */
extern int WORD_PHONETIC_MODE;

/** Show Chinese translation (short). */
extern BOOL WORD_SHOW_CHINESE;

/** Max characters for Chinese translation (0 = unlimited). */
extern int WORD_CHINESE_MAX_LEN;

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

/** Initialize word system (loads embedded TSV into memory). Safe to call multiple times. */
BOOL WordsDisplay_Init(void);

/** Shutdown and free memory. */
void WordsDisplay_Shutdown(void);

/* ============================================================================
 * Runtime control
 * ============================================================================ */

/** Tick function; call frequently. Returns TRUE if current word changed. */
BOOL WordsDisplay_Tick(DWORD nowTick);

/** Force to next word. Returns TRUE if changed. */
BOOL WordsDisplay_Next(void);

/* ============================================================================
 * Formatting
 * ============================================================================ */

/**
 * Build formatted word suffix for the clock display.
 * Example: "  abandon [əˈbændən] · 放弃…"
 * Writes empty string if word display disabled or unavailable.
 */
void WordsDisplay_FormatSuffix(wchar_t* out, size_t outChars);

#endif /* WORDS_DISPLAY_H */
