#ifndef MATLOG_UI_REWRITE_H
#define MATLOG_UI_REWRITE_H

#include <matlog/session/session.h>
#include <matlog/ui/lineedit.h>

/*
 * Inline prettifying of math input: while typing, ASCII spellings turn into
 * their unicode form as soon as the token is complete.
 *
 *	<=  >=  !=  ->  =>  <->      relation and arrow symbols
 *	pi                           the Greek letter (when a non word char follows)
 *	\alpha  \infty  \pm  ...     LaTeX style names (same trigger)
 *	x^2  x^-1  x^10              superscripts (same trigger)
 *	sqrt(x)  sqrt(x+1)           the root sign when the ")" is typed:
 *	                             "√x" and "√(x+1)"
 *	lambda x.                    "λx." in the lambda domain
 *	&&  ||  ~                    the logic symbols in the boolean domain
 *
 * Superscripts are off in the boolean and PGA domains, where ^ is an operator.
 * Backspace pressed right after a rewrite restores the ASCII text that was
 * replaced (a character typed after a word stays in place), any other key
 * makes the rewrite final. Feed all keys of the line through
 * ml_rewriter_handle_key and call ml_rewriter_flush before committing the line.
 */

/*
 * The pseudo domain of notes: free text gets the same rewrites as the domains
 * that have no special rules (pi, \alpha, <=, ->, x^2, sqrt(x)), so a note can
 * say "the area is pi r^2" and show π r². Rules of one domain only (the logic
 * symbols, lambda) are not active in notes.
 */
#define ML_REWRITE_NOTES ((MlDomain)ML_DOMAIN_COUNT)

typedef struct MlRewriter
{
	MlDomain domain;
	MlStr    original;     /* the text the last rewrite replaced */
	uint64_t start;        /* byte offset of the replacement in the line */
	uint64_t replaced_len; /* bytes of the replacement */
	/* bytes after the replacement that stay when undoing */
	uint64_t trigger_len;
	/* the last key was a rewrite that backspace may undo */
	int32_t armed;
} MlRewriter;

void ml_rewriter_init (MlRewriter *rw, MlDomain domain);
void ml_rewriter_free (MlRewriter *rw);

void ml_rewriter_set_domain (MlRewriter *rw, MlDomain domain);

/* Inserts cp at the cursor, applying the rewrites it completes. */
MlResult ml_rewriter_insert_cp (MlRewriter *rw, MlLineEdit *e, uint32_t cp);

/* Undoes the rewrite just made, or deletes the previous character. */
MlResult ml_rewriter_backspace (MlRewriter *rw, MlLineEdit *e);

/*
 * Completes the token before the cursor as if a space followed (without
 * inserting it).
 */
MlResult ml_rewriter_flush (MlRewriter *rw, MlLineEdit *e);

/*
 * Like ml_lineedit_handle_key, with rewriting for characters and undo for
 * Backspace.
 */
MlEditResult ml_rewriter_handle_key (MlRewriter *rw, MlLineEdit *e, MlKey key);

#endif /* MATLOG_UI_REWRITE_H */
