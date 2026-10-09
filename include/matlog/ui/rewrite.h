#ifndef MATLOG_UI_REWRITE_H
#define MATLOG_UI_REWRITE_H

#include <matlog/session/session.h>
#include <matlog/ui/lineedit.h>

/*
 * Inline prettifying of math input: while typing, ASCII spellings turn into
 * their unicode form as soon as the token is complete.
 *
 *	<=  >=  !=  ->  =>  <->      become the relation and arrow symbols
 *	pi                           becomes the Greek letter, once a non word character follows
 *	\alpha  \infty  \pm  ...     LaTeX style names, once a non word character follows
 *	x^2  x^-1  x^10              become superscripts, once a non word character follows
 *	sqrt(x)  sqrt(x+1)           become the root sign: "√x" and "√(x+1)" when the ")" is typed
 *	lambda x.                    becomes "λx." in the lambda domain
 *	&&  ||  ~                    become the logic symbols in the boolean domain
 *
 * Superscripts are off in the boolean and PGA domains, where ^ is an operator.
 * Backspace pressed right after a rewrite restores the ASCII text that was
 * replaced (a character typed after a word stays in place), any other key
 * makes the rewrite final. Feed all keys of the line through
 * ml_rewriter_handle_key and call ml_rewriter_flush before committing the line.
 */

typedef struct MlRewriter
{
	MlDomain domain;
	MlStr    original;     /* the text the last rewrite replaced */
	uint64_t start;        /* byte offset of the replacement in the line */
	uint64_t replaced_len; /* bytes of the replacement */
	uint64_t trigger_len;  /* bytes after the replacement that stay when undoing */
	int32_t  armed;        /* the last key was a rewrite that backspace may undo */
} MlRewriter;

void ml_rewriter_init (MlRewriter *rw, MlDomain domain);
void ml_rewriter_free (MlRewriter *rw);

void ml_rewriter_set_domain (MlRewriter *rw, MlDomain domain);

/* Inserts cp at the cursor, applying the rewrites it completes. */
MlResult ml_rewriter_insert_cp (MlRewriter *rw, MlLineEdit *e, uint32_t cp);

/* Undoes the rewrite just made, or deletes the previous character. */
MlResult ml_rewriter_backspace (MlRewriter *rw, MlLineEdit *e);

/* Completes the token before the cursor as if a space followed (without inserting it). */
MlResult ml_rewriter_flush (MlRewriter *rw, MlLineEdit *e);

/* Like ml_lineedit_handle_key, with rewriting for characters and undo for Backspace. */
MlEditResult ml_rewriter_handle_key (MlRewriter *rw, MlLineEdit *e, MlKey key);

#endif /* MATLOG_UI_REWRITE_H */
